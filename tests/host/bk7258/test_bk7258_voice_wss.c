/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_wss.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RX_BYTES 4096u
#define TX_BYTES 4096u

struct fake_tls_s
{
  uint8_t rx[RX_BYTES];
  size_t rx_size;
  size_t rx_offset;
  uint8_t tx[TX_BYTES];
  size_t tx_size;
  size_t io_chunk;
  int open_calls;
  int close_calls;
  int close_failures;
  int interrupt_calls;
  int open_error;
  int io_error;
  int random_error;
  uint64_t open_deadline;
  uint64_t io_deadline;
};

static int fake_open(void *context, const char *host, uint16_t port,
                     uint64_t deadline)
{
  struct fake_tls_s *fake = context;
  assert(strcmp(host, "localhost") == 0);
  assert(port == 443);
  fake->open_calls++;
  fake->open_deadline = deadline;
  return fake->open_error;
}

static ssize_t fake_send(void *context, const uint8_t *buffer, size_t bytes,
                         uint64_t deadline)
{
  struct fake_tls_s *fake = context;
  size_t count = bytes < fake->io_chunk ? bytes : fake->io_chunk;
  fake->io_deadline = deadline;
  if (fake->io_error < 0)
    return fake->io_error;
  assert(fake->tx_size + count <= sizeof(fake->tx));
  memcpy(fake->tx + fake->tx_size, buffer, count);
  fake->tx_size += count;
  return (ssize_t)count;
}

static ssize_t fake_recv(void *context, uint8_t *buffer, size_t bytes,
                         uint64_t deadline)
{
  struct fake_tls_s *fake = context;
  size_t available = fake->rx_size - fake->rx_offset;
  size_t count;
  fake->io_deadline = deadline;
  if (fake->io_error < 0)
    return fake->io_error;
  if (available == 0)
    return -ETIMEDOUT;
  count = bytes < fake->io_chunk ? bytes : fake->io_chunk;
  if (count > available)
    count = available;
  memcpy(buffer, fake->rx + fake->rx_offset, count);
  fake->rx_offset += count;
  return (ssize_t)count;
}

static int fake_interrupt(void *context)
{
  ((struct fake_tls_s *)context)->interrupt_calls++;
  return 0;
}

static int fake_close(void *context)
{
  struct fake_tls_s *fake = context;
  fake->close_calls++;
  if (fake->close_failures > 0)
    {
      fake->close_failures--;
      return -EIO;
    }
  return 0;
}

static int fake_random(void *context, uint8_t *buffer, size_t bytes)
{
  static const uint8_t nonce[16] =
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  static const uint8_t mask[4] = {1, 2, 3, 4};
  struct fake_tls_s *fake = context;
  if (fake->random_error < 0)
    return fake->random_error;
  if (bytes == sizeof(nonce))
    memcpy(buffer, nonce, sizeof(nonce));
  else if (bytes == sizeof(mask))
    memcpy(buffer, mask, sizeof(mask));
  else
    return -EINVAL;
  return 0;
}

