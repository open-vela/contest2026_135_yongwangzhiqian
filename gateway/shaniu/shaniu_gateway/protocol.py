# SPDX-License-Identifier: Apache-2.0
"""Transport-independent companion-v1 framing and Gateway session policy."""

from __future__ import annotations

import enum
import struct
import time
from dataclasses import dataclass


MAGIC = 0x424B5631
VERSION = 1
HEADER_BYTES = 40
AUDIO_FRAME_BYTES = 640
MAX_PAYLOAD = 64 * 1024
MAX_WINDOW = 256 * 1024
UINT32_MAX = (1 << 32) - 1

_HEADER = struct.Struct("!IBBHHHIIIIIQ")
_WINDOW = struct.Struct("!I")


class MessageType(enum.IntEnum):
    HELLO = 1
    WELCOME = 2
    TURN_START = 3
    AUDIO_UP = 4
    TURN_END = 5
    TTS_START = 6
    AUDIO_DOWN = 7
    TTS_END = 8
    VISION_START = 9
    VISION_CHUNK = 10
    VISION_END = 11
    CANCEL = 12
    ACK = 13
    WINDOW_UPDATE = 14
    HEARTBEAT = 15
    ERROR = 16


class Flag(enum.IntFlag):
    SYNTHETIC = 1 << 0
    END_OF_STREAM = 1 << 1


FLAG_MASK = int(Flag.SYNTHETIC | Flag.END_OF_STREAM)

_TURN_TYPES = {
    MessageType.TURN_START,
    MessageType.AUDIO_UP,
    MessageType.TURN_END,
    MessageType.TTS_START,
    MessageType.AUDIO_DOWN,
    MessageType.TTS_END,
    MessageType.VISION_START,
    MessageType.VISION_CHUNK,
    MessageType.VISION_END,
    MessageType.CANCEL,
}
_VISION_TYPES = {
    MessageType.VISION_START,
    MessageType.VISION_CHUNK,
    MessageType.VISION_END,
}
_SYNTHETIC_TYPES = {
    MessageType.TTS_START,
    MessageType.AUDIO_DOWN,
    MessageType.TTS_END,
}
_EMPTY_PAYLOAD_TYPES = {
    MessageType.TURN_START,
    MessageType.TURN_END,
    MessageType.TTS_START,
    MessageType.TTS_END,
    MessageType.CANCEL,
    MessageType.HEARTBEAT,
}


class ProtocolError(Exception):
    """A fail-closed wire or session violation with a log-safe code."""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


