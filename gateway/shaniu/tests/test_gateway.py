# SPDX-License-Identifier: Apache-2.0

import asyncio
import ssl
import subprocess
import tempfile
import unittest
from pathlib import Path

import websockets
from websockets.exceptions import ConnectionClosedError

from shaniu_gateway.protocol import (
    AUDIO_FRAME_BYTES,
    MAX_WINDOW,
    Flag,
    Frame,
    MessageType,
    decode_window_credit,
    encode_window_credit,
    monotonic_ms,
)
from shaniu_gateway.server import (
    DEFAULT_PATH,
    SUBPROTOCOL,
    GatewayConfig,
    GatewayServer,
    _bind_scope,
    build_tls_context,
    deterministic_pcm_frame,
    start_gateway_server,
)


class ClientWire:
    def __init__(self, boot_generation: int = 7, session_id: int = 3) -> None:
        self.boot_generation = boot_generation
        self.session_id = session_id
        self.next_sequence = 1

    def frame(
        self,
        message_type: MessageType,
        *,
        turn_id: int = 0,
        flags: int = 0,
        payload: bytes = b"",
        sequence: int | None = None,
    ) -> Frame:
        if sequence is None:
            sequence = self.next_sequence
            self.next_sequence += 1
        return Frame(
            message_type=message_type,
            flags=flags,
            boot_generation=self.boot_generation,
            session_id=self.session_id,
            turn_id=turn_id,
            sequence=sequence,
            timestamp_ms=monotonic_ms(),
            payload=payload,
        )