static int fake_sha1(void *context, const uint8_t *buffer, size_t bytes,
                     uint8_t digest[20])
{
  static const uint8_t expected[20] =
    {0x07, 0x3d, 0xea, 0x25, 0x84, 0xc6, 0x74, 0xe7, 0xbc, 0x81,
     0x44, 0xa9, 0x2e, 0x8b, 0x04, 0x76, 0x22, 0xca, 0x0e, 0xb9};
  (void)context;
  assert(bytes == 60);
  assert(memcmp(buffer, "AAECAwQFBgcICQoLDA0ODw=="
                       "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 60) == 0);
  memcpy(digest, expected, sizeof(expected));
  return 0;
}

static const struct bkvoice_wss_tls_ops_s *fake_ops(void)
{
  static const struct bkvoice_wss_tls_ops_s ops =
    {.open_verified = fake_open, .send = fake_send, .recv = fake_recv,
     .interrupt = fake_interrupt, .close = fake_close, .random = fake_random,
     .sha1 = fake_sha1};
  return &ops;
}

static void push_rx(struct fake_tls_s *fake, const void *data, size_t bytes)
{
  assert(fake->rx_size + bytes <= sizeof(fake->rx));
  memcpy(fake->rx + fake->rx_size, data, bytes);
  fake->rx_size += bytes;
}

static size_t frame(uint8_t *out, bool final, uint8_t opcode,
                    const uint8_t *payload, size_t bytes)
{
  out[0] = (final ? 0x80 : 0) | opcode;
  out[1] = (uint8_t)bytes;
  if (bytes != 0)
    memcpy(out + 2, payload, bytes);
  return bytes + 2;
}

static size_t companion(uint8_t *out)
{
  struct bkvoice_companion_header_s header;
  size_t encoded;
  memset(&header, 0, sizeof(header));
  header.magic = BKVOICE_COMPANION_MAGIC;
  header.version = BKVOICE_COMPANION_VERSION;
  header.type = BKVOICE_COMPANION_WELCOME;
  header.header_len = BKVOICE_COMPANION_HEADER_BYTES;
  header.boot_generation = 1;
  header.session_id = 1;
  header.sequence = 1;
  assert(bkvoice_companion_encode(&header, NULL, out,
                                  BKVOICE_WSS_MAX_MESSAGE, &encoded) == 0);
  return encoded;
}

static void setup(struct bkvoice_wss_s *wss, struct fake_tls_s *fake,
                  const char *path, const char *subprotocol)
{
  const struct bkvoice_wss_config_s config =
    {.host = "localhost", .port = 443, .path = path,
     .subprotocol = subprotocol};
  memset(fake, 0, sizeof(*fake));
  fake->io_chunk = 3;
  assert(bkvoice_wss_initialize(wss, fake_ops(), fake, &config) == 0);
}

static void handshake_response(struct fake_tls_s *fake, const char *accept,
                               const char *extra)
{
  char response[1024];
  int length = snprintf(response, sizeof(response),
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
    "Sec-WebSocket-Accept: %s\r\nSec-WebSocket-Protocol: companion-v1\r\n"
    "%s\r\n", accept, extra == NULL ? "" : extra);
  assert(length > 0);
  push_rx(fake, response, (size_t)length);
}

static void test_handshake_and_masked_send(void)
{
  struct bkvoice_wss_s wss;
  struct bkvoice_transport_s transport;
  struct fake_tls_s fake;
  uint8_t frame_wire[128];
  uint8_t payload[64];
  size_t payload_size;
  setup(&wss, &fake, "/companion/v1", "companion-v1");
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
  assert(bkvoice_transport_open(&transport, 77) == 0);
  assert(fake.open_deadline == 77 && fake.io_deadline == 77);
  assert(strstr((char *)fake.tx, "GET /companion/v1 HTTP/1.1\r\n") != NULL);
  assert(strstr((char *)fake.tx, "Sec-WebSocket-Key: AAECAwQFBgcICQoLDA0ODw==\r\n") != NULL);
  payload_size = companion(payload);
  fake.tx_size = 0;
  assert(bkvoice_transport_send_all(&transport, payload, payload_size, 88) == 0);
  assert((fake.tx[0] & 0x8f) == 0x82 && (fake.tx[1] & 0x80) != 0);
  memcpy(frame_wire, fake.tx, fake.tx_size);
  assert(fake.tx_size == payload_size + 6);
  assert(frame_wire[2] == 1 && frame_wire[3] == 2 && frame_wire[4] == 3 && frame_wire[5] == 4);
  assert(frame_wire[6] == (payload[0] ^ 1));
  assert(bkvoice_transport_close(&transport) == 0);
  assert(bkvoice_wss_uninitialize(&wss) == 0);
}

static void test_fragment_ping_and_failures(void)
{
  struct bkvoice_wss_s wss;
  struct bkvoice_transport_s transport;
  struct fake_tls_s fake;
  uint8_t payload[64];
  uint8_t wire[128];
  size_t size;
  setup(&wss, &fake, "/companion/v1", "companion-v1");
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
  assert(bkvoice_transport_open(&transport, 1) == 0);
  fake.tx_size = 0;
  size = companion(payload);
  size = frame(wire, false, 2, payload, 15);
  push_rx(&fake, wire, size);
  size = frame(wire, true, 9, (const uint8_t *)"x", 1);
  push_rx(&fake, wire, size);
  size = frame(wire, true, 0, payload + 15, companion(payload) - 15);
  push_rx(&fake, wire, size);
  assert(bkvoice_transport_recv_exact(&transport, payload, companion(payload),
                                      2) == 0);
  assert(fake.tx_size >= 7 && fake.tx[0] == 0x8a && fake.tx[1] == 0x81);
  assert(fake.tx[6] == ('x' ^ 1));
  assert(bkvoice_transport_close(&transport) == 0);
  assert(bkvoice_wss_uninitialize(&wss) == 0);
}

static void test_invalid_handshake_config_interrupt_close(void)
{
  struct bkvoice_wss_s wss;
  struct bkvoice_transport_s transport;
  struct fake_tls_s fake;
  const struct bkvoice_wss_config_s bad_path =
    {.host = "localhost", .port = 443, .path = "/bad\r\n", .subprotocol = "companion-v1"};
  const struct bkvoice_wss_config_s bad_token =
    {.host = "localhost", .port = 443, .path = "/companion/v1", .subprotocol = "companion v1"};
  uint8_t payload[64];
  setup(&wss, &fake, "/companion/v1", "companion-v1");
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  handshake_response(&fake, "invalid", NULL);
  assert(bkvoice_transport_open(&transport, 3) == -EACCES);
  assert(bkvoice_wss_uninitialize(&wss) == 0);
  assert(bkvoice_wss_initialize(&wss, fake_ops(), &fake, &bad_path) == -EINVAL);
  assert(bkvoice_wss_initialize(&wss, fake_ops(), &fake, &bad_token) == -EINVAL);
  setup(&wss, &fake, "/companion/v1", "companion-v1");
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
  assert(bkvoice_transport_open(&transport, 4) == 0);
  assert(bkvoice_transport_interrupt(&transport) == 0);
  assert(fake.interrupt_calls == 1);
  assert(bkvoice_transport_send_all(&transport, payload, companion(payload),
                                    5) == -ECANCELED);
  fake.close_failures = 1;
  assert(bkvoice_transport_close(&transport) == -EIO);
  assert(bkvoice_transport_close(&transport) == 0);
  assert(bkvoice_wss_uninitialize(&wss) == 0);
}

static void test_wire_rejections(void)
{
  struct bkvoice_wss_s wss;
  struct bkvoice_transport_s transport;
  struct fake_tls_s fake;
  uint8_t wire[800];
  uint8_t payload[64];
  size_t size;
  int mode;

  for (mode = 0; mode < 3; mode++)
    {
      setup(&wss, &fake, "/companion/v1", "companion-v1");
      handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
      assert(bkvoice_transport_initialize(&transport,
                                          bkvoice_wss_transport_ops(), &wss) == 0);
      assert(bkvoice_transport_open(&transport, 9) == 0);
      if (mode == 0)
        {
          size = frame(wire, true, 1, (const uint8_t *)"x", 1);
        }
      else if (mode == 1)
        {
          size = frame(wire, true, 2, payload, 0);
          wire[1] |= 0x80;
        }
      else
        {
          wire[0] = 0x82;
          wire[1] = 126;
          wire[2] = 0x02;
          wire[3] = 0xc0;
          memset(wire + 4, 0, 0x2c0);
          size = 4;
        }
      push_rx(&fake, wire, size);
      assert(bkvoice_transport_recv_exact(&transport, payload,
                                          companion(payload), 10) < 0);
      assert(mode == 2 ? wss.last_error == -EMSGSIZE :
                         wss.last_error == -EPROTO);
      assert(bkvoice_transport_close(&transport) == 0);
      assert(bkvoice_wss_uninitialize(&wss) == 0);
    }

  setup(&wss, &fake, "/companion/v1", "companion-v1");
  handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  assert(bkvoice_transport_open(&transport, 11) == 0);
  size = companion(payload);
  wire[0] = 0x82;
  wire[1] = (uint8_t)size;
  memcpy(wire + 2, payload, size);
  wire[2 + 10] ^= 1;
  push_rx(&fake, wire, size + 2);
  assert(bkvoice_transport_recv_exact(&transport, payload, size, 12) < 0);
  assert(wss.last_error == -EPROTO || wss.last_error == -EMSGSIZE);
  assert(bkvoice_transport_close(&transport) == 0);
  assert(bkvoice_wss_uninitialize(&wss) == 0);
}

static void test_close_validation_and_echo_failure(void)
{
  struct bkvoice_wss_s wss;
  struct bkvoice_transport_s transport;
  struct fake_tls_s fake;
  uint8_t payload[64];
  uint8_t wire[32];
  const uint8_t valid_close[] = {0x03, 0xe8, 0xc2, 0xa2};
  const uint8_t forbidden_close[] = {0x03, 0xed};
  const uint8_t invalid_utf8[] = {0x03, 0xe8, 0xc0, 0x80};
  size_t size;
  int mode;

  setup(&wss, &fake, "/companion/v1", "companion-v1");
  handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  assert(bkvoice_transport_open(&transport, 20) == 0);
  fake.tx_size = 0;
  size = frame(wire, true, 8, valid_close, sizeof(valid_close));
  push_rx(&fake, wire, size);
  assert(bkvoice_transport_recv_exact(&transport, payload,
                                      companion(payload), 21) ==
         -ECONNRESET);
  assert(wss.peer_closed && !wss.faulted);
  assert(fake.tx_size == sizeof(valid_close) + 6u);
  assert(fake.tx[0] == 0x88 && fake.tx[1] == 0x84);
  assert(bkvoice_transport_close(&transport) == 0);
  assert(bkvoice_wss_uninitialize(&wss) == 0);

  setup(&wss, &fake, "/companion/v1", "companion-v1");
  handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  assert(bkvoice_transport_open(&transport, 22) == 0);
  size = frame(wire, true, 8, valid_close, sizeof(valid_close));
  push_rx(&fake, wire, size);
  fake.random_error = -EIO;
  assert(bkvoice_transport_recv_exact(&transport, payload,
                                      companion(payload), 23) == -EIO);
  assert(wss.faulted && !wss.peer_closed);
  assert(bkvoice_transport_close(&transport) == 0);
  assert(bkvoice_wss_uninitialize(&wss) == 0);

  for (mode = 0; mode < 2; mode++)
    {
      const uint8_t *close_payload = mode == 0 ? forbidden_close :
                                               invalid_utf8;
      size_t close_size = mode == 0 ? sizeof(forbidden_close) :
                                      sizeof(invalid_utf8);

      setup(&wss, &fake, "/companion/v1", "companion-v1");
      handshake_response(&fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=", NULL);
      assert(bkvoice_transport_initialize(&transport,
                                          bkvoice_wss_transport_ops(),
                                          &wss) == 0);
      assert(bkvoice_transport_open(&transport, 24) == 0);
      size = frame(wire, true, 8, close_payload, close_size);
      push_rx(&fake, wire, size);
      assert(bkvoice_transport_recv_exact(&transport, payload,
                                          companion(payload), 25) ==
             -EPROTO);
      assert(wss.faulted && !wss.peer_closed);
      assert(bkvoice_transport_close(&transport) == 0);
      assert(bkvoice_wss_uninitialize(&wss) == 0);
    }
}

static void test_duplicate_handshake_header(void)
{
  struct bkvoice_wss_s wss;
  struct bkvoice_transport_s transport;
  struct fake_tls_s fake;

  setup(&wss, &fake, "/companion/v1", "companion-v1");
  assert(bkvoice_transport_initialize(&transport,
                                      bkvoice_wss_transport_ops(), &wss) == 0);
  handshake_response(
    &fake, "Bz3qJYTGdOe8gUSpLosEdiLKDrk=",
    "Sec-WebSocket-Accept: Bz3qJYTGdOe8gUSpLosEdiLKDrk=\r\n");
  assert(bkvoice_transport_open(&transport, 30) == -EACCES);
  assert(fake.close_calls == 1);
  assert(bkvoice_wss_uninitialize(&wss) == 0);
}

int main(void)
{
  test_handshake_and_masked_send();
  test_fragment_ping_and_failures();
  test_invalid_handshake_config_interrupt_close();
  test_wire_rejections();
  test_close_validation_and_echo_failure();
  test_duplicate_handshake_header();
  puts("BK7258 VOICE WSS HOST TEST PASS");
  return 0;
}
