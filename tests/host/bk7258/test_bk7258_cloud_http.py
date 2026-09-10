# SPDX-License-Identifier: Apache-2.0
"""Compile unmodified OpenVela webclient against deterministic TLS byte I/O."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import base64
import io
import json
import ssl
import os
import threading
import wave
from http.server import BaseHTTPRequestHandler, HTTPServer


ROOT = Path(__file__).resolve().parents[3]
APPS = ROOT.parent / 'apps'
BUILD = ROOT / 'tests/host/bk7258/build/voice-tls-mbedtls'
MBEDTLS = APPS / 'crypto/mbedtls/mbedtls'

class CloudHttpTest(unittest.TestCase):
    def test_real_webclient(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / 'nuttx').mkdir()
            (temp / 'netutils').mkdir()
            (temp / 'nuttx/config.h').write_text('''#include <nuttx/compiler.h>
#define OK 0
#define ERROR -1
#define CONFIG_WEBCLIENT_MAXHOSTNAME 128
#define CONFIG_WEBCLIENT_MAXFILENAME 256
''')
            (temp / 'nuttx/version.h').write_text('')
            (temp / 'debug.h').write_text('''#include <assert.h>
#include <string.h>
static inline size_t strlcpy(char *d, const char *s, size_t n)
{ size_t k = strlen(s); if (n) { size_t c=k<n-1?k:n-1; memcpy(d,s,c); d[c]=0; } return k; }
#define DEBUGASSERT assert
#define ninfo(...) ((void)0)
#define nerr(...) ((void)0)
#define nwarn(...) ((void)0)
''')
            (temp / 'netutils/netlib.h').write_text('''#include <stdint.h>
struct url_s { char *scheme; int schemelen; char *host; int hostlen;
uint16_t port; char *path; int pathlen; };
int netlib_parseurl(const char *, struct url_s *);
''')
            executable = temp / 'test'
            subprocess.run(['cmake', '-S', str(MBEDTLS), '-B', str(BUILD),
                            '-DENABLE_TESTING=OFF', '-DENABLE_PROGRAMS=OFF',
                            '-DCMAKE_C_FLAGS=-Wno-error=missing-prototypes'],
                           check=True, capture_output=True)
            subprocess.run(['cmake', '--build', str(BUILD), '--parallel', '2'],
                           check=True, capture_output=True)
            subprocess.run([
                'cc', '-std=gnu11', '-D_GNU_SOURCE', '-pthread', '-Wall', '-Wextra', '-Werror',
                '-Wno-unused-parameter', '-ffunction-sections', '-Wl,--gc-sections',
                '-I', str(temp), '-I', str(ROOT / 'tests/host/bk7258/mocks'),
                '-I', str(APPS / 'include'), '-I', str(ROOT / 'app/bk7258'),
                '-I', str(MBEDTLS / 'include'),
                '-I', str(APPS / 'netutils/cjson/cJSON'),
                str(ROOT / 'app/bk7258/bk7258_cloud_http.c'),
                str(ROOT / 'app/bk7258/bk7258_cloud_config.c'),
                str(ROOT / 'app/bk7258/bk7258_cloud_client.c'),
                str(ROOT / 'app/bk7258/bk7258_cloud_tts.c'),
                str(ROOT / 'app/bk7258/bk7258_cloud_request.c'),
                str(APPS / 'netutils/cjson/cJSON/cJSON.c'),
                str(ROOT / 'app/bk7258/bk7258_voice_tls.c'),
                str(ROOT / 'tests/host/bk7258/test_bk7258_cloud_http.c'),
                str(APPS / 'netutils/webclient/webclient.c'),
                str(APPS / 'netutils/netlib/netlib_parseurl.c'),
                '-L', str(BUILD / 'library'), '-lmbedtls', '-lmbedx509', '-lmbedcrypto', '-lm', '-o', str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)
            # Explicit operator opt-in only. Do not log the secret record.
            live_config = os.environ.get('BKCLOUD_LIVE_CONFIG')
            if live_config:
                ca_file = os.environ.get('BKCLOUD_LIVE_CA', '/etc/ssl/certs/ca-certificates.crt')
                roundtrip = os.environ.get('BKCLOUD_LIVE_ASR') == '1'
                generated = temp / 'generated-24k.pcm'
                args = [str(executable), '--live', ca_file]
                if roundtrip:
                    args.append(str(generated))
                subprocess.run(args,
                    input=Path(live_config).read_bytes(), check=True, timeout=135)
                if roundtrip:
                    # Fixed synthetic test phrase only. Host conversion is not
                    # evidence for the target Speex/DAC path.
                    converted = temp / 'generated-16k.pcm'
                    subprocess.run(['ffmpeg', '-nostdin', '-v', 'error',
                        '-f', 's16le', '-ar', '24000', '-ac', '1', '-i', str(generated),
                        '-ar', '16000', '-f', 's16le', str(converted)],
                        check=True, capture_output=True, timeout=15)
                    subprocess.run([str(executable), '--live-asr', ca_file, str(converted)],
                        input=Path(live_config).read_bytes(), check=True, timeout=70)
            ca, ca_key = temp / 'ca.pem', temp / 'ca.key'
            cert, key = temp / 'server.pem', temp / 'server.key'
            commands = [
                ['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                 '-keyout', str(ca_key), '-out', str(ca), '-days', '1',
                 '-subj', '/CN=cloud-test-ca',
                 '-addext', 'basicConstraints=critical,CA:TRUE'],
                ['openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes',
                 '-keyout', str(key), '-out', str(temp / 'server.csr'),
                 '-subj', '/CN=localhost'],
                ['openssl', 'x509', '-req', '-in', str(temp / 'server.csr'),
                 '-CA', str(ca), '-CAkey', str(ca_key), '-CAcreateserial',
                 '-out', str(cert), '-days', '1', '-extfile', str(temp / 'server.ext')],
            ]
            (temp / 'server.ext').write_text(
                'basicConstraints=CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\n'
                'extendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\n')
            for command in commands:
                subprocess.run(command, check=True, capture_output=True)
            ca_key.chmod(0o600)
            key.chmod(0o600)
            requests = []
            class Handler(BaseHTTPRequestHandler):
                def log_message(self, *args):
                    pass
                def do_POST(self):
                    requests.append((self.path, self.headers.get('Authorization'),
                                     self.rfile.read(int(self.headers['Content-Length']))))
                    request = json.loads(requests[-1][2])
                    if request['model'] == 'vendor/tts':
                        self.send_response(200)
                        self.send_header('Content-Type', 'audio/pcm')
                        self.send_header('Content-Length', '4')
                        self.end_headers()
                        for sample_byte in range(4):
                            self.wfile.write(bytes([sample_byte])); self.wfile.flush()
                        return
                    if request['model'] == 'mimo-v2.5-tts':
                        events = [
                            {'choices': [{'index': 0, 'delta': {'audio': {'data': 'AAECAw=='}},
                                          'finish_reason': None}]},
                            {'choices': [{'index': 0, 'delta': {}, 'finish_reason': 'stop'}]}]
                        payload = ''.join('data: ' + json.dumps(e) + '\n\n' for e in events)
                        payload += 'data: [DONE]\n\n'
                        self.send_response(200)
                        self.send_header('Content-Type', 'text/event-stream')
                        self.send_header('Content-Length', str(len(payload)))
                        self.end_headers()
                        for start in range(0, len(payload), 7):
                            self.wfile.write(payload[start:start+7].encode())
                            self.wfile.flush()
                        return
                    body = json.dumps({'choices': [{'index': 0, 'finish_reason': 'stop',
                        'message': {'content': 'hello'}}]}).encode()
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/json')
                    self.send_header('Content-Length', str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
            server = HTTPServer(('127.0.0.1', 0), Handler)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.maximum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(cert, key)
            server.socket = context.wrap_socket(server.socket, server_side=True)
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                subprocess.run([str(executable), str(ca), str(server.server_port)],
                               check=True, timeout=15)
            finally:
                server.shutdown()
                worker.join(timeout=5)
                server.server_close()
            self.assertEqual(len(requests), 5)
            self.assertEqual(requests[4][0], '/v1/audio/speech')
            self.assertEqual(json.loads(requests[4][2]), {
                'model': 'vendor/tts', 'input': 'hello', 'voice': 'alloy', 'response_format': 'pcm'})
            speech = json.loads(requests[3][2])
            self.assertTrue(speech['stream'])
            self.assertEqual(speech['audio'], {'format': 'pcm16', 'voice': 'mimo_default'})
            self.assertEqual(speech['messages'], [{'role': 'assistant', 'content': 'hello'}])
            first = json.loads(requests[1][2])
            second = json.loads(requests[2][2])
            self.assertEqual(first['model'], 'vendor/chat')
            self.assertNotIn('thinking', first)
            self.assertEqual(first['max_tokens'], 1024)
            self.assertEqual(first['messages'], [
                {'role': 'system', 'content': 'persona \"line\"\n'},
                {'role': 'user', 'content': 'question 1'}])
            self.assertEqual(second['thinking'], {'type': 'disabled'})
            self.assertEqual(second['max_completion_tokens'], 1024)
            self.assertEqual(second['messages'], [
                {'role': 'system', 'content': 'persona \"line\"\n'},
                {'role': 'user', 'content': 'question 1'},
                {'role': 'assistant', 'content': 'hello'},
                {'role': 'user', 'content': 'question 2'}])
            path, auth, payload = requests[0]
            self.assertEqual(path, '/v1/chat/completions')
            self.assertEqual(auth, 'Bearer test-only-key')
            body = json.loads(payload)
            self.assertEqual(body['model'], 'vendor/asr')
            data = body['messages'][0]['content'][0]['input_audio']['data']
            raw = base64.b64decode(data.split(',', 1)[1], validate=True)
            with wave.open(io.BytesIO(raw), 'rb') as audio:
                self.assertEqual(audio.getframerate(), 16000)
                self.assertEqual(audio.readframes(2), bytes([0, 1, 2, 3]))


if __name__ == '__main__':
    unittest.main()
