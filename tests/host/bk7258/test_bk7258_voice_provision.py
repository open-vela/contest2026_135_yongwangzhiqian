#!/usr/bin/env python3
"""Offline checks for the private BKVoice provisioner helper."""

from __future__ import annotations

import sys
import json
import base64
import hashlib
import subprocess
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
    def test_runtime_identity_bind_busy_retries_only_after_persistence(self):
        source = (ROOT / "app/bk7258/bk7258_voice_runtime.c").read_text()
        start = source.index("static void bkvoice_identity_progress")
        end = source.index("\nstatic void bkvoice_configuration_progress", start)
        function = source[start:end]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define BKVOICE_CONFIG_MAX_BYTES 4096u
struct bkprov_identity_s { unsigned char *record; size_t size; };
struct bkvoice_runtime_s {
  struct bkprov_identity_s identity;
  bool identity_pending, identity_bind_pending, identity_bound;
  int identity_result, last_error;
  uint64_t provision_restore_ms;
  void *upload;
};
static uint64_t clock_ms;
static int install_result, storage_result, bind_results[4], bind_count;
static int install_calls, storage_calls, bind_calls;
static unsigned char stable_record[4096];
static uint64_t bkvoice_now(void) { return clock_ms; }
static void mbedtls_platform_zeroize(void *p, size_t n) { memset(p, 0, n); }
static int bkprov_storage_refresh(void) { return 0; }
static int bkprov_storage_identity_install(const void *record, size_t size)
{ (void)record; (void)size; install_calls++; return install_result; }
static int bkprov_storage_identity(void *record, size_t max, size_t *size)
{ (void)record; (void)max; storage_calls++; if (storage_result == 0) *size = 4; return storage_result; }
static int bkprov_identity_load(struct bkprov_identity_s *identity,
                                const void *record, size_t size)
{ memcpy(stable_record, record, size); identity->record = stable_record;
  identity->size = size; return 0; }
static int bkvoice_identity_bind(struct bkvoice_runtime_s *runtime)
{ int ret = bind_results[bind_calls < bind_count ? bind_calls : bind_count - 1];
  bind_calls++; if (ret == 0) runtime->identity_bound = true; return ret; }
'''
        harness += function + r'''
static void reset(struct bkvoice_runtime_s *r)
{
  memset(r, 0, sizeof(*r)); clock_ms = 1000; install_result = 0;
  storage_result = -ENOENT; install_calls = storage_calls = bind_calls = 0;
  bind_count = 0; memset(bind_results, 0, sizeof(bind_results));
}
int main(void)
{
  struct bkvoice_runtime_s r; unsigned char record[4] = {1, 2, 3, 4};
  reset(&r); storage_result = 0; bind_results[0] = -EBUSY;
  bind_results[1] = 0; bind_count = 2;
  bkvoice_identity_progress(&r); assert(r.identity_bind_pending && bind_calls == 1);
  assert(storage_calls == 1 && install_calls == 0);
  bkvoice_identity_progress(&r);
  assert(bind_calls == 1 && storage_calls == 1 && install_calls == 0);
  clock_ms += 1000; bkvoice_identity_progress(&r);
  assert(r.identity_bound && !r.identity_bind_pending && bind_calls == 2);
  bkvoice_identity_progress(&r);
  assert(bind_calls == 2 && storage_calls == 1 && install_calls == 0);

  reset(&r); r.identity.record = record; r.identity.size = sizeof(record);
  r.identity_pending = true; install_result = -EIO; bind_count = 1;
  bkvoice_identity_progress(&r);
  assert(!r.identity_pending && !r.identity_bind_pending && bind_calls == 0);

  reset(&r); storage_result = 0; bind_results[0] = -EIO; bind_count = 1;
  bkvoice_identity_progress(&r); assert(bind_calls == 1 && !r.identity_bind_pending);
  clock_ms += 1000; bkvoice_identity_progress(&r); assert(bind_calls == 1);

  reset(&r); r.identity.record = record; r.identity.size = sizeof(record);
  r.identity_pending = true; install_result = 0; bind_results[0] = -EBUSY;
  bind_results[1] = 0; bind_count = 2; bkvoice_identity_progress(&r);
  assert(!r.identity_pending && r.identity_bind_pending && bind_calls == 1);
  assert(install_calls == 1);
  bkvoice_identity_progress(&r);
  assert(bind_calls == 1 && install_calls == 1);
  clock_ms += 1000; bkvoice_identity_progress(&r);
  assert(r.identity_bound && bind_calls == 2);
  bkvoice_identity_progress(&r);
  assert(bind_calls == 2 && install_calls == 1);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            c_file, binary = root / "identity_progress.c", root / "identity_progress"
            c_file.write_text(harness, encoding="utf-8")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(c_file), "-o", str(binary)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            subprocess.run([str(binary)], check=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)

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
                     port=8443, console_port="COM8", activation_output=output,
                     server_ca=Path("ca"), client_cert=Path("cert"), client_key=Path("key"),
                     openssl=Path("openssl"), direct_cloud=False)
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

    def test_direct_cloud_writes_four_field_bootstrap_and_resume_is_immutable(self):
        cert, key = b"test-cert", b"private-test-key"
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "bootstrap.json"
            args = SimpleNamespace(device_id="test-device", console_port="COM8",
                                   activation_output=output, direct_cloud=True,
                                   client_cert=Path("cert"), client_key=Path("key"),
                                   openssl=Path("openssl"))
            captured = []
            with patch.object(voice, "_identity_parts", return_value=(cert, key)), \
                 patch.object(voice, "_run_console", side_effect=lambda port, payload: captured.append(bytes(payload))):
                voice._pairing(args)
                saved = output.read_bytes()
                document = json.loads(saved)
                self.assertEqual(set(document), {"protocol", "device_id",
                                                  "certificate_sha256", "possession_secret"})
                self.assertEqual(document["protocol"], "provision-bootstrap-v1")
                self.assertEqual(document["certificate_sha256"], hashlib.sha256(cert).hexdigest())
                self.assertEqual(base64.b64decode(document["possession_secret"]), captured[0][16:48])
                args.resume = True
                with patch.object(voice.os, "urandom", side_effect=AssertionError("must reuse proof")):
                    voice._pairing(args)
                self.assertEqual(output.read_bytes(), saved)
                self.assertEqual(captured[1], captured[0])
                args.direct_cloud = False
                args.host = "gateway.test"; args.peer = "192.168.1.2"; args.server_ca = Path("ca")
                with self.assertRaises(voice.VoiceProvisionError): voice._pairing(args)
                args.direct_cloud = True; args.host = "gateway.test"
                with self.assertRaises(voice.VoiceProvisionError): voice._pairing(args)
                invalid_output = Path(directory) / "invalid.json"
                invalid = SimpleNamespace(device_id="test-device", console_port="ttyUSB0",
                                          activation_output=invalid_output, direct_cloud=True,
                                          client_cert=Path("cert"), client_key=Path("key"),
                                          openssl=Path("openssl"))
                with patch.object(voice, "_identity_parts", side_effect=AssertionError("must reject first")):
                    with self.assertRaises(voice.VoiceProvisionError): voice._pairing(invalid)
                self.assertFalse(invalid_output.exists())

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
