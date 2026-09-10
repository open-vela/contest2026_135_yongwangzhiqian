# SPDX-License-Identifier: Apache-2.0
"""Native mbedTLS interoperability regression for the BK7258 voice TLS provider."""

import asyncio
import os
from pathlib import Path
import shutil
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

try:
    import websockets
except ImportError:  # pragma: no cover - reported by setUpClass
    websockets = None

ROOT = Path(__file__).resolve().parents[3]
MBEDTLS = ROOT.parent / "apps/crypto/mbedtls/mbedtls"
BUILD = ROOT / "tests/host/bk7258/build/voice-tls-mbedtls"
PROBE = BUILD / "test_bk7258_voice_tls"
sys.path.insert(0, str(ROOT / "gateway/shaniu"))
from shaniu_gateway.server import (GatewayConfig, GatewayServer, build_tls_context,
                                   start_gateway_server)

MAGIC = 0x424B5631
VERSION = 1
HELLO = 1
WELCOME = 2
HEADER_SIZE = 40


def run(command, **kwargs):
    return subprocess.run(command, check=True, text=True, capture_output=True, **kwargs)


def make_cert(directory, name, subject, ca_cert=None, ca_key=None, *, server=False,
              expired=False):
    key = directory / f"{name}.key"
    csr = directory / f"{name}.csr"
    cert = directory / f"{name}.pem"
    run(["openssl", "genrsa", "-out", str(key), "2048"])
    run(["openssl", "req", "-new", "-key", str(key), "-out", str(csr),
         "-subj", subject])
    extension = directory / f"{name}.ext"
    extension.write_text(
        "basicConstraints=CA:FALSE\n"
        "keyUsage=digitalSignature,keyEncipherment\n"
        "extendedKeyUsage=" + ("serverAuth\nsubjectAltName=DNS:localhost\n" if server else "clientAuth\n"),
        encoding="ascii")
    if ca_cert:
        command = ["openssl", "x509", "-req", "-in", str(csr), "-CA", str(ca_cert),
                   "-CAkey", str(ca_key), "-CAcreateserial", "-out", str(cert),
                   "-sha256", "-extfile", str(extension)]
        if expired:
            # NotAfter is the issuance time.  The rejection test waits past
            # that boundary before attempting the handshake.
            command.extend(["-days", "0"])
        else:
            command.extend(["-days", "2"])
        run(command)
    else:
        run(["openssl", "req", "-x509", "-new", "-key", str(key), "-out", str(cert),
             "-days", "2", "-subj", subject,
             "-addext", "basicConstraints=critical,CA:TRUE"])
    os.chmod(key, 0o600)
    return cert, key


class Gateway:
    def __init__(self, cert, key, client_ca):
        self.cert = cert
        self.key = key
        self.client_ca = client_ca
        self.loop = asyncio.new_event_loop()
        self.ready = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.server = None
        self.port = None

    async def _handler(self, websocket):
        try:
            await websocket.recv()
        except websockets.ConnectionClosed:
            return
        welcome = struct.pack("!IBBHHHIIIIIQ", MAGIC, VERSION, WELCOME,
                              0, HEADER_SIZE, 0, 0, 7, 3, 0, 1, 0)
        await websocket.send(welcome)
        await websocket.wait_closed()

    def _run(self):
        asyncio.set_event_loop(self.loop)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.maximum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(self.cert, self.key)
        if self.client_ca is not None:
            context.load_verify_locations(self.client_ca)
            context.verify_mode = ssl.CERT_REQUIRED
        self.server = self.loop.run_until_complete(websockets.serve(
            self._handler, "127.0.0.1", 0, ssl=context,
            subprotocols=["companion-v1"], ping_interval=None))
        sock = self.server.sockets[0]
        self.port = sock.getsockname()[1]
        self.ready.set()
        self.loop.run_forever()
        self.server.close()
        self.loop.run_until_complete(self.server.wait_closed())
        self.loop.close()

    def start(self):
        self.thread.start()
        if not self.ready.wait(5):
            raise RuntimeError("loopback WSS gateway did not start")
        return self

    def close(self):
        if self.loop.is_running():
            self.loop.call_soon_threadsafe(self.loop.stop)
        self.thread.join(5)
        if self.thread.is_alive():
            raise RuntimeError("loopback WSS gateway did not stop")


class ProductGateway(Gateway):
    """The production gateway for the successful mTLS/WSS path."""

    def _run(self):
        asyncio.set_event_loop(self.loop)
        context = build_tls_context(self.cert, self.key, self.client_ca)
        gateway = GatewayServer(GatewayConfig(reply_frames=6, reply_interval_ms=0),
                                event_sink=lambda _event: None)
        self.server = self.loop.run_until_complete(start_gateway_server(
            gateway, "127.0.0.1", 0, context, self.client_ca))
        self.port = self.server.sockets[0].getsockname()[1]
        self.ready.set()
        self.loop.run_forever()
        self.server.close()
        self.loop.run_until_complete(self.server.wait_closed())
        self.loop.close()


class VoiceTlsInteropTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if websockets is None:
            raise RuntimeError("Python websockets is required")
        if shutil.which("openssl") is None:
            raise RuntimeError("openssl is required for ephemeral test credentials")
        run(["cmake", "-S", str(MBEDTLS), "-B", str(BUILD),
             "-DENABLE_TESTING=OFF", "-DENABLE_PROGRAMS=OFF",
             "-DCMAKE_C_FLAGS=-Wno-error=missing-prototypes"])
        run(["cmake", "--build", str(BUILD), "--parallel", "2"])
        sources = [
            ROOT / "tests/host/bk7258/test_bk7258_voice_tls.c",
            ROOT / "app/bk7258/bk7258_voice_tls.c",
            ROOT / "app/bk7258/bk7258_voice_wss.c",
            ROOT / "app/bk7258/bk7258_voice_transport.c",
            ROOT / "app/bk7258/bk7258_voice_companion.c",
        ]
        run(["cc", "-std=c11", "-D_GNU_SOURCE", "-DFAR=", "-Wall", "-Wextra",
             "-Werror", "-pthread", "-I", str(ROOT / "app/bk7258"),
             "-I", str(MBEDTLS / "include"), *map(str, sources), "-L",
             str(BUILD / "library"), "-lmbedtls", "-lmbedx509", "-lmbedcrypto",
             "-o", str(PROBE)])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bkvoice-tls-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        os.chmod(self.path, 0o700)
        self.ca, self.ca_key = make_cert(self.path, "ca", "/CN=bkvoice-test-ca")
        self.server_cert, self.server_key = make_cert(
            self.path, "server", "/CN=localhost", self.ca, self.ca_key, server=True)
        self.client_cert, self.client_key = make_cert(
            self.path, "client", "/CN=bkvoice-client", self.ca, self.ca_key)
        self.other_ca, self.other_ca_key = make_cert(self.path, "other-ca", "/CN=other-ca")
        self.untrusted_server, self.untrusted_server_key = make_cert(
            self.path, "untrusted-server", "/CN=localhost", self.other_ca,
            self.other_ca_key, server=True)
        self.expired_server, self.expired_server_key = make_cert(
            self.path, "expired-server", "/CN=localhost", self.ca, self.ca_key,
            server=True, expired=True)
        self.gateway = None

    def tearDown(self):
        if self.gateway:
            self.gateway.close()
        self.temp.cleanup()

    def start_gateway(self, cert=None, key=None, product=True):
        factory = ProductGateway if product else Gateway
        self.gateway = factory(cert or self.server_cert, key or self.server_key,
                               self.ca).start()

    def probe(self, mode, host="localhost", trusted=True):
        result = subprocess.run([
            str(PROBE), mode, host, str(self.gateway.port if self.gateway else 1),
            str(self.ca), str(self.client_cert), str(self.client_key),
            "1" if trusted else "0",
        ], text=True, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_mtls_wss_complete_turn_and_reconnect(self):
        self.start_gateway(product=True)
        self.probe("success")
        self.probe("reconnect")

    def test_verification_rejections(self):
        self.start_gateway()
        self.probe("reject-verify", host="wrong-host")
        self.gateway.close()
        self.gateway = None
        self.start_gateway(self.untrusted_server, self.untrusted_server_key)
        self.probe("reject-verify")
        self.gateway.close()
        self.gateway = None
        time.sleep(1)
        self.start_gateway(self.expired_server, self.expired_server_key)
        self.probe("reject-verify")

    def test_untrusted_time_rejected_before_connect(self):
        self.probe("reject-time", trusted=False)

    def test_cloud_server_auth_without_client_certificate(self):
        self.gateway = ProductGateway(self.server_cert, self.server_key, None).start()
        self.probe("cloud-success")
        self.probe("cloud-reconnect")
        self.probe("cloud-reject-verify", host="wrong-host")
        self.probe("cloud-reject-time", trusted=False)
        self.gateway.close()
        self.gateway = Gateway(self.untrusted_server, self.untrusted_server_key, None).start()
        self.probe("cloud-reject-verify")

    def test_deadline_concurrent_tx_and_interrupt(self):
        self.start_gateway()
        self.probe("deadline")
        self.probe("concurrent")
        self.probe("interrupt")

    def test_ephemeral_private_keys_are_private(self):
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o700)
        for key in self.path.glob("*.key"):
            self.assertEqual(key.stat().st_mode & 0o777, 0o600)


if __name__ == "__main__":
    unittest.main()