class GatewayIntegrationTest(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp_dir = tempfile.TemporaryDirectory(prefix="shaniu-gateway-test-")
        cls.cert_path = Path(cls.temp_dir.name) / "cert.pem"
        cls.key_path = Path(cls.temp_dir.name) / "key.pem"
        subprocess.run(
            [
                "openssl",
                "req",
                "-x509",
                "-newkey",
                "rsa:2048",
                "-nodes",
                "-keyout",
                str(cls.key_path),
                "-out",
                str(cls.cert_path),
                "-days",
                "1",
                "-subj",
                "/CN=localhost",
                "-addext",
                "subjectAltName=DNS:localhost",
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        cls.ca_path = Path(cls.temp_dir.name) / "client-ca.pem"
        cls.ca_key_path = Path(cls.temp_dir.name) / "client-ca.key"
        cls.mtls_cert_path = Path(cls.temp_dir.name) / "mtls-server.pem"
        cls.mtls_key_path = Path(cls.temp_dir.name) / "mtls-server.key"
        cls.trusted_client_cert = Path(cls.temp_dir.name) / "trusted-client.pem"
        cls.trusted_client_key = Path(cls.temp_dir.name) / "trusted-client.key"
        cls.untrusted_client_cert = Path(cls.temp_dir.name) / "untrusted-client.pem"
        cls.untrusted_client_key = Path(cls.temp_dir.name) / "untrusted-client.key"
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(cls.ca_key_path), "-out", str(cls.ca_path),
                "-days", "1", "-subj", "/CN=shaniu-test-client-ca",
            ],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        cls._issue_certificate(
            cls.mtls_cert_path, cls.mtls_key_path, "localhost",
            "subjectAltName=DNS:localhost",
        )
        cls._issue_certificate(
            cls.trusted_client_cert, cls.trusted_client_key, "trusted-client"
        )
        subprocess.run(
            [
                "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                "-keyout", str(cls.untrusted_client_key),
                "-out", str(cls.untrusted_client_cert), "-days", "1",
                "-subj", "/CN=untrusted-client",
            ],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    @classmethod
    def _issue_certificate(
        cls, certificate: Path, key: Path, common_name: str,
        extension: str | None = None,
    ) -> None:
        request = certificate.with_suffix(".csr")
        command = [
            "openssl", "req", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(request),
            "-subj", f"/CN={common_name}",
        ]
        subprocess.run(
            command, check=True, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        command = [
            "openssl", "x509", "-req", "-in", str(request), "-CA",
            str(cls.ca_path), "-CAkey", str(cls.ca_key_path),
            "-CAcreateserial", "-out", str(certificate), "-days", "1",
        ]
        if extension is not None:
            extfile = certificate.with_suffix(".ext")
            extfile.write_text(extension + "\n")
            command += ["-extfile", str(extfile)]
        subprocess.run(
            command, check=True, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temp_dir.cleanup()

    async def asyncSetUp(self) -> None:
        self.events: list[dict[str, object]] = []
        self.loop_errors: list[dict[str, object]] = []
        self.loop = asyncio.get_running_loop()
        self.previous_exception_handler = self.loop.get_exception_handler()
        self.loop.set_exception_handler(
            lambda _loop, context: self.loop_errors.append(dict(context))
        )
        config = GatewayConfig(
            reply_frames=4,
            reply_interval_ms=0,
            downlink_window_timeout_ms=500,
        )
        self.gateway = GatewayServer(
            config, event_sink=lambda event: self.events.append(dict(event))
        )
        self.server = await start_gateway_server(
            self.gateway,
            "127.0.0.1",
            0,
            build_tls_context(self.cert_path, self.key_path),
        )
        self.port = self.server.sockets[0].getsockname()[1]
        self.client_tls = ssl.create_default_context(cafile=self.cert_path)
        self.client_tls.minimum_version = ssl.TLSVersion.TLSv1_2

    async def asyncTearDown(self) -> None:
        self.server.close()
        await self.server.wait_closed()
        self.loop.set_exception_handler(self.previous_exception_handler)
        unexpected = [
            context
            for context in self.loop_errors
            if not isinstance(context.get("exception"), ConnectionResetError)
            and not (
                getattr(self, "expect_tls_handshake_rejection", False)
                and isinstance(context.get("exception"), ssl.SSLError)
                and context["exception"].reason in {
                    "PEER_DID_NOT_RETURN_A_CERTIFICATE",
                    "CERTIFICATE_VERIFY_FAILED",
                }
            )
        ]
        if unexpected:
            self.fail("unexpected asyncio loop error")

    def connect(self):
        return websockets.connect(
            f"wss://localhost:{self.port}{DEFAULT_PATH}",
            ssl=self.client_tls,
            subprotocols=[SUBPROTOCOL],
            compression=None,
            max_size=64 * 1024 + 40,
        )

    async def start_mtls_server(self):
        server = await start_gateway_server(
            self.gateway, "127.0.0.1", 0,
            build_tls_context(
                self.mtls_cert_path, self.mtls_key_path, self.ca_path
            ),
            self.ca_path,
        )
        self.addAsyncCleanup(server.wait_closed)
        self.addAsyncCleanup(server.close)
        return server

    def mtls_client_context(
        self, certificate: Path | None = None, key: Path | None = None
    ) -> ssl.SSLContext:
        context = ssl.create_default_context(cafile=self.ca_path)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        if certificate is not None:
            context.load_cert_chain(certificate, key)
        return context

    async def handshake(self, websocket, client: ClientWire) -> tuple[Frame, Frame]:
        await websocket.send(client.frame(MessageType.HELLO).encode())
        welcome = Frame.decode(await websocket.recv())
        window = Frame.decode(await websocket.recv())
        self.assertEqual(welcome.message_type, MessageType.WELCOME)
        self.assertEqual(window.message_type, MessageType.WINDOW_UPDATE)
        self.assertEqual(decode_window_credit(window.payload), MAX_WINDOW)
        self.assertEqual(websocket.subprotocol, SUBPROTOCOL)
        return welcome, window

    async def wait_for_summary(self) -> dict[str, object]:
        for _ in range(100):
            summaries = [
                event
                for event in self.events
                if event.get("event") == "connection_summary"
            ]
            if summaries:
                return summaries[-1]
            await asyncio.sleep(0.005)
        self.fail("connection summary was not emitted")

    async def test_normal_turn_streams_fixed_pcm_and_ends_idle(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await websocket.send(
                client.frame(
                    MessageType.WINDOW_UPDATE,
                    payload=encode_window_credit(4 * AUDIO_FRAME_BYTES),
                ).encode()
            )
            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=1).encode()
            )
            await websocket.send(
                client.frame(
                    MessageType.AUDIO_UP,
                    turn_id=1,
                    payload=bytes(AUDIO_FRAME_BYTES),
                ).encode()
            )
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())

            replenishment = Frame.decode(await websocket.recv())
            self.assertEqual(replenishment.message_type, MessageType.WINDOW_UPDATE)
            received = []
            while not received or received[-1].message_type is not MessageType.TTS_END:
                received.append(Frame.decode(await websocket.recv()))

            self.assertEqual(received[0].message_type, MessageType.TTS_START)
            audio = [
                frame
                for frame in received
                if frame.message_type is MessageType.AUDIO_DOWN
            ]
            self.assertEqual(len(audio), 4)
            self.assertTrue(
                all(len(frame.payload) == AUDIO_FRAME_BYTES for frame in audio)
            )
            self.assertTrue(all(frame.flags & Flag.SYNTHETIC for frame in audio))
            self.assertTrue(audio[-1].flags & Flag.END_OF_STREAM)
            self.assertEqual(audio[0].payload, deterministic_pcm_frame(0))
            self.assertEqual(received[-1].message_type, MessageType.TTS_END)

        summary = await self.wait_for_summary()
        self.assertEqual(summary["turns_completed"], 1)
        self.assertEqual(summary["uplink_frames"], 1)
        self.assertEqual(summary["downlink_frames"], 4)
        self.assertEqual(summary["protocol_errors"], 0)

    async def test_cancel_stops_a_reply_and_keeps_the_session_usable(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=1).encode()
            )
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())
            await websocket.send(client.frame(MessageType.CANCEL, turn_id=1).encode())
            try:
                possible_start = Frame.decode(
                    await asyncio.wait_for(websocket.recv(), timeout=0.05)
                )
                self.assertEqual(possible_start.message_type, MessageType.TTS_START)
            except asyncio.TimeoutError:
                pass
            self.assertTrue(websocket.open)

            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=2).encode()
            )
            await websocket.send(client.frame(MessageType.CANCEL, turn_id=2).encode())
            await asyncio.sleep(0.02)
            self.assertTrue(websocket.open)

        summary = await self.wait_for_summary()
        self.assertEqual(summary["turns_cancelled"], 2)
        self.assertEqual(summary["turns_completed"], 0)
        self.assertEqual(summary["aborted_turns"], 0)

    async def test_sequence_gap_sends_error_then_closes(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            gap = client.frame(MessageType.TURN_START, turn_id=1, sequence=3)
            await websocket.send(gap.encode())
            error = Frame.decode(await websocket.recv())
            self.assertEqual(error.message_type, MessageType.ERROR)
            self.assertEqual(error.turn_id, 0)
            with self.assertRaises(ConnectionClosedError) as closed:
                await websocket.recv()
            self.assertEqual(closed.exception.code, 1002)

        summary = await self.wait_for_summary()
        self.assertEqual(summary["protocol_errors"], 1)

    async def test_disconnect_mid_uplink_is_counted_as_aborted(self) -> None:
        client = ClientWire()
        websocket = await self.connect()
        await self.handshake(websocket, client)
        await websocket.send(client.frame(MessageType.TURN_START, turn_id=1).encode())
        await websocket.send(
            client.frame(
                MessageType.AUDIO_UP,
                turn_id=1,
                payload=bytes(AUDIO_FRAME_BYTES),
            ).encode()
        )
        replenishment = Frame.decode(await websocket.recv())
        self.assertEqual(replenishment.message_type, MessageType.WINDOW_UPDATE)
        await websocket.close()

        summary = await self.wait_for_summary()
        self.assertEqual(summary["terminal_state"], "uplink")
        self.assertEqual(summary["aborted_turns"], 1)
        self.assertEqual(summary["uplink_frames"], 1)

    async def test_downlink_waits_for_additional_window_credit(self) -> None:
        client = ClientWire()
        async with self.connect() as websocket:
            await self.handshake(websocket, client)
            await websocket.send(
                client.frame(
                    MessageType.WINDOW_UPDATE,
                    payload=encode_window_credit(AUDIO_FRAME_BYTES),
                ).encode()
            )
            await websocket.send(
                client.frame(MessageType.TURN_START, turn_id=1).encode()
            )
            await websocket.send(client.frame(MessageType.TURN_END, turn_id=1).encode())

            self.assertEqual(
                Frame.decode(await websocket.recv()).message_type, MessageType.TTS_START
            )
            first_audio = Frame.decode(await websocket.recv())
            self.assertEqual(first_audio.message_type, MessageType.AUDIO_DOWN)
            with self.assertRaises(asyncio.TimeoutError):
                await asyncio.wait_for(websocket.recv(), timeout=0.05)

            await websocket.send(
                client.frame(
                    MessageType.WINDOW_UPDATE,
                    turn_id=1,
                    payload=encode_window_credit(3 * AUDIO_FRAME_BYTES),
                ).encode()
            )
            tail = [Frame.decode(await websocket.recv()) for _ in range(4)]
            self.assertEqual(
                [frame.message_type for frame in tail],
                [
                    MessageType.AUDIO_DOWN,
                    MessageType.AUDIO_DOWN,
                    MessageType.AUDIO_DOWN,
                    MessageType.TTS_END,
                ],
            )
            self.assertTrue(tail[-2].flags & Flag.END_OF_STREAM)

    async def test_certificate_hostname_mismatch_is_rejected(self) -> None:
        with self.assertRaises((ssl.SSLCertVerificationError, OSError)):
            async with websockets.connect(
                f"wss://127.0.0.1:{self.port}{DEFAULT_PATH}",
                ssl=self.client_tls,
                subprotocols=[SUBPROTOCOL],
            ):
                pass

    async def test_non_loopback_bind_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "loopback"):
            await start_gateway_server(
                self.gateway,
                "0.0.0.0",
                0,
                build_tls_context(self.cert_path, self.key_path),
            )

    async def test_mtls_trusted_client_connects(self) -> None:
        server = await self.start_mtls_server()
        port = server.sockets[0].getsockname()[1]
        async with websockets.connect(
            f"wss://localhost:{port}{DEFAULT_PATH}",
            ssl=self.mtls_client_context(
                self.trusted_client_cert, self.trusted_client_key
            ),
            subprotocols=[SUBPROTOCOL],
        ) as websocket:
            await self.handshake(websocket, ClientWire())

    async def test_bind_checks_actual_tls_context(self) -> None:
        context = build_tls_context(self.cert_path, self.key_path)
        with self.assertRaisesRegex(ValueError, "require client certificates"):
            await start_gateway_server(
                self.gateway, "192.168.1.7", 0, context, self.ca_path
            )
        context.minimum_version = ssl.TLSVersion.MINIMUM_SUPPORTED
        with self.assertRaisesRegex(ValueError, "TLS 1.2"):
            await start_gateway_server(self.gateway, "127.0.0.1", 0, context)

    async def test_mtls_rejects_missing_or_untrusted_client_certificate(self) -> None:
        server = await self.start_mtls_server()
        port = server.sockets[0].getsockname()[1]
        self.expect_tls_handshake_rejection = True
        for context in (
            self.mtls_client_context(),
            self.mtls_client_context(
                self.untrusted_client_cert, self.untrusted_client_key
            ),
        ):
            with self.assertRaises((OSError, ssl.SSLError, websockets.InvalidMessage)):
                async with websockets.connect(
                    f"wss://localhost:{port}{DEFAULT_PATH}",
                    ssl=context, subprotocols=[SUBPROTOCOL],
                ):
                    pass

    def test_bind_scope_gate_table(self) -> None:
        cases = (
            ("127.0.0.1", None, "loopback"),
            ("::1", None, "loopback"),
            ("localhost", None, "loopback"),
            ("192.168.1.7", self.ca_path, "mtls_unicast"),
            ("2001:db8::7", self.ca_path, "mtls_unicast"),
        )
        for host, client_ca, expected in cases:
            self.assertEqual(_bind_scope(host, client_ca), expected)

        for host, client_ca in (
            ("192.168.1.7", None), ("0.0.0.0", self.ca_path),
            ("::", self.ca_path), ("255.255.255.255", self.ca_path),
            ("224.0.0.1", self.ca_path), ("gateway.example", self.ca_path),
        ):
            with self.assertRaises(ValueError):
                _bind_scope(host, client_ca)


if __name__ == "__main__":
    unittest.main()