@dataclass(frozen=True, slots=True)
class Frame:
    message_type: MessageType
    flags: int
    boot_generation: int
    session_id: int
    turn_id: int
    sequence: int
    timestamp_ms: int
    payload: bytes = b""

    def validate(self) -> None:
        _require_uint(self.flags, 16, "invalid_flags")
        _require_uint(self.boot_generation, 32, "invalid_identity")
        _require_uint(self.session_id, 32, "invalid_identity")
        _require_uint(self.turn_id, 32, "invalid_turn")
        _require_uint(self.sequence, 32, "invalid_sequence")
        _require_uint(self.timestamp_ms, 64, "invalid_timestamp")

        if not isinstance(self.message_type, MessageType):
            raise ProtocolError("invalid_message_type")
        if not isinstance(self.payload, bytes):
            raise ProtocolError("invalid_payload")
        if self.flags & ~FLAG_MASK:
            raise ProtocolError("invalid_flags")
        if self.boot_generation == 0 or self.session_id == 0:
            raise ProtocolError("invalid_identity")
        if self.sequence == 0:
            raise ProtocolError("invalid_sequence")
        if len(self.payload) > MAX_PAYLOAD:
            raise ProtocolError("payload_too_large")
        if self.message_type in _VISION_TYPES:
            raise ProtocolError("unsupported_message")
        if self.message_type in _TURN_TYPES and self.turn_id == 0:
            raise ProtocolError("invalid_turn")
        if (
            self.message_type in {MessageType.HELLO, MessageType.WELCOME}
            and self.turn_id != 0
        ):
            raise ProtocolError("invalid_turn")
        if self.message_type in {MessageType.AUDIO_UP, MessageType.AUDIO_DOWN}:
            if len(self.payload) != AUDIO_FRAME_BYTES:
                raise ProtocolError("invalid_audio_frame")
        if (
            self.message_type is MessageType.WINDOW_UPDATE
            and len(self.payload) != _WINDOW.size
        ):
            raise ProtocolError("invalid_window_update")
        if self.message_type in _EMPTY_PAYLOAD_TYPES and self.payload:
            raise ProtocolError("unexpected_payload")

        synthetic = bool(self.flags & Flag.SYNTHETIC)
        if (self.message_type in _SYNTHETIC_TYPES) != synthetic:
            raise ProtocolError("invalid_synthetic_flag")
        if self.flags & Flag.END_OF_STREAM and self.message_type not in {
            MessageType.AUDIO_UP,
            MessageType.AUDIO_DOWN,
        }:
            raise ProtocolError("invalid_end_of_stream_flag")

    def encode(self) -> bytes:
        self.validate()
        header = _HEADER.pack(
            MAGIC,
            VERSION,
            int(self.message_type),
            self.flags,
            HEADER_BYTES,
            0,
            len(self.payload),
            self.boot_generation,
            self.session_id,
            self.turn_id,
            self.sequence,
            self.timestamp_ms,
        )
        return header + self.payload

    @classmethod
    def decode(cls, wire: bytes) -> "Frame":
        if not isinstance(wire, bytes):
            raise ProtocolError("binary_frame_required")
        if len(wire) < HEADER_BYTES:
            raise ProtocolError("frame_too_short")

        (
            magic,
            version,
            raw_type,
            flags,
            header_len,
            reserved,
            payload_len,
            boot_generation,
            session_id,
            turn_id,
            sequence,
            timestamp_ms,
        ) = _HEADER.unpack_from(wire)

        if (
            magic != MAGIC
            or version != VERSION
            or header_len != HEADER_BYTES
            or reserved != 0
        ):
            raise ProtocolError("invalid_header")
        if payload_len > MAX_PAYLOAD:
            raise ProtocolError("payload_too_large")
        if len(wire) != HEADER_BYTES + payload_len:
            raise ProtocolError("frame_size_mismatch")
        try:
            message_type = MessageType(raw_type)
        except ValueError as error:
            raise ProtocolError("invalid_message_type") from error

        frame = cls(
            message_type=message_type,
            flags=flags,
            boot_generation=boot_generation,
            session_id=session_id,
            turn_id=turn_id,
            sequence=sequence,
            timestamp_ms=timestamp_ms,
            payload=wire[HEADER_BYTES:],
        )
        frame.validate()
        return frame


class SessionState(enum.Enum):
    WAIT_HELLO = "wait_hello"
    IDLE = "idle"
    UPLINK = "uplink"
    THINKING = "thinking"
    DOWNLINK = "downlink"
    CLOSED = "closed"


class SessionEvent(enum.Enum):
    HELLO = "hello"
    TURN_STARTED = "turn_started"
    AUDIO_UP = "audio_up"
    TURN_ENDED = "turn_ended"
    CANCELLED = "cancelled"
    PEER_ERROR = "peer_error"
    CONTROL = "control"


@dataclass(frozen=True, slots=True)
class ReceiveResult:
    event: SessionEvent
    outbound: tuple[Frame, ...] = ()


