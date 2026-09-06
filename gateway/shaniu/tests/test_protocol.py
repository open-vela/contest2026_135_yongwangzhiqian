# SPDX-License-Identifier: Apache-2.0

import unittest

from shaniu_gateway.protocol import (
    AUDIO_FRAME_BYTES,
    HEADER_BYTES,
    MAGIC,
    MAX_WINDOW,
    VERSION,
    Flag,
    Frame,
    GatewaySession,
    MessageType,
    ProtocolError,
    SessionEvent,
    SessionState,
    decode_window_credit,
    encode_window_credit,
)


class FrameTest(unittest.TestCase):
    def test_wire_layout_matches_the_board_contract(self) -> None:
        frame = Frame(
            message_type=MessageType.HELLO,
            flags=0,
            boot_generation=0x01020304,
            session_id=0x11121314,
            turn_id=0,
            sequence=0x21222324,
            timestamp_ms=0x3132333435363738,
        )

        wire = frame.encode()

        self.assertEqual(len(wire), HEADER_BYTES)
        self.assertEqual(wire[0:4], MAGIC.to_bytes(4, "big"))
        self.assertEqual(wire[4], VERSION)
        self.assertEqual(wire[5], MessageType.HELLO)
        self.assertEqual(wire[8:10], HEADER_BYTES.to_bytes(2, "big"))
        self.assertEqual(wire[16:20], b"\x01\x02\x03\x04")
        self.assertEqual(wire[20:24], b"\x11\x12\x13\x14")
        self.assertEqual(wire[28:32], b"\x21\x22\x23\x24")
        self.assertEqual(wire[32:40], b"\x31\x32\x33\x34\x35\x36\x37\x38")
        self.assertEqual(Frame.decode(wire), frame)

    def test_decode_rejects_size_flags_and_unsupported_vision(self) -> None:
        hello = Frame(
            MessageType.HELLO,
            0,
            boot_generation=1,
            session_id=1,
            turn_id=0,
            sequence=1,
            timestamp_ms=1,
        ).encode()
        with self.assertRaisesRegex(ProtocolError, "frame_size_mismatch"):
            Frame.decode(hello + b"extra")

        bad_flags = bytearray(hello)
        bad_flags[6:8] = (4).to_bytes(2, "big")
        with self.assertRaisesRegex(ProtocolError, "invalid_flags"):
            Frame.decode(bytes(bad_flags))

        vision = bytearray(hello)
        vision[5] = MessageType.VISION_START
        vision[24:28] = (1).to_bytes(4, "big")
        with self.assertRaisesRegex(ProtocolError, "unsupported_message"):
            Frame.decode(bytes(vision))

    def test_audio_and_synthetic_rules_match_the_board_contract(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "invalid_audio_frame"):
            Frame(
                MessageType.AUDIO_UP,
                0,
                1,
                1,
                1,
                1,
                1,
                b"short",
            ).encode()

        with self.assertRaisesRegex(ProtocolError, "invalid_synthetic_flag"):
            Frame(
                MessageType.TTS_START,
                0,
                1,
                1,
                1,
                1,
                1,
            ).encode()

        audio = Frame(
            MessageType.AUDIO_DOWN,
            int(Flag.SYNTHETIC | Flag.END_OF_STREAM),
            1,
            1,
            1,
            1,
            1,
            bytes(AUDIO_FRAME_BYTES),
        )
        self.assertEqual(Frame.decode(audio.encode()), audio)


class GatewaySessionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.session = GatewaySession()
        hello = Frame(MessageType.HELLO, 0, 7, 3, 0, 1, 1)
        result = self.session.receive(hello)
        self.assertEqual(result.event, SessionEvent.HELLO)
        self.assertEqual(
            [item.message_type for item in result.outbound],
            [MessageType.WELCOME, MessageType.WINDOW_UPDATE],
        )
        self.assertEqual(decode_window_credit(result.outbound[1].payload), MAX_WINDOW)

    def test_gap_replay_stale_identity_and_window_overflow_fail_closed(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "sequence_gap"):
            self.session.receive(Frame(MessageType.HEARTBEAT, 0, 7, 3, 0, 3, 1))

        self.session.receive(Frame(MessageType.HEARTBEAT, 0, 7, 3, 0, 2, 1))
        with self.assertRaisesRegex(ProtocolError, "sequence_replay"):
            self.session.receive(Frame(MessageType.HEARTBEAT, 0, 7, 3, 0, 2, 1))

        with self.assertRaisesRegex(ProtocolError, "stale_identity"):
            self.session.receive(Frame(MessageType.HEARTBEAT, 0, 8, 3, 0, 3, 1))

        self.session.receive(
            Frame(
                MessageType.WINDOW_UPDATE,
                0,
                7,
                3,
                0,
                3,
                1,
                encode_window_credit(MAX_WINDOW),
            )
        )
        with self.assertRaisesRegex(ProtocolError, "window_overflow"):
            self.session.receive(
                Frame(
                    MessageType.WINDOW_UPDATE,
                    0,
                    7,
                    3,
                    0,
                    4,
                    1,
                    encode_window_credit(1),
                )
            )

    def test_complete_turn_consumes_both_direction_windows(self) -> None:
        self.session.receive(
            Frame(
                MessageType.WINDOW_UPDATE,
                0,
                7,
                3,
                0,
                2,
                1,
                encode_window_credit(AUDIO_FRAME_BYTES),
            )
        )
        self.session.receive(Frame(MessageType.TURN_START, 0, 7, 3, 1, 3, 1))
        audio_result = self.session.receive(
            Frame(
                MessageType.AUDIO_UP,
                0,
                7,
                3,
                1,
                4,
                1,
                bytes(AUDIO_FRAME_BYTES),
            )
        )
        self.assertEqual(
            audio_result.outbound[0].message_type, MessageType.WINDOW_UPDATE
        )
        self.session.receive(Frame(MessageType.TURN_END, 0, 7, 3, 1, 5, 1))
        self.assertEqual(self.session.state, SessionState.THINKING)

        self.session.make_tts_start(1)
        audio_down = self.session.make_audio_down(
            1,
            bytes(AUDIO_FRAME_BYTES),
            final=True,
        )
        self.assertTrue(audio_down.flags & Flag.END_OF_STREAM)
        self.session.make_tts_end(1)
        self.assertEqual(self.session.state, SessionState.IDLE)


if __name__ == "__main__":
    unittest.main()
