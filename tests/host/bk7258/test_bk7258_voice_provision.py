#!/usr/bin/env python3
"""Offline checks for the private BKVoice provisioner helper."""

from __future__ import annotations

import sys
import json
import base64
import hashlib
import tempfile
import time
from unittest.mock import patch
from pathlib import Path
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/bk7258"))
from _lib import voice


class VoiceProvisionTests(unittest.TestCase):
    def test_console_enrollment_reads_private_token_and_never_returns_it(self):
        pin = "sha256/" + base64.b64encode(b"p" * 32).decode("ascii")
        token = "a" * 32
        expiry = int(time.time() * 1000) + 60_000
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            token_file = root / "token"
            token_file.write_text(token, encoding="ascii")
            token_file.chmod(0o600)
            output = root / "enrollment.json"
            access_output = root / "access.json"
            args = SimpleNamespace(device_id="test-device", https_origin="https://gateway.test:8443/",
                                   spki_pin=[pin], token_file=token_file,
                                   expires_at_ms=str(expiry), output=output,
                                   access_output=access_output)
            with patch.object(voice.os, "fsync", wraps=voice.os.fsync) as fsync:
                result = voice._console_enrollment(args)
            self.assertEqual(fsync.call_count, 2)
            enrollment = json.loads(output.read_text(encoding="utf-8"))
            access = json.loads(access_output.read_text(encoding="utf-8"))
            self.assertEqual(result, {"status": "console-enrollment-written", "device_id": "test-device",
                                      "https_origin": "https://gateway.test:8443", "spki_pins": 1,
                                      "expires_at_ms": expiry,
                                      "access_registry_written": True})
            self.assertEqual(output.stat().st_mode & 0o777, 0o600)
            self.assertEqual(access_output.stat().st_mode & 0o777, 0o600)
            self.assertEqual(set(enrollment), {"protocol", "device_id", "gateway_origin",
                                               "certificate_pins", "access_token", "expires_at_ms"})
            self.assertEqual(enrollment["access_token"], token)
            self.assertEqual(access, {
                "format": "shaniu.console-access/1",
                "grants": [{
                    "device_id": "test-device",
                    "expires_at_ms": expiry,
                    "token_sha256": hashlib.sha256(token.encode("ascii")).hexdigest(),
                    "write": True,
                }],
            })
            self.assertNotIn(token, json.dumps(result))
            with self.assertRaises(voice.VoiceProvisionError):
                voice._console_enrollment(args)
            access_output.unlink()
            with self.assertRaises(voice.VoiceProvisionError):
                voice._console_enrollment(args)
            self.assertFalse(access_output.exists())
            self.assertTrue(output.exists())

    def test_console_enrollment_rejects_unsafe_input(self):
        pin = "sha256/" + base64.b64encode(b"p" * 32).decode("ascii")
        expiry = int(time.time() * 1000) + 60_000
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            token_file = root / "token"
            token_file.write_text("a" * 32, encoding="ascii")
            token_file.chmod(0o644)
            args = SimpleNamespace(device_id="test-device", https_origin="https://gateway.test",
                                   spki_pin=[pin], token_file=token_file,
                                   expires_at_ms=str(expiry), output=root / "output",
                                   access_output=root / "access")
            with self.assertRaises(voice.VoiceProvisionError): voice._console_enrollment(args)
            token_file.chmod(0o600)
            for field, value in (("device_id", "../device"), ("https_origin", "http://gateway.test"),
                                 ("https_origin", "https://gateway.test/path"),
                                 ("spki_pin", [pin, pin]), ("spki_pin", ["sha256/not-base64"]),
                                 ("expires_at_ms", "001"), ("expires_at_ms", "1")):
                original = getattr(args, field)
                setattr(args, field, value)
                with self.assertRaises(voice.VoiceProvisionError): voice._console_enrollment(args)
                self.assertFalse(args.output.exists())
                self.assertFalse(args.access_output.exists())
                setattr(args, field, original)
            args.access_output = args.output
            with self.assertRaises(voice.VoiceProvisionError): voice._console_enrollment(args)
            self.assertFalse(args.output.exists())

    def test_console_enrollment_refuses_windows_without_acl_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = SimpleNamespace(
                device_id="test-device", https_origin="https://gateway.test",
                spki_pin=["sha256/" + base64.b64encode(b"p" * 32).decode("ascii")],
                token_file=root / "token", expires_at_ms=str(int(time.time() * 1000) + 60_000),
                output=root / "enrollment", access_output=root / "access",
            )
            with patch.object(voice.os, "name", "nt"):
                with self.assertRaisesRegex(voice.VoiceProvisionError, "POSIX private-file host"):
                    voice._console_enrollment(args)
            self.assertFalse(args.output.exists())
            self.assertFalse(args.access_output.exists())
    def test_pairing_owner_file_matches_uploaded_identity(self):
        cert, key, ca, host = b"test-cert", b"private-test-key", b"test-ca", b"gateway.test"
        bundle = voice._BVC1_HEADER.pack(b"BVC1", 1, 0, 1800000000, len(host), 8443,
                  bytes([192,168,1,2]), len(ca), len(cert), len(key), 0) + host + ca + cert + key
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "activation.json"
            args = SimpleNamespace(device_id="test-device", host="gateway.test", peer="192.168.1.2",
                     port=8443, console_port="COM8", activation_output=output)
            captured = []
            with patch.object(voice, "_bundle", return_value=bundle), patch.object(voice, "_run_console",
                    side_effect=lambda port, payload: captured.append(bytes(payload))):
                result = voice._pairing(args)
                self.assertEqual(result["status"], "identity-supplied")
                activation = json.loads(output.read_text())
                self.assertEqual(output.stat().st_mode & 0o777, 0o600)
                self.assertEqual(base64.b64decode(activation["possession_secret"]), captured[0][16:48])
                self.assertEqual(captured[0][48:], cert + key)
                self.assertNotIn(key.decode(), output.read_text())
                with self.assertRaises(voice.VoiceProvisionError): voice._pairing(args)
                self.assertEqual(len(captured), 1)
                original = output.read_bytes()
                args.resume = True
                with patch.object(voice.os, "urandom", side_effect=AssertionError("must reuse proof")):
                    voice._pairing(args)
                self.assertEqual(captured[1], captured[0])
                self.assertEqual(output.read_bytes(), original)
                args.device_id = "other-device"
                with self.assertRaises(voice.VoiceProvisionError): voice._pairing(args)
                args.device_id = "test-device"
                for invalid in [None, [], {**activation, "possession_secret": "bad!"},
                                {**activation, "possession_secret": ""},
                                {**activation, "gateway_ipv4": "192.168.1.3"},
                                {**activation, "extra": "unexpected"}]:
                    output.write_text(json.dumps(invalid))
                    with self.assertRaises(voice.VoiceProvisionError): voice._pairing(args)
                self.assertEqual(len(captured), 2)

    def test_helper_starts_command_before_secret_protocol(self) -> None:
        helper = voice._POWERSHELL
        self.assertIn("$serial.DiscardInBuffer()", helper)
        self.assertIn('$serial.Write("bkvoice provision`r")', helper)
        self.assertLess(
            helper.index('$serial.Write("bkvoice provision`r")'),
            helper.index("Wait-Exact 'BKVOICE PROVISION READY'"),
        )
        self.assertIn("$true", helper)
        self.assertIn("$false", helper)

    def test_wsl_path_conversion_is_used_for_helper_and_payload(self) -> None:
        commands: list[list[str]] = []
        original_run = voice.subprocess.run
        original_windows_path = voice._windows_path
        original_powershell = voice._powershell
        paths: dict[str, Path] = {}

        def fake_windows_path(path: Path) -> str:
            result = "C:\\tmp\\" + path.name
            paths[result] = path
            return result

        def fake_run(command: list[str], **kwargs: object) -> SimpleNamespace:
            commands.append(command)
            helper = paths[next(item for item in command if item.endswith(".ps1"))]
            self.assertTrue(helper.is_file())
            text = helper.read_text(encoding="utf-8")
            self.assertLess(text.index('$serial.Write("bkvoice provision`r")'),
                            text.index("Wait-Exact 'BKVOICE PROVISION READY'"))
            self.assertNotIn("0102", " ".join(command))
            return SimpleNamespace(returncode=0, stdout=b"", stderr=b"")

        voice._windows_path = fake_windows_path
        voice._powershell = lambda: "powershell.exe"
        voice.subprocess.run = fake_run
        try:
            voice._run_console("COM8", b"\x01\x02")
        finally:
            voice.subprocess.run = original_run
            voice._windows_path = original_windows_path
            voice._powershell = original_powershell

        self.assertEqual(len(commands), 1)
        self.assertIn("C:\\tmp\\provision.ps1", commands[0])
        self.assertIn("C:\\tmp\\payload.bin", commands[0])

    def test_wslpath_result_is_sanitized(self) -> None:
        original_run = voice.subprocess.run
        voice.subprocess.run = lambda *args, **kwargs: SimpleNamespace(
            returncode=0, stdout="C:\\Temp\\payload.bin\n", stderr=""
        )
        try:
            self.assertEqual(
                voice._windows_path(Path("/tmp/payload.bin")),
                "C:\\Temp\\payload.bin",
            )
        finally:
            voice.subprocess.run = original_run


if __name__ == "__main__":
    unittest.main()
