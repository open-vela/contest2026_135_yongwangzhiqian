# SPDX-License-Identifier: Apache-2.0
"""TLS-only WebSocket endpoint for the deterministic companion-v1 slice."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import ipaddress
import json
import logging
import ssl
import struct
import sys
import time
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from pathlib import Path
import websockets
from websockets.exceptions import ConnectionClosed
from websockets.server import WebSocketServer, WebSocketServerProtocol

from .protocol import (
    AUDIO_FRAME_BYTES,
    HEADER_BYTES,
    MAX_PAYLOAD,
    Frame,
    GatewaySession,
    MessageType,
    ProtocolError,
    SessionEvent,
    SessionState,
)


SUBPROTOCOL = "companion-v1"
DEFAULT_PATH = "/companion/v1"
_LOOPBACK_HOSTS = {"127.0.0.1", "::1", "localhost"}
EventSink = Callable[[Mapping[str, object]], None]


def json_event_sink(event: Mapping[str, object]) -> None:
    """Write a log-safe aggregate event without payloads or device identity."""

    print(
        json.dumps(dict(event), sort_keys=True, separators=(",", ":")),
        file=sys.stderr,
        flush=True,
    )


@dataclass(frozen=True, slots=True)
class GatewayConfig:
    path: str = DEFAULT_PATH
    reply_frames: int = 10
    reply_interval_ms: int = 20
    downlink_window_timeout_ms: int = 2_000

    def __post_init__(self) -> None:
        if not self.path.startswith("/") or "?" in self.path or "#" in self.path:
            raise ValueError("path must be an absolute path without query or fragment")
        if not 2 <= self.reply_frames <= 500:
            raise ValueError("reply_frames must be in 2..500")
        if not 0 <= self.reply_interval_ms <= 1_000:
            raise ValueError("reply_interval_ms must be in 0..1000")
        if not 10 <= self.downlink_window_timeout_ms <= 60_000:
            raise ValueError("downlink_window_timeout_ms must be in 10..60000")


@dataclass(slots=True)
class ConnectionMetrics:
    turns_started: int = 0
    turns_completed: int = 0
    turns_cancelled: int = 0
    aborted_turns: int = 0
    uplink_frames: int = 0
    uplink_bytes: int = 0
    downlink_frames: int = 0
    downlink_bytes: int = 0
    protocol_errors: int = 0


class GatewayConnection:
    def __init__(
        self,
        websocket: WebSocketServerProtocol,
        path: str,
        config: GatewayConfig,
        event_sink: EventSink,
    ) -> None:
        self.websocket = websocket
        self.path = path
        self.config = config
        self.emit = event_sink
        self.session = GatewaySession()
        self.metrics = ConnectionMetrics()
        self._credit_changed = asyncio.Condition()
        self._reply_task: asyncio.Task[None] | None = None
        self._closing = False

    async def run(self) -> None:
        if self.path != self.config.path:
            await self.websocket.close(code=1008, reason="invalid_path")
            return
        if self.websocket.subprotocol != SUBPROTOCOL:
            await self.websocket.close(code=1002, reason="subprotocol_required")
            return

        self.emit({"event": "connection_open", "state": self.session.state.value})
        try:
            async for wire in self.websocket:
                if not isinstance(wire, bytes):
                    raise ProtocolError("binary_frame_required")
                frame = Frame.decode(wire)
                result = self.session.receive(frame)
                self._account_inbound(result.event, frame)
                for outbound in result.outbound:
                    await self._send(outbound)

                if result.event is SessionEvent.TURN_ENDED:
                    self.emit(
                        {"event": "turn_state", "state": SessionState.THINKING.value}
                    )
                    self._start_reply(frame.turn_id, time.monotonic())
                elif result.event is SessionEvent.CANCELLED:
                    self.metrics.turns_cancelled += 1
                    await self._cancel_reply()
                    await self._notify_credit_changed()
                    self.emit({"event": "turn_state", "state": SessionState.IDLE.value})
                elif result.event is SessionEvent.CONTROL:
                    await self._notify_credit_changed()
                elif result.event is SessionEvent.PEER_ERROR:
                    await self._cancel_reply()
                    await self._notify_credit_changed()
                    if self.session.state is SessionState.CLOSED:
                        await self.websocket.close(code=1000, reason="peer_error")
                    else:
                        self.emit(
                            {"event": "turn_state", "state": SessionState.IDLE.value}
                        )
        except ProtocolError as error:
            await self._fail_closed(error)
        except ConnectionClosed:
            pass
        except asyncio.CancelledError:
            raise
        except Exception:
            await self._internal_failure()
        finally:
            terminal_state = self.session.state.value
            if self.session.state in {
                SessionState.UPLINK,
                SessionState.THINKING,
                SessionState.DOWNLINK,
            }:
                self.metrics.aborted_turns += 1
            await self._cancel_reply()
            self.emit(
                {
                    "event": "connection_summary",
                    "terminal_state": terminal_state,
                    "turns_started": self.metrics.turns_started,
                    "turns_completed": self.metrics.turns_completed,
                    "turns_cancelled": self.metrics.turns_cancelled,
                    "aborted_turns": self.metrics.aborted_turns,
                    "uplink_frames": self.metrics.uplink_frames,
                    "uplink_bytes": self.metrics.uplink_bytes,
                    "downlink_frames": self.metrics.downlink_frames,
                    "downlink_bytes": self.metrics.downlink_bytes,
                    "protocol_errors": self.metrics.protocol_errors,
                }
            )

    def _account_inbound(self, event: SessionEvent, frame: Frame) -> None:
        if event is SessionEvent.TURN_STARTED:
            self.metrics.turns_started += 1
            self.emit({"event": "turn_state", "state": SessionState.UPLINK.value})
        elif event is SessionEvent.AUDIO_UP:
            self.metrics.uplink_frames += 1
            self.metrics.uplink_bytes += len(frame.payload)

    def _start_reply(self, turn_id: int, turn_end_time: float) -> None:
        if self._reply_task is not None and not self._reply_task.done():
            raise ProtocolError("reply_already_active")
        self._reply_task = asyncio.create_task(
            self._stream_reply(turn_id, turn_end_time),
            name="shaniu-fixed-reply",
        )

    async def _stream_reply(self, turn_id: int, turn_end_time: float) -> None:
        try:
            if (
                self.session.state is not SessionState.THINKING
                or self.session.turn_id != turn_id
            ):
                return
            await self._send(self.session.make_tts_start(turn_id))
            first_audio = True
            for frame_index in range(self.config.reply_frames):
                await self._wait_for_downlink_credit(turn_id)
                if self.session.state is not SessionState.DOWNLINK:
                    return
                payload = deterministic_pcm_frame(frame_index)
                outbound = self.session.make_audio_down(
                    turn_id,
                    payload,
                    final=frame_index == self.config.reply_frames - 1,
                )
                await self._send(outbound)
                self.metrics.downlink_frames += 1
                self.metrics.downlink_bytes += len(payload)
                if first_audio:
                    first_audio = False
                    latency_ms = int((time.monotonic() - turn_end_time) * 1_000)
                    self.emit(
                        {
                            "event": "first_audio",
                            "state": SessionState.DOWNLINK.value,
                            "latency_ms": latency_ms,
                        }
                    )
                if self.config.reply_interval_ms:
                    await asyncio.sleep(self.config.reply_interval_ms / 1_000)

            await self._send(self.session.make_tts_end(turn_id))
            self.metrics.turns_completed += 1
            self.emit({"event": "turn_state", "state": SessionState.IDLE.value})
        except asyncio.CancelledError:
            raise
        except ProtocolError as error:
            await self._fail_closed(error)
        except ConnectionClosed:
            pass
        except Exception:
            await self._internal_failure()

    async def _wait_for_downlink_credit(self, turn_id: int) -> None:
        timeout = self.config.downlink_window_timeout_ms / 1_000
        try:
            async with self._credit_changed:
                await asyncio.wait_for(
                    self._credit_changed.wait_for(
                        lambda: self.session.state is not SessionState.DOWNLINK
                        or self.session.turn_id != turn_id
                        or self.session.downlink_credit >= AUDIO_FRAME_BYTES
                    ),
                    timeout=timeout,
                )
        except TimeoutError as error:
            raise ProtocolError("downlink_window_timeout") from error

    async def _notify_credit_changed(self) -> None:
        async with self._credit_changed:
            self._credit_changed.notify_all()

    async def _send(self, frame: Frame) -> None:
        await self.websocket.send(frame.encode())

    async def _cancel_reply(self) -> None:
        task = self._reply_task
        self._reply_task = None
        if task is None or task.done():
            if task is not None:
                with contextlib.suppress(asyncio.CancelledError, Exception):
                    await task
            return
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task

    async def _fail_closed(self, error: ProtocolError) -> None:
        if self._closing:
            return
        self._closing = True
        self.metrics.protocol_errors += 1
        self.emit(
            {
                "event": "protocol_error",
                "state": self.session.state.value,
                "code": error.code,
            }
        )
        with contextlib.suppress(ProtocolError, ConnectionClosed):
            outbound = self.session.make_error()
            if outbound is not None:
                await self._send(outbound)
        with contextlib.suppress(ConnectionClosed):
            await self.websocket.close(code=1002, reason=error.code[:123])

    async def _internal_failure(self) -> None:
        if self._closing:
            return
        self._closing = True
        self.emit(
            {
                "event": "internal_error",
                "state": self.session.state.value,
                "code": "internal_error",
            }
        )
        with contextlib.suppress(ConnectionClosed):
            await self.websocket.close(code=1011, reason="internal_error")


class GatewayServer:
    def __init__(
        self,
        config: GatewayConfig | None = None,
        event_sink: EventSink = json_event_sink,
    ) -> None:
        self.config = config or GatewayConfig()
        self.event_sink = event_sink

    async def handle(self, websocket: WebSocketServerProtocol, path: str) -> None:
        connection = GatewayConnection(websocket, path, self.config, self.event_sink)
        await connection.run()


def deterministic_pcm_frame(frame_index: int) -> bytes:
    """Generate one 20 ms 16 kHz mono S16LE tone frame without WAV buffering."""

    if frame_index < 0:
        raise ValueError("frame_index must be non-negative")
    payload = bytearray(AUDIO_FRAME_BYTES)
    first_sample = frame_index * (AUDIO_FRAME_BYTES // 2)
    for offset in range(0, AUDIO_FRAME_BYTES, 2):
        sample_index = first_sample + offset // 2
        sample = 1_400 if ((sample_index * 440) // 16_000) % 2 == 0 else -1_400
        struct.pack_into("<h", payload, offset, sample)
    return bytes(payload)


def build_tls_context(
    cert_path: Path, key_path: Path, client_ca: Path | None = None
) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(certfile=cert_path, keyfile=key_path)
    if client_ca is not None:
        context.load_verify_locations(cafile=client_ca)
        context.verify_mode = ssl.CERT_REQUIRED
    return context


def _bind_scope(host: str, client_ca: Path | None) -> str:
    if host in _LOOPBACK_HOSTS:
        return "loopback"

    if client_ca is None:
        raise ValueError("non-loopback binds require a client CA")

    try:
        address = ipaddress.ip_address(host)
    except ValueError as error:
        raise ValueError("non-loopback bind must be a literal IP address") from error

    if (address.is_loopback or address.is_unspecified or address.is_multicast
            or address.is_reserved or address == ipaddress.ip_address(
                "255.255.255.255")):
        raise ValueError("bind must be a unicast non-loopback IP address")

    return "mtls_unicast"


async def start_gateway_server(
    gateway: GatewayServer,
    host: str,
    port: int,
    tls_context: ssl.SSLContext,
    client_ca: Path | None = None,
) -> WebSocketServer:
    if tls_context is None:
        raise ValueError("TLS context is required")
    scope = _bind_scope(host, client_ca)
    if tls_context.minimum_version < ssl.TLSVersion.TLSv1_2:
        raise ValueError("TLS 1.2 or newer is required")
    if scope != "loopback" and tls_context.verify_mode != ssl.CERT_REQUIRED:
        raise ValueError("non-loopback TLS context must require client certificates")

    quiet_logger = logging.getLogger("shaniu_gateway.websocket")
    quiet_logger.setLevel(logging.CRITICAL)
    return await websockets.serve(
        gateway.handle,
        host,
        port,
        ssl=tls_context,
        subprotocols=[SUBPROTOCOL],
        compression=None,
        max_size=HEADER_BYTES + MAX_PAYLOAD,
        max_queue=4,
        ping_interval=20,
        ping_timeout=20,
        close_timeout=2,
        logger=quiet_logger,
    )


async def _run(args: argparse.Namespace) -> None:
    config = GatewayConfig(
        path=args.path,
        reply_frames=args.reply_frames,
        reply_interval_ms=args.reply_interval_ms,
        downlink_window_timeout_ms=args.window_timeout_ms,
    )
    gateway = GatewayServer(config)
    tls_context = build_tls_context(args.cert, args.key, args.client_ca)
    scope = _bind_scope(args.host, args.client_ca)
    server = await start_gateway_server(
        gateway, args.host, args.port, tls_context, args.client_ca
    )
    gateway.event_sink(
        {
            "event": "server_started",
            "transport": "wss",
            "scope": scope,
            "port": args.port,
        }
    )
    try:
        await server.wait_closed()
    finally:
        server.close()
        await server.wait_closed()


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Shaniu companion-v1 deterministic WSS Gateway"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--path", default=DEFAULT_PATH)
    parser.add_argument("--cert", type=Path, required=True)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--client-ca", type=Path)
    parser.add_argument("--reply-frames", type=int, default=10)
    parser.add_argument("--reply-interval-ms", type=int, default=20)
    parser.add_argument("--window-timeout-ms", type=int, default=2_000)
    return parser


def main() -> int:
    args = _parser().parse_args()
    try:
        asyncio.run(_run(args))
    except KeyboardInterrupt:
        return 0
    except (OSError, ssl.SSLError, ValueError):
        json_event_sink({"event": "server_error", "code": "startup_failed"})
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