class GatewaySession:
    """Server-side mirror of the single-device companion-v1 state machine."""

    def __init__(self) -> None:
        self.state = SessionState.WAIT_HELLO
        self.boot_generation = 0
        self.session_id = 0
        self.turn_id = 0
        self.incoming_sequence = 0
        self.outgoing_sequence = 0
        self.uplink_credit = 0
        self.downlink_credit = 0

    @property
    def identity_bound(self) -> bool:
        return self.boot_generation != 0 and self.session_id != 0

    def receive(self, frame: Frame) -> ReceiveResult:
        frame.validate()
        if self.state is SessionState.WAIT_HELLO:
            return self._receive_hello(frame)
        if self.state is SessionState.CLOSED:
            raise ProtocolError("session_closed")
        if (
            frame.boot_generation != self.boot_generation
            or frame.session_id != self.session_id
        ):
            raise ProtocolError("stale_identity")
        if frame.sequence <= self.incoming_sequence:
            raise ProtocolError("sequence_replay")
        if frame.sequence == UINT32_MAX:
            raise ProtocolError("sequence_exhausted")
        if frame.sequence != self.incoming_sequence + 1:
            raise ProtocolError("sequence_gap")

        result = self._transition(frame)
        self.incoming_sequence = frame.sequence
        return result

    def _receive_hello(self, frame: Frame) -> ReceiveResult:
        if frame.message_type is not MessageType.HELLO:
            raise ProtocolError("hello_required")
        if frame.sequence != 1:
            raise ProtocolError("sequence_gap")

        self.boot_generation = frame.boot_generation
        self.session_id = frame.session_id
        self.incoming_sequence = frame.sequence
        self.state = SessionState.IDLE
        welcome = self._outbound(MessageType.WELCOME, turn_id=0)
        window = self._grant_uplink(MAX_WINDOW)
        return ReceiveResult(SessionEvent.HELLO, (welcome, window))

    def _transition(self, frame: Frame) -> ReceiveResult:
        message_type = frame.message_type
        if message_type is MessageType.WINDOW_UPDATE:
            self._require_ready()
            self._require_control_turn(frame.turn_id)
            (credit,) = _WINDOW.unpack(frame.payload)
            if credit == 0 or credit > MAX_WINDOW:
                raise ProtocolError("invalid_window_credit")
            if self.downlink_credit > MAX_WINDOW - credit:
                raise ProtocolError("window_overflow")
            self.downlink_credit += credit
            return ReceiveResult(SessionEvent.CONTROL)

        if message_type is MessageType.TURN_START:
            if self.state is not SessionState.IDLE:
                raise ProtocolError("invalid_state")
            if self.turn_id == UINT32_MAX or frame.turn_id != self.turn_id + 1:
                raise ProtocolError("stale_turn")
            self.turn_id = frame.turn_id
            self.state = SessionState.UPLINK
            return ReceiveResult(SessionEvent.TURN_STARTED)

        if message_type is MessageType.AUDIO_UP:
            self._require_state_and_turn(SessionState.UPLINK, frame.turn_id)
            if self.uplink_credit < len(frame.payload):
                raise ProtocolError("uplink_window_exhausted")
            self.uplink_credit -= len(frame.payload)
            replenishment = self._grant_uplink(len(frame.payload))
            return ReceiveResult(SessionEvent.AUDIO_UP, (replenishment,))

        if message_type is MessageType.TURN_END:
            self._require_state_and_turn(SessionState.UPLINK, frame.turn_id)
            self.state = SessionState.THINKING
            return ReceiveResult(SessionEvent.TURN_ENDED)

        if message_type is MessageType.CANCEL:
            if (
                self.state
                not in {
                    SessionState.UPLINK,
                    SessionState.THINKING,
                    SessionState.DOWNLINK,
                }
                or frame.turn_id != self.turn_id
            ):
                raise ProtocolError("stale_turn")
            self.state = SessionState.IDLE
            return ReceiveResult(SessionEvent.CANCELLED)

        if message_type in {MessageType.ACK, MessageType.HEARTBEAT}:
            self._require_ready()
            self._require_control_turn(frame.turn_id)
            return ReceiveResult(SessionEvent.CONTROL)

        if message_type is MessageType.ERROR:
            self._require_ready()
            self._require_control_turn(frame.turn_id)
            self.state = (
                SessionState.CLOSED if frame.turn_id == 0 else SessionState.IDLE
            )
            return ReceiveResult(SessionEvent.PEER_ERROR)

        raise ProtocolError("invalid_direction")

    def make_tts_start(self, turn_id: int) -> Frame:
        self._require_state_and_turn(SessionState.THINKING, turn_id)
        frame = self._outbound(
            MessageType.TTS_START,
            turn_id=turn_id,
            flags=int(Flag.SYNTHETIC),
        )
        self.state = SessionState.DOWNLINK
        return frame

    def make_audio_down(self, turn_id: int, payload: bytes, *, final: bool) -> Frame:
        self._require_state_and_turn(SessionState.DOWNLINK, turn_id)
        if self.downlink_credit < len(payload):
            raise ProtocolError("downlink_window_exhausted")
        flags = int(Flag.SYNTHETIC)
        if final:
            flags |= int(Flag.END_OF_STREAM)
        frame = self._outbound(
            MessageType.AUDIO_DOWN,
            turn_id=turn_id,
            flags=flags,
            payload=payload,
        )
        self.downlink_credit -= len(payload)
        return frame

    def make_tts_end(self, turn_id: int) -> Frame:
        self._require_state_and_turn(SessionState.DOWNLINK, turn_id)
        frame = self._outbound(
            MessageType.TTS_END,
            turn_id=turn_id,
            flags=int(Flag.SYNTHETIC),
        )
        self.state = SessionState.IDLE
        return frame

    def make_error(self) -> Frame | None:
        if not self.identity_bound or self.state is SessionState.CLOSED:
            return None
        return self._outbound(MessageType.ERROR, turn_id=0)

    def _grant_uplink(self, credit: int) -> Frame:
        if credit <= 0 or credit > MAX_WINDOW:
            raise ProtocolError("invalid_window_credit")
        if self.uplink_credit > MAX_WINDOW - credit:
            raise ProtocolError("window_overflow")
        frame = self._outbound(
            MessageType.WINDOW_UPDATE,
            turn_id=0,
            payload=_WINDOW.pack(credit),
        )
        self.uplink_credit += credit
        return frame

    def _outbound(
        self,
        message_type: MessageType,
        *,
        turn_id: int,
        flags: int = 0,
        payload: bytes = b"",
    ) -> Frame:
        if not self.identity_bound:
            raise ProtocolError("identity_not_bound")
        if self.outgoing_sequence >= UINT32_MAX - 1:
            raise ProtocolError("sequence_exhausted")
        frame = Frame(
            message_type=message_type,
            flags=flags,
            boot_generation=self.boot_generation,
            session_id=self.session_id,
            turn_id=turn_id,
            sequence=self.outgoing_sequence + 1,
            timestamp_ms=monotonic_ms(),
            payload=payload,
        )
        frame.validate()
        self.outgoing_sequence = frame.sequence
        return frame

    def _require_ready(self) -> None:
        if self.state not in {
            SessionState.IDLE,
            SessionState.UPLINK,
            SessionState.THINKING,
            SessionState.DOWNLINK,
        }:
            raise ProtocolError("invalid_state")

    def _require_control_turn(self, turn_id: int) -> None:
        if turn_id not in {0, self.turn_id}:
            raise ProtocolError("stale_turn")

    def _require_state_and_turn(self, state: SessionState, turn_id: int) -> None:
        if self.state is not state:
            raise ProtocolError("invalid_state")
        if turn_id != self.turn_id:
            raise ProtocolError("stale_turn")


def monotonic_ms() -> int:
    return time.monotonic_ns() // 1_000_000


def encode_window_credit(credit: int) -> bytes:
    if credit <= 0 or credit > MAX_WINDOW:
        raise ProtocolError("invalid_window_credit")
    return _WINDOW.pack(credit)


def decode_window_credit(payload: bytes) -> int:
    if len(payload) != _WINDOW.size:
        raise ProtocolError("invalid_window_update")
    (credit,) = _WINDOW.unpack(payload)
    if credit == 0 or credit > MAX_WINDOW:
        raise ProtocolError("invalid_window_credit")
    return credit


def _require_uint(value: int, bits: int, code: str) -> None:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value >= 1 << bits
    ):
        raise ProtocolError(code)
