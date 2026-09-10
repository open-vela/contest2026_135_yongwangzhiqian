/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_gateway.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_BUFFER_BYTES 16384u

struct fake_transport_s
{
  uint8_t tx[TEST_BUFFER_BYTES];
  uint8_t rx[TEST_BUFFER_BYTES];
  size_t tx_size;
  size_t rx_size;
  size_t rx_offset;
  size_t send_chunk;
  size_t recv_chunk;
  size_t send_fail_after;
  int send_error;
  int empty_recv_error;
  int open_error;
  int interrupt_error;
  int close_failures;
  int close_error;
  int open_calls;
  int send_calls;
  int recv_calls;
  int interrupt_calls;
  int close_calls;
  uint64_t open_deadline;
  uint64_t send_deadline;
  uint64_t recv_deadline;
  bool send_zero_once;
  bool opened;
};

struct fake_audio_s
{
  int mic_acquire_calls;
  int mic_prepare_calls;
  int mic_start_calls;
  int mic_stop_calls;
  int mic_drain_calls;
  int mic_release_calls;
  int dac_acquire_calls;
  int dac_prepare_calls;
  int dac_start_calls;
  int dac_write_calls;
  int dac_drain_calls;
  int dac_stop_calls;
  int dac_release_calls;
  size_t dac_bytes;
  ssize_t dac_write_result;
  bool dac_write_result_set;
};

struct fake_owner_s
{
  struct bkvoice_turn_s *turn;
  int tts_start_calls;
  int tts_audio_calls;
  int tts_end_calls;
  int terminal_calls;
  int terminal_reason;
  uint32_t terminal_session_id;
  uint32_t terminal_turn_id;
  int volume_error;
  unsigned int volume;
  unsigned int requested_volume;
  int ota_calls;
  int ota_error;
  uint32_t ota_request_sequence;
  uint8_t ota_digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES];
};

struct fake_clock_s
{
  uint64_t now_ms;
};

struct fixture_s
{
  struct fake_transport_s transport;
  struct fake_audio_s audio;
  struct fake_owner_s owner;
  struct fake_clock_s clock;
  struct bkvoice_turn_s turn;
  struct bkvoice_gateway_s gateway;
  uint32_t session_id;
  uint32_t server_sequence;
};

static size_t test_min_size(size_t left, size_t right)
{
  return left < right ? left : right;
}

static uint32_t test_get_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

static uint16_t test_get_be16(const uint8_t *data)
{
  return ((uint16_t)data[0] << 8) | data[1];
}

static void test_put_be32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static int fake_transport_open(void *context, uint64_t deadline_ms)
{
  struct fake_transport_s *transport = context;

  transport->open_calls++;
  transport->open_deadline = deadline_ms;
  if (transport->open_error < 0)
    {
      return transport->open_error;
    }

  transport->opened = true;
  return 0;
}

static ssize_t fake_transport_send(void *context, const uint8_t *buffer,
                                   size_t bytes, uint64_t deadline_ms)
{
  struct fake_transport_s *transport = context;
  size_t chunk;

  transport->send_calls++;
  transport->send_deadline = deadline_ms;
  if (!transport->opened)
    {
      return -ENOTCONN;
    }

  if (transport->send_zero_once)
    {
      transport->send_zero_once = false;
      return 0;
    }

  if (transport->send_error < 0 &&
      transport->tx_size >= transport->send_fail_after)
    {
      return transport->send_error;
    }

  chunk = test_min_size(bytes, transport->send_chunk);
  if (transport->send_error < 0 &&
      transport->tx_size + chunk > transport->send_fail_after)
    {
      chunk = transport->send_fail_after - transport->tx_size;
    }

  if (chunk == 0)
    {
      return transport->send_error < 0 ? transport->send_error : -EIO;
    }

  if (transport->tx_size + chunk > sizeof(transport->tx))
    {
      return -ENOSPC;
    }

  memcpy(transport->tx + transport->tx_size, buffer, chunk);
  transport->tx_size += chunk;
  return (ssize_t)chunk;
}

static ssize_t fake_transport_recv(void *context, uint8_t *buffer,
                                   size_t bytes, uint64_t deadline_ms)
{
  struct fake_transport_s *transport = context;
  size_t available;
  size_t chunk;

  transport->recv_calls++;
  transport->recv_deadline = deadline_ms;
  if (!transport->opened)
    {
      return -ENOTCONN;
    }

  available = transport->rx_size - transport->rx_offset;
  if (available == 0)
    {
      return transport->empty_recv_error;
    }

  chunk = test_min_size(bytes, transport->recv_chunk);
  chunk = test_min_size(chunk, available);
  memcpy(buffer, transport->rx + transport->rx_offset, chunk);
  transport->rx_offset += chunk;
  return (ssize_t)chunk;
}

static int fake_transport_interrupt(void *context)
{
  struct fake_transport_s *transport = context;

  transport->interrupt_calls++;
  return transport->interrupt_error;
}

static int fake_transport_close(void *context)
{
  struct fake_transport_s *transport = context;

  transport->close_calls++;
  if (transport->close_failures > 0)
    {
      transport->close_failures--;
      return transport->close_error;
    }

  transport->opened = false;
  return 0;
}

static const struct bkvoice_transport_ops_s g_transport_ops =
{
  .open = fake_transport_open,
  .send = fake_transport_send,
  .recv = fake_transport_recv,
  .interrupt = fake_transport_interrupt,
  .close = fake_transport_close,
};

static int fake_mic_acquire(void *context)
{
  ((struct fake_audio_s *)context)->mic_acquire_calls++;
  return 0;
}

static int fake_mic_prepare(void *context)
{
  ((struct fake_audio_s *)context)->mic_prepare_calls++;
  return 0;
}

static int fake_mic_start(void *context)
{
  ((struct fake_audio_s *)context)->mic_start_calls++;
  return 0;
}

static int fake_mic_stop(void *context)
{
  ((struct fake_audio_s *)context)->mic_stop_calls++;
  return 0;
}

static int fake_mic_drain(void *context)
{
  ((struct fake_audio_s *)context)->mic_drain_calls++;
  return 0;
}

static int fake_mic_release(void *context)
{
  ((struct fake_audio_s *)context)->mic_release_calls++;
  return 0;
}

static int fake_dac_acquire(void *context)
{
  ((struct fake_audio_s *)context)->dac_acquire_calls++;
  return 0;
}

static int fake_dac_prepare(void *context)
{
  ((struct fake_audio_s *)context)->dac_prepare_calls++;
  return 0;
}

static int fake_dac_start(void *context)
{
  ((struct fake_audio_s *)context)->dac_start_calls++;
  return 0;
}

static ssize_t fake_dac_write(void *context, const uint8_t *pcm,
                              size_t bytes)
{
  struct fake_audio_s *audio = context;
  ssize_t written;

  assert(pcm != NULL);
  audio->dac_write_calls++;
  written = audio->dac_write_result_set ? audio->dac_write_result :
            (ssize_t)bytes;
  if (written > 0)
    {
      audio->dac_bytes += (size_t)written;
    }

  return written;
}

static int fake_dac_drain(void *context)
{
  ((struct fake_audio_s *)context)->dac_drain_calls++;
  return 0;
}

static int fake_dac_stop(void *context)
{
  ((struct fake_audio_s *)context)->dac_stop_calls++;
  return 0;
}

static int fake_dac_release(void *context)
{
  ((struct fake_audio_s *)context)->dac_release_calls++;
  return 0;
}

static const struct bkvoice_turn_audio_ops_s g_audio_ops =
{
  .mic_acquire = fake_mic_acquire,
  .mic_prepare = fake_mic_prepare,
  .mic_start = fake_mic_start,
  .mic_stop = fake_mic_stop,
  .mic_drain = fake_mic_drain,
  .mic_release = fake_mic_release,
  .dac_acquire = fake_dac_acquire,
  .dac_prepare = fake_dac_prepare,
  .dac_start = fake_dac_start,
  .dac_write = fake_dac_write,
  .dac_drain = fake_dac_drain,
  .dac_stop = fake_dac_stop,
  .dac_release = fake_dac_release,
};

static int fake_owner_tts_start(
  void *context, const struct bkvoice_turn_token_s *token, uint64_t now_ms)
{
  struct fake_owner_s *owner = context;

  owner->tts_start_calls++;
  return bkvoice_turn_tts_start(owner->turn, token, now_ms);
}

static int fake_owner_tts_audio(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t *pcm, size_t bytes, uint64_t now_ms)
{
  struct fake_owner_s *owner = context;

  owner->tts_audio_calls++;
  return bkvoice_turn_tts_audio(owner->turn, token, pcm, bytes, now_ms);
}

static int fake_owner_tts_end(
  void *context, const struct bkvoice_turn_token_s *token)
{
  struct fake_owner_s *owner = context;

  owner->tts_end_calls++;
  return bkvoice_turn_tts_end(owner->turn, token);
}

static int fake_owner_terminal(void *context, uint32_t session_id,
                               uint32_t turn_id, int reason)
{
  struct fake_owner_s *owner = context;
  struct bkvoice_turn_token_s token;

  owner->terminal_calls++;
  owner->terminal_session_id = session_id;
  owner->terminal_turn_id = turn_id;
  owner->terminal_reason = reason;
  if (owner->turn->state == BKVOICE_TURN_IDLE ||
      owner->turn->state == BKVOICE_TURN_FAULTED)
    {
      return 0;
    }

  token = owner->turn->active;
  token.sequence = owner->turn->last_control_sequence + 1u;
  return bkvoice_turn_cancel(owner->turn, &token, reason);
}

static const struct bkvoice_gateway_downlink_ops_s g_downlink_ops =
{
  .tts_start = fake_owner_tts_start,
  .tts_audio = fake_owner_tts_audio,
  .tts_end = fake_owner_tts_end,
  .terminal = fake_owner_terminal,
};

static uint64_t fake_now_ms(void *context)
{
  return ((struct fake_clock_s *)context)->now_ms;
}

static void fake_transport_initialize(struct fake_transport_s *transport)
{
  memset(transport, 0, sizeof(*transport));
  transport->send_chunk = 13;
  transport->recv_chunk = 7;
  transport->send_fail_after = SIZE_MAX;
  transport->empty_recv_error = -EAGAIN;
  transport->close_error = -EIO;
}

static void fake_enqueue_server_frame(
  struct fixture_s *fixture, uint8_t type, uint16_t flags,
  uint32_t turn_id, uint32_t sequence,
  const uint8_t *payload, uint32_t payload_len)
{
  struct bkvoice_companion_header_s header;
  uint8_t frame[BKVOICE_GATEWAY_FRAME_BYTES];
  size_t frame_size;

  memset(&header, 0, sizeof(header));
  header.magic = BKVOICE_COMPANION_MAGIC;
  header.version = BKVOICE_COMPANION_VERSION;
  header.type = type;
  header.flags = flags;
  header.header_len = BKVOICE_COMPANION_HEADER_BYTES;
  header.payload_len = payload_len;
  header.boot_generation = 7;
  header.session_id = fixture->session_id;
  header.turn_id = turn_id;
  header.sequence = sequence;
  header.timestamp_ms = fixture->clock.now_ms;
  assert(bkvoice_companion_encode(&header, payload, frame, sizeof(frame),
                                  &frame_size) == 0);
  assert(fixture->transport.rx_size + frame_size <=
         sizeof(fixture->transport.rx));
  memcpy(fixture->transport.rx + fixture->transport.rx_size, frame,
         frame_size);
  fixture->transport.rx_size += frame_size;
}

static void fixture_enqueue_server(
  struct fixture_s *fixture, uint8_t type, uint16_t flags,
  uint32_t turn_id, const uint8_t *payload, uint32_t payload_len)
{
  fixture->server_sequence++;
  fake_enqueue_server_frame(fixture, type, flags, turn_id,
                            fixture->server_sequence, payload, payload_len);
}

static int fake_volume(void *context, bool set, unsigned int requested,
                        unsigned int *observed)
{
  struct fake_owner_s *owner = context;
  if (set)
    {
      owner->requested_volume = requested;
    }

  *observed = owner->volume;
  return owner->volume_error;
}

static int fake_ota_request(
  void *context, uint32_t request_sequence,
  const uint8_t digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES])
{
  struct fake_owner_s *owner = context;

  owner->ota_calls++;
  owner->ota_request_sequence = request_sequence;
  memcpy(owner->ota_digest, digest, sizeof(owner->ota_digest));
  return owner->ota_error;
}

static void fixture_initialize_options(struct fixture_s *fixture,
                                       uint32_t uplink_credit,
                                       uint32_t downlink_credit, bool volume,
                                       bool ota)
{
  struct bkvoice_gateway_downlink_ops_s ops = g_downlink_ops;
  struct bkvoice_gateway_snapshot_s snapshot;
  struct bkvoice_gateway_config_s gateway_config =
  {
    .io_timeout_ms = 50,
  };
  struct bkvoice_turn_limits_s turn_limits =
  {
    .capture_timeout_ms = 1000,
    .waiting_tts_timeout_ms = 1000,
    .playback_timeout_ms = 1000,
    .audio_frame_bytes = BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
  };
  uint8_t window[sizeof(uint32_t)];

  memset(fixture, 0, sizeof(*fixture));
  fake_transport_initialize(&fixture->transport);
  fixture->clock.now_ms = 100;
  fixture->owner.turn = &fixture->turn;
  ops.volume = volume ? fake_volume : NULL;
  gateway_config.ota_request = ota ? fake_ota_request : NULL;
  gateway_config.ota_context = ota ? &fixture->owner : NULL;
  assert(bkvoice_turn_initialize(&fixture->turn, &g_audio_ops,
                                 &fixture->audio, &turn_limits, 7) == 0);
  assert(bkvoice_gateway_initialize(
           &fixture->gateway, &g_transport_ops, &fixture->transport,
           &ops, &fixture->owner, fake_now_ms, &fixture->clock,
           &gateway_config, 7) == 0);
  assert(bkvoice_gateway_connect(&fixture->gateway, 500) == 0);
  assert(fixture->transport.open_deadline == 500);

  bkvoice_gateway_snapshot(&fixture->gateway, &snapshot);
  fixture->session_id = snapshot.session_id;
  assert(fixture->session_id == 1);
  assert(snapshot.companion_state == BKVOICE_COMPANION_HELLO_SENT);

  fixture_enqueue_server(fixture, BKVOICE_COMPANION_WELCOME, 0, 0,
                         NULL, 0);
  assert(bkvoice_gateway_receive_one(&fixture->gateway, 600) == 0);
  assert(bkvoice_turn_session_open(&fixture->turn,
                                   fixture->session_id) == 0);

  test_put_be32(window, uplink_credit);
  fixture_enqueue_server(fixture, BKVOICE_COMPANION_WINDOW_UPDATE, 0, 0,
                         window, sizeof(window));
  assert(bkvoice_gateway_receive_one(&fixture->gateway, 601) == 0);
  assert(bkvoice_gateway_grant_downlink(&fixture->gateway,
                                        downlink_credit) == 0);
}

static void fixture_initialize_volume(struct fixture_s *fixture,
                                      uint32_t uplink_credit,
                                      uint32_t downlink_credit, bool volume)
{
  fixture_initialize_options(fixture, uplink_credit, downlink_credit, volume,
                             false);
}

static void fixture_initialize_ota(struct fixture_s *fixture,
                                   uint32_t uplink_credit,
                                   uint32_t downlink_credit)
{
  fixture_initialize_options(fixture, uplink_credit, downlink_credit, false,
                             true);
}

static void fixture_initialize(struct fixture_s *fixture,
                               uint32_t uplink_credit,
                               uint32_t downlink_credit)
{
  fixture_initialize_volume(fixture, uplink_credit, downlink_credit, false);
}

static void fixture_finish(struct fixture_s *fixture)
{
  assert(fixture->turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_session_close(&fixture->turn, -ENOTCONN) == 0);
  assert(bkvoice_gateway_disconnect(&fixture->gateway, -ENOTCONN) == 0);
  assert(bkvoice_gateway_uninitialize(&fixture->gateway) == 0);
}

static struct bkvoice_companion_header_s fake_tx_header(
  const struct fake_transport_s *transport, size_t index)
{
  struct bkvoice_companion_header_s header;
  const uint8_t *payload;
  size_t offset = 0;
  size_t frame_size;
  size_t current;

  for (current = 0; current <= index; current++)
    {
      assert(offset + BKVOICE_COMPANION_HEADER_BYTES <= transport->tx_size);
      frame_size = BKVOICE_COMPANION_HEADER_BYTES +
                   test_get_be32(transport->tx + offset + 12);
      assert(offset + frame_size <= transport->tx_size);
      if (current == index)
        {
          assert(bkvoice_companion_decode(transport->tx + offset,
                                          frame_size, &header,
                                          &payload) == 0);
          return header;
        }

      offset += frame_size;
    }

  assert(false);
  memset(&header, 0, sizeof(header));
  return header;
}

static uint32_t fake_tx_window_credit(
  const struct fake_transport_s *transport, size_t index)
{
  struct bkvoice_companion_header_s header;
  size_t offset = 0;
  size_t frame_size;
  size_t current;

  for (current = 0; current <= index; current++)
    {
      assert(offset + BKVOICE_COMPANION_HEADER_BYTES <= transport->tx_size);
      frame_size = BKVOICE_COMPANION_HEADER_BYTES +
                   test_get_be32(transport->tx + offset + 12);
      assert(offset + frame_size <= transport->tx_size);
      if (current == index)
        {
          header = fake_tx_header(transport, index);
          assert(header.type == BKVOICE_COMPANION_WINDOW_UPDATE);
          assert(header.payload_len == sizeof(uint32_t));
          return test_get_be32(
            transport->tx + offset + BKVOICE_COMPANION_HEADER_BYTES);
        }

      offset += frame_size;
    }

  assert(false);
  return 0;
}

static void test_transport_guards(void)
{
  struct fake_transport_s fake;
  struct bkvoice_transport_s transport;
  struct bkvoice_transport_ops_s bad_ops = g_transport_ops;
  uint8_t byte = 1;

  fake_transport_initialize(&fake);
  bad_ops.close = NULL;
  assert(bkvoice_transport_initialize(&transport, &bad_ops, &fake) ==
         -EINVAL);
  assert(bkvoice_transport_initialize(&transport, &g_transport_ops,
                                      &fake) == 0);
  assert(bkvoice_transport_send_all(&transport, &byte, 1, 1) ==
         -ENOTCONN);
  assert(bkvoice_transport_open(&transport, 10) == 0);
  assert(bkvoice_transport_open(&transport, 10) == -EALREADY);
  fake.send_zero_once = true;
  assert(bkvoice_transport_send_all(&transport, &byte, 1, 11) == -EPIPE);
  fake.empty_recv_error = 0;
  assert(bkvoice_transport_recv_exact(&transport, &byte, 1, 12) ==
         -ECONNRESET);
  assert(bkvoice_transport_interrupt(&transport) == 0);
  assert(bkvoice_transport_close(&transport) == 0);
  assert(bkvoice_transport_close(&transport) == 0);
}

static void test_single_turn_vertical(void)
{
  static const uint8_t expected_types[] =
  {
    BKVOICE_COMPANION_HELLO,
    BKVOICE_COMPANION_WINDOW_UPDATE,
    BKVOICE_COMPANION_TURN_START,
    BKVOICE_COMPANION_AUDIO_UP,
    BKVOICE_COMPANION_AUDIO_UP,
    BKVOICE_COMPANION_TURN_END,
    BKVOICE_COMPANION_WINDOW_UPDATE,
  };
  struct bkvoice_gateway_snapshot_s gateway_snapshot;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct bkvoice_companion_header_s header;
  struct fixture_s fixture;
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  size_t index;

  memset(audio, 0x5a, sizeof(audio));
  fixture_initialize(&fixture, sizeof(audio) * 2u, sizeof(audio) * 2u);
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->audio(
           &fixture.gateway, &token, audio, sizeof(audio)) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->audio(
           &fixture.gateway, &token, audio, sizeof(audio)) == 0);

  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_ptt_up(&fixture.turn, &event,
                             fixture.clock.now_ms) == 0);
  assert(fixture.audio.mic_release_calls == 1);
  assert(bkvoice_gateway_capture_sink_ops()->end(&fixture.gateway,
                                                 &token) == 0);

  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_START,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         NULL, 0);
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_AUDIO_DOWN,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         audio, sizeof(audio));
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_END,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         NULL, 0);
  struct bkvoice_gateway_frame_s frames[3];
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &frames[0], 700) == 0);
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &frames[1], 701) == 0);
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &frames[2], 702) == 0);
  assert(fixture.owner.tts_start_calls == 0);
  assert(fixture.owner.tts_audio_calls == 0 &&
         fixture.owner.tts_end_calls == 0);
  assert(fixture.owner.terminal_calls == 0);
  assert(fixture.audio.dac_acquire_calls == 0);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &frames[0]) == 0);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &frames[1]) == 0);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &frames[2]) == 0);

  assert(fixture.turn.state == BKVOICE_TURN_IDLE);
  assert(fixture.audio.dac_write_calls == 1);
  assert(fixture.audio.dac_bytes == sizeof(audio));
  assert(fixture.audio.dac_release_calls == 1);
  assert(fixture.owner.tts_start_calls == 1);
  assert(fixture.owner.tts_audio_calls == 1);
  assert(fixture.owner.tts_end_calls == 1);
  assert(fixture.owner.terminal_calls == 0);

  bkvoice_gateway_snapshot(&fixture.gateway, &gateway_snapshot);
  assert(gateway_snapshot.companion_state == BKVOICE_COMPANION_IDLE);
  assert(gateway_snapshot.tx_frames == 7);
  assert(gateway_snapshot.rx_frames == 5);
  assert(gateway_snapshot.downlink_sequence == 3);
  assert(gateway_snapshot.tx_window == 0);
  assert(gateway_snapshot.rx_window == sizeof(audio) * 2u);
  assert(!gateway_snapshot.faulted);
  assert(fixture.transport.send_calls > (int)gateway_snapshot.tx_frames);
  assert(fixture.transport.recv_calls > (int)gateway_snapshot.rx_frames);
  assert(fixture.transport.send_deadline ==
         fixture.clock.now_ms + 50);
  assert(fixture.transport.recv_deadline == 702);

  for (index = 0; index < sizeof(expected_types); index++)
    {
      header = fake_tx_header(&fixture.transport, index);
      assert(header.type == expected_types[index]);
      assert(header.sequence == index + 1u);
    }

  fixture_finish(&fixture);
}

static void test_downlink_credit_replenished_after_dac_accept(void)
{
  struct bkvoice_gateway_snapshot_s snapshot;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct fixture_s fixture;
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  size_t index;

  memset(audio, 0x69, sizeof(audio));
  fixture_initialize(&fixture, sizeof(audio), sizeof(audio));
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_ptt_up(&fixture.turn, &event,
                             fixture.clock.now_ms) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->end(&fixture.gateway,
                                                 &token) == 0);

  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_START,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         NULL, 0);
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 700) == 0);
  for (index = 0; index < 6; index++)
    {
      fixture_enqueue_server(&fixture, BKVOICE_COMPANION_AUDIO_DOWN,
                             BKVOICE_COMPANION_FLAG_SYNTHETIC,
                             token.turn_id, audio, sizeof(audio));
      assert(bkvoice_gateway_receive_one(&fixture.gateway, 701 + index) == 0);
      bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
      assert(snapshot.rx_window == sizeof(audio));
      assert(fixture.audio.dac_write_calls == (int)index + 1);
      assert(fake_tx_window_credit(&fixture.transport, 4 + index) ==
             sizeof(audio));
    }

  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_END,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         NULL, 0);
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 707) == 0);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.companion_state == BKVOICE_COMPANION_IDLE);
  assert(snapshot.tx_frames == 10);
  assert(snapshot.rx_frames == 10);
  assert(snapshot.downlink_sequence == 8);
  assert(snapshot.rx_window == sizeof(audio));
  assert(!snapshot.faulted);
  fixture_finish(&fixture);
}

static void test_rejected_downlink_does_not_replenish_credit(void)
{
  struct bkvoice_gateway_snapshot_s before;
  struct bkvoice_gateway_snapshot_s after;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct fixture_s fixture;
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  size_t tx_size;

  memset(audio, 0x52, sizeof(audio));
  fixture_initialize(&fixture, sizeof(audio), sizeof(audio));
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_ptt_up(&fixture.turn, &event,
                             fixture.clock.now_ms) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->end(&fixture.gateway,
                                                 &token) == 0);
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_START,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         NULL, 0);
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 800) == 0);

  bkvoice_gateway_snapshot(&fixture.gateway, &before);
  tx_size = fixture.transport.tx_size;
  fixture.audio.dac_write_result_set = true;
  fixture.audio.dac_write_result = (ssize_t)sizeof(audio) - 1;
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_AUDIO_DOWN,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         audio, sizeof(audio));
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 801) == -EIO);
  bkvoice_gateway_snapshot(&fixture.gateway, &after);
  assert(fixture.transport.tx_size == tx_size);
  assert(after.tx_frames == before.tx_frames);
  assert(after.faulted);
  assert(fixture.audio.dac_write_calls == 1);
  assert(fixture.audio.dac_bytes == sizeof(audio) - 1u);
  assert(fixture.owner.terminal_calls == 1);
  assert(fixture.turn.state == BKVOICE_TURN_IDLE);
  fixture_finish(&fixture);
}

static void test_downlink_credit_send_failure_faults_session(void)
{
  struct bkvoice_gateway_snapshot_s before;
  struct bkvoice_gateway_snapshot_s after;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct fixture_s fixture;
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  size_t tx_size;

  memset(audio, 0x27, sizeof(audio));
  fixture_initialize(&fixture, sizeof(audio), sizeof(audio));
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_ptt_up(&fixture.turn, &event,
                             fixture.clock.now_ms) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->end(&fixture.gateway,
                                                 &token) == 0);
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_START,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         NULL, 0);
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 900) == 0);

  bkvoice_gateway_snapshot(&fixture.gateway, &before);
  tx_size = fixture.transport.tx_size;
  fixture.transport.send_fail_after = tx_size + 9u;
  fixture.transport.send_error = -EIO;
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_AUDIO_DOWN,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
                         audio, sizeof(audio));
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 901) == -EIO);
  bkvoice_gateway_snapshot(&fixture.gateway, &after);
  assert(after.faulted);
  assert(after.companion_state == BKVOICE_COMPANION_DISCONNECTED);
  assert(after.tx_frames == before.tx_frames);
  assert(fixture.transport.tx_size == tx_size + 9u);
  assert(fixture.audio.dac_write_calls == 1);
  assert(bkvoice_gateway_capture_sink_ops()->audio(
           &fixture.gateway, &token, audio, sizeof(audio)) == -ENOTCONN);

  assert(bkvoice_turn_session_close(&fixture.turn, -EIO) == 0);
  assert(bkvoice_gateway_disconnect(&fixture.gateway, -EIO) == 0);
  assert(bkvoice_gateway_uninitialize(&fixture.gateway) == 0);
}

static void test_backpressure_retry_and_local_cancel(void)
{
  struct bkvoice_gateway_snapshot_s before;
  struct bkvoice_gateway_snapshot_s after;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct bkvoice_companion_header_s header;
  struct fixture_s fixture;
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  uint8_t window[sizeof(uint32_t)];
  size_t tx_size;

  memset(audio, 0x33, sizeof(audio));
  fixture_initialize(&fixture, sizeof(audio), sizeof(audio));
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->audio(
           &fixture.gateway, &token, audio, sizeof(audio)) == 0);
  bkvoice_gateway_snapshot(&fixture.gateway, &before);
  tx_size = fixture.transport.tx_size;
  assert(bkvoice_gateway_capture_sink_ops()->audio(
           &fixture.gateway, &token, audio, sizeof(audio)) == -EAGAIN);
  bkvoice_gateway_snapshot(&fixture.gateway, &after);
  assert(fixture.transport.tx_size == tx_size);
  assert(after.tx_sequence == before.tx_sequence);
  assert(!after.faulted);

  test_put_be32(window, sizeof(audio));
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_WINDOW_UPDATE, 0,
                         token.turn_id, window, sizeof(window));
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 800) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->audio(
           &fixture.gateway, &token, audio, sizeof(audio)) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->cancel(
           &fixture.gateway, &token, -ECANCELED) == 0);

  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_cancel(&fixture.turn, &event, -ECANCELED) == 0);
  header = fake_tx_header(&fixture.transport, 5);
  assert(header.type == BKVOICE_COMPANION_CANCEL);
  fixture_finish(&fixture);
}

static void test_remote_cancel_dispatch(void)
{
  struct bkvoice_gateway_snapshot_s snapshot;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct fixture_s fixture;

  fixture_initialize(&fixture, BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
                     BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_ptt_up(&fixture.turn, &event,
                             fixture.clock.now_ms) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->end(&fixture.gateway,
                                                 &token) == 0);

  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_CANCEL, 0,
                         token.turn_id, NULL, 0);
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 900) == 0);
  assert(fixture.owner.terminal_calls == 1);
  assert(fixture.owner.terminal_reason == -ECANCELED);
  assert(fixture.turn.state == BKVOICE_TURN_IDLE);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.companion_state == BKVOICE_COMPANION_IDLE);
  assert(!snapshot.faulted);
  fixture_finish(&fixture);
}

static void test_partial_send_failure_and_close_retry(void)
{
  struct bkvoice_gateway_snapshot_s snapshot;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct fixture_s fixture;
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];

  memset(audio, 0x77, sizeof(audio));
  fixture_initialize(&fixture, sizeof(audio), sizeof(audio));
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  fixture.transport.send_fail_after = fixture.transport.tx_size + 9u;
  fixture.transport.send_error = -EIO;
  assert(bkvoice_gateway_capture_sink_ops()->audio(
           &fixture.gateway, &token, audio, sizeof(audio)) == -EIO);
  assert(bkvoice_gateway_capture_sink_ops()->cancel(
           &fixture.gateway, &token, -EIO) == 0);

  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.faulted);
  assert(snapshot.companion_state == BKVOICE_COMPANION_DISCONNECTED);
  assert(snapshot.transport.opened);
  assert(fixture.transport.interrupt_calls == 1);
  assert(fixture.owner.terminal_calls == 0);

  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_cancel(&fixture.turn, &event, -EIO) == 0);
  assert(bkvoice_turn_session_close(&fixture.turn, -EIO) == 0);
  fixture.transport.close_failures = 1;
  assert(bkvoice_gateway_disconnect(&fixture.gateway, -EIO) == -EIO);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.transport.opened);
  assert(snapshot.faulted);
  assert(bkvoice_gateway_disconnect(&fixture.gateway, -EIO) == 0);
  assert(bkvoice_gateway_uninitialize(&fixture.gateway) == 0);
}

static void test_sequence_gap_and_receive_timeout_fail_closed(void)
{
  struct bkvoice_gateway_snapshot_s snapshot;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_token_s token;
  struct fixture_s fixture;

  fixture_initialize(&fixture, BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
                     BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  assert(bkvoice_turn_ptt_down(&fixture.turn, fixture.session_id, 1,
                               fixture.clock.now_ms, &token) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->start(&fixture.gateway,
                                                   &token) == 0);
  event = token;
  event.sequence = 2;
  assert(bkvoice_turn_ptt_up(&fixture.turn, &event,
                             fixture.clock.now_ms) == 0);
  assert(bkvoice_gateway_capture_sink_ops()->end(&fixture.gateway,
                                                 &token) == 0);

  fake_enqueue_server_frame(
    &fixture, BKVOICE_COMPANION_TTS_START,
    BKVOICE_COMPANION_FLAG_SYNTHETIC, token.turn_id,
    fixture.server_sequence + 2u, NULL, 0);
  struct bkvoice_gateway_frame_s bad_sequence;
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &bad_sequence,
                                        1000) == 0);
  assert(fixture.owner.terminal_calls == 0);
  assert(!fixture.gateway.faulted);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &bad_sequence) ==
         -EPROTO);
  assert(fixture.owner.terminal_calls == 1);
  assert(fixture.owner.terminal_session_id == fixture.session_id);
  assert(fixture.owner.terminal_turn_id == token.turn_id);
  assert(fixture.turn.state == BKVOICE_TURN_IDLE);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.faulted);
  assert(snapshot.companion_state == BKVOICE_COMPANION_DISCONNECTED);
  assert(bkvoice_gateway_disconnect(&fixture.gateway, -EPROTO) == 0);
  assert(bkvoice_turn_session_close(&fixture.turn, -EPROTO) == 0);
  assert(bkvoice_gateway_uninitialize(&fixture.gateway) == 0);

  fixture_initialize(&fixture, BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
                     BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  fixture.transport.empty_recv_error = -ETIMEDOUT;
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 1100) ==
         -ETIMEDOUT);
  assert(fixture.transport.recv_deadline == 1100);
  assert(fixture.transport.interrupt_calls == 1);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.faulted);
  assert(bkvoice_gateway_disconnect(&fixture.gateway, -ETIMEDOUT) == 0);
  assert(bkvoice_turn_session_close(&fixture.turn, -ETIMEDOUT) == 0);
  assert(bkvoice_gateway_uninitialize(&fixture.gateway) == 0);
}

static void test_split_stale_frames_and_generation_limit(void)
{
  struct fixture_s fixture;
  struct bkvoice_gateway_frame_s old_frame;
  struct bkvoice_gateway_frame_s old_error;
  struct bkvoice_gateway_frame_s welcome;
  struct bkvoice_gateway_snapshot_s snapshot;
  uint8_t credit[4];
  uint32_t generation;
  int opens;
  int interrupts;

  fixture_initialize(&fixture, BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
                      BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  generation = fixture.gateway.connection_generation;
  assert(generation == 1);
  test_put_be32(credit, BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_WINDOW_UPDATE,
                           0, 0, credit, sizeof(credit));
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &old_frame, 800) == 0);
  fixture.transport.empty_recv_error = -ETIMEDOUT;
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &old_error, 801) ==
         -ETIMEDOUT);
  assert(old_error.error == -ETIMEDOUT && old_error.size == 0);
  assert(old_frame.generation == generation &&
         old_error.generation == generation);
  assert(!fixture.gateway.faulted && fixture.owner.terminal_calls == 0);
  assert(fixture.transport.interrupt_calls == 0);

  assert(bkvoice_turn_session_close(&fixture.turn, -ENOTCONN) == 0);
  assert(bkvoice_gateway_disconnect(&fixture.gateway, -ENOTCONN) == 0);
  fixture.transport.open_error = -EIO;
  assert(bkvoice_gateway_connect(&fixture.gateway, 900) == -EIO);
  assert(fixture.gateway.connection_generation == generation);
  fixture.transport.open_error = 0;
  assert(bkvoice_gateway_connect(&fixture.gateway, 901) == 0);
  assert(fixture.gateway.connection_generation == generation + 1);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  fixture.session_id = snapshot.session_id;
  fixture.server_sequence = 0;
  interrupts = fixture.transport.interrupt_calls;

  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &old_frame) ==
         -ESTALE);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &old_error) ==
         -ESTALE);
  assert(!fixture.gateway.faulted && fixture.owner.terminal_calls == 0);
  assert(fixture.transport.interrupt_calls == interrupts);
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_WELCOME, 0, 0, NULL, 0);
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &welcome, 902) == 0);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &welcome) == 0);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &old_error) ==
         -ESTALE);
  assert(!fixture.gateway.faulted);
  assert(bkvoice_turn_session_open(&fixture.turn, fixture.session_id) == 0);
  assert(bkvoice_turn_session_close(&fixture.turn, -ENOTCONN) == 0);
  assert(bkvoice_gateway_disconnect(&fixture.gateway, -ENOTCONN) == 0);

  fixture.gateway.connection_generation = UINT32_MAX;
  opens = fixture.transport.open_calls;
  assert(bkvoice_gateway_connect(&fixture.gateway, 903) == -EOVERFLOW);
  assert(fixture.transport.open_calls == opens);
  assert(fixture.gateway.connection_generation == UINT32_MAX);
  assert(bkvoice_gateway_uninitialize(&fixture.gateway) == 0);
}

static void test_split_length_error_is_deferred(void)
{
  struct fixture_s fixture;
  struct bkvoice_gateway_frame_s frame;
  struct bkvoice_gateway_frame_s malformed;
  size_t offset;

  fixture_initialize(&fixture, BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
                      BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  offset = fixture.transport.rx_size;
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_WELCOME,
                           0, 0, NULL, 0);
  test_put_be32(fixture.transport.rx + offset + 12,
                BKVOICE_GATEWAY_MAX_PAYLOAD + 1);
  assert(bkvoice_gateway_receive_frame(&fixture.gateway, &frame, 800) ==
         -EMSGSIZE);
  assert(frame.error == -EMSGSIZE && frame.size == 0);
  assert(!fixture.gateway.faulted && fixture.transport.interrupt_calls == 0);
  assert(fixture.owner.terminal_calls == 0);
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &frame) ==
         -EMSGSIZE);
  assert(fixture.gateway.faulted && fixture.transport.interrupt_calls == 1);
  fixture_finish(&fixture);

  fixture_initialize(&fixture, BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
                      BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  memset(&malformed, 0, sizeof(malformed));
  malformed.generation = fixture.gateway.connection_generation;
  malformed.size = sizeof(malformed.data) + 1;
  assert(bkvoice_gateway_dispatch_frame(&fixture.gateway, &malformed) ==
         -EMSGSIZE);
  fixture_finish(&fixture);
}

static void test_volume_dispatch(void)
{
  struct fixture_s fixture;
  struct bkvoice_companion_header_s header;
  const uint8_t *payload;
  uint8_t requested[4];
  size_t offset;
  int i;

  fixture_initialize_volume(&fixture, 640, 640, true);
  assert(fake_tx_header(&fixture.transport, 0).payload_len == 4);
  assert(test_get_be32(fixture.transport.tx + BKVOICE_COMPANION_HEADER_BYTES) ==
         (BKVOICE_COMPANION_CAP_VOLUME |
          BKVOICE_COMPANION_CAP_PLAYBACK_ACK |
          BKVOICE_COMPANION_CAP_STATUS_REPORT));
  for (i = 0; i < 3; i++)
    {
      fixture.owner.volume = i == 2 ? 101 : 67;
      fixture.owner.volume_error = i == 1 ? -EBUSY : 0;
      offset = fixture.transport.tx_size;
      test_put_be32(requested, 65);
      fixture_enqueue_server(&fixture, BKVOICE_COMPANION_VOLUME_SET, 0, 0,
                              requested, sizeof(requested));
      assert(bkvoice_gateway_receive_one(&fixture.gateway, 700) == 0);
      assert(fixture.owner.requested_volume == 65);
      assert(bkvoice_companion_decode(fixture.transport.tx + offset,
               fixture.transport.tx_size - offset, &header, &payload) == 0);
      assert(header.type == BKVOICE_COMPANION_VOLUME_REPORT);
      assert(header.turn_id == 0);
      assert(test_get_be32(payload) == fixture.server_sequence);
      assert((int32_t)test_get_be32(payload + 4) ==
             (i == 1 ? -EBUSY : i == 2 ? -EIO : 0));
      assert(test_get_be32(payload + 8) == (i == 0 ? 67u : UINT32_MAX));
      assert(!fixture.gateway.faulted);
    }

  fixture_finish(&fixture);
}

static void test_status_report(void)
{
  struct bkvoice_gateway_status_s status;
  struct bkvoice_companion_header_s header;
  struct fixture_s fixture;
  const uint8_t *payload;
  size_t offset;

  fixture_initialize(&fixture, 640, 640);
  assert(test_get_be32(fixture.transport.tx +
                       BKVOICE_COMPANION_HEADER_BYTES) ==
         (BKVOICE_COMPANION_CAP_PLAYBACK_ACK |
          BKVOICE_COMPANION_CAP_STATUS_REPORT));
  memset(&status, 0, sizeof(status));
  status.battery_percent = BKVOICE_COMPANION_BATTERY_UNKNOWN;
  status.charging = 1;
  status.battery_state = BKVOICE_COMPANION_BATTERY_STATE_CHARGING;
  status.battery_voltage_mv = 3888;
  status.firmware_major = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_minor = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_revision = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_build = BKVOICE_COMPANION_FW_BUILD_UNKNOWN;

  offset = fixture.transport.tx_size;
  assert(bkvoice_gateway_report_status(&fixture.gateway, &status) == 0);
  assert(bkvoice_companion_decode(fixture.transport.tx + offset,
           fixture.transport.tx_size - offset, &header, &payload) == 0);
  assert(header.type == BKVOICE_COMPANION_STATUS_REPORT);
  assert(header.turn_id == 0 && header.flags == 0);
  assert(payload[0] == 1 && payload[1] == 0xff && payload[2] == 1 &&
         payload[3] == BKVOICE_COMPANION_BATTERY_STATE_CHARGING);
  assert(test_get_be32(payload + 4) == 3888);
  assert(test_get_be16(payload + 8) == UINT16_MAX);
  assert(test_get_be32(payload + 16) == UINT32_MAX);

  status.firmware_major = 18;
  status.firmware_minor = 6;
  status.firmware_revision = 389;
  status.firmware_build = 449;
  status.firmware_identity_valid = true;
  for (size_t index = 0; index < sizeof(status.firmware_root_sha256); index++)
    {
      status.firmware_root_sha256[index] = (uint8_t)(index + 1u);
    }
  offset = fixture.transport.tx_size;
  assert(bkvoice_gateway_report_status(&fixture.gateway, &status) == 0);
  assert(bkvoice_companion_decode(fixture.transport.tx + offset,
           fixture.transport.tx_size - offset, &header, &payload) == 0);
  assert(header.payload_len == BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES);
  assert(payload[0] == 2 && test_get_be16(payload + 8) == 18 &&
         test_get_be16(payload + 10) == 6 &&
         test_get_be16(payload + 12) == 389 &&
         test_get_be32(payload + 16) == 449);
  assert(memcmp(payload + BKVOICE_COMPANION_STATUS_REPORT_BYTES,
                status.firmware_root_sha256,
                sizeof(status.firmware_root_sha256)) == 0);

  memset(status.firmware_root_sha256, 0,
         sizeof(status.firmware_root_sha256));
  offset = fixture.transport.tx_size;
  assert(bkvoice_gateway_report_status(&fixture.gateway, &status) == -ERANGE);
  assert(fixture.transport.tx_size == offset);

  offset = fixture.transport.tx_size;
  status.firmware_identity_valid = false;
  status.charging = 0;
  assert(bkvoice_gateway_report_status(&fixture.gateway, &status) == -ERANGE);
  assert(fixture.transport.tx_size == offset);
  fixture_finish(&fixture);
}

static void test_ota_dispatch_and_reports(void)
{
  struct fixture_s fixture;
  struct bkvoice_companion_header_s header;
  struct bkvoice_gateway_snapshot_s snapshot;
  struct bkvoice_gateway_status_s status;
  const uint8_t *payload;
  uint8_t digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES];
  uint8_t other_digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES];
  size_t offset;

  memset(digest, 0x39, sizeof(digest));
  memset(other_digest, 0x3a, sizeof(other_digest));
  fixture_initialize_ota(&fixture, 640, 640);
  assert(test_get_be32(fixture.transport.tx + BKVOICE_COMPANION_HEADER_BYTES) ==
         (BKVOICE_COMPANION_CAP_PLAYBACK_ACK |
          BKVOICE_COMPANION_CAP_STATUS_REPORT |
          BKVOICE_COMPANION_CAP_OTA));

  offset = fixture.transport.tx_size;
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_OTA_REQUEST, 0, 0,
                         digest, sizeof(digest));
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 700) == 0);
  assert(fixture.owner.ota_calls == 1);
  assert(fixture.owner.ota_request_sequence == fixture.server_sequence);
  assert(memcmp(fixture.owner.ota_digest, digest, sizeof(digest)) == 0);
  assert(bkvoice_companion_decode(fixture.transport.tx + offset,
                                  fixture.transport.tx_size - offset,
                                  &header, &payload) == 0);
  assert(header.type == BKVOICE_COMPANION_OTA_REPORT && header.turn_id == 0);
  assert(test_get_be32(payload) == fixture.owner.ota_request_sequence);
  assert((int32_t)test_get_be32(payload + 4) == 0);
  assert(payload[8] == BKVOICE_COMPANION_OTA_DOWNLOADING && payload[9] == 0);
  assert(memcmp(payload + 12, digest, sizeof(digest)) == 0);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.ota_active && snapshot.ota_phase ==
         BKVOICE_COMPANION_OTA_DOWNLOADING);

  memset(&status, 0, sizeof(status));
  status.battery_percent = BKVOICE_COMPANION_BATTERY_UNKNOWN;
  status.charging = 1;
  status.battery_state = BKVOICE_COMPANION_BATTERY_STATE_CHARGING;
  status.battery_voltage_mv = 3800;
  status.firmware_major = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_minor = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_revision = BKVOICE_COMPANION_FW_UNKNOWN;
  status.firmware_build = BKVOICE_COMPANION_FW_BUILD_UNKNOWN;
  assert(bkvoice_gateway_report_status(&fixture.gateway, &status) == 0);

  offset = fixture.transport.tx_size;
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_OTA_REQUEST, 0, 0,
                         digest, sizeof(digest));
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 700) == -EBUSY);
  assert(bkvoice_companion_decode(fixture.transport.tx + offset,
                                  fixture.transport.tx_size - offset,
                                  &header, &payload) == 0);
  assert(header.type == BKVOICE_COMPANION_OTA_REPORT &&
         (int32_t)test_get_be32(payload + 4) == -EBUSY &&
         payload[8] == BKVOICE_COMPANION_OTA_FAILED);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(snapshot.ota_active && !snapshot.faulted &&
         snapshot.rx_sequence == fixture.server_sequence);

  offset = fixture.transport.tx_size;
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence + 1u,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_VERIFYING, 1) ==
         -EINVAL);
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     other_digest, 0,
                                     BKVOICE_COMPANION_OTA_VERIFYING, 1) ==
         -EINVAL);
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_FAILED, 1) ==
         -EINVAL);
  assert(fixture.transport.tx_size == offset);

  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_VERIFYING, 1) == 0);
  offset = fixture.transport.tx_size;
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_DOWNLOADING, 2) ==
         -EINVAL);
  assert(fixture.transport.tx_size == offset);
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_STAGED, 100) == 0);
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_REBOOTING, 100) == 0);
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_TRIAL, 100) == 0);
  assert(bkvoice_gateway_report_ota(&fixture.gateway,
                                     fixture.owner.ota_request_sequence,
                                     digest, 0,
                                     BKVOICE_COMPANION_OTA_CONFIRMED, 100) == 0);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(!snapshot.ota_active);

  fixture.owner.ota_error = -EPERM;
  offset = fixture.transport.tx_size;
  fixture_enqueue_server(&fixture, BKVOICE_COMPANION_OTA_REQUEST, 0, 0,
                         digest, sizeof(digest));
  assert(bkvoice_gateway_receive_one(&fixture.gateway, 701) == 0);
  assert(bkvoice_companion_decode(fixture.transport.tx + offset,
                                  fixture.transport.tx_size - offset,
                                  &header, &payload) == 0);
  assert(header.type == BKVOICE_COMPANION_OTA_REPORT);
  assert((int32_t)test_get_be32(payload + 4) == -EPERM);
  assert(payload[8] == BKVOICE_COMPANION_OTA_FAILED);
  bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
  assert(!snapshot.ota_active && fixture.owner.ota_calls == 2);
  fixture_finish(&fixture);
}

static void test_ota_request_busy_voice_states(void)
{
  static const enum bkvoice_companion_state_e states[] =
  {
    BKVOICE_COMPANION_UPLINK,
    BKVOICE_COMPANION_THINKING,
    BKVOICE_COMPANION_DOWNLINK,
  };
  struct fixture_s fixture;
  struct bkvoice_gateway_snapshot_s snapshot;
  struct bkvoice_companion_header_s header;
  const uint8_t *payload;
  uint8_t digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES];
  size_t index;
  size_t offset;

  memset(digest, 0x4d, sizeof(digest));
  for (index = 0; index < sizeof(states) / sizeof(states[0]); index++)
    {
      fixture_initialize_ota(&fixture, 640, 640);
      fixture.gateway.companion.state = states[index];
      fixture.gateway.companion.turn_id = 1;
      offset = fixture.transport.tx_size;
      fixture_enqueue_server(&fixture, BKVOICE_COMPANION_OTA_REQUEST, 0, 0,
                             digest, sizeof(digest));
      assert(bkvoice_gateway_receive_one(&fixture.gateway, 700) == -EBUSY);
      assert(fixture.owner.ota_calls == 0);
      assert(bkvoice_companion_decode(fixture.transport.tx + offset,
                                      fixture.transport.tx_size - offset,
                                      &header, &payload) == 0);
      assert(header.type == BKVOICE_COMPANION_OTA_REPORT &&
             header.turn_id == 0 && test_get_be32(payload) ==
             fixture.server_sequence &&
             (int32_t)test_get_be32(payload + 4) == -EBUSY &&
             payload[8] == BKVOICE_COMPANION_OTA_FAILED &&
             memcmp(payload + 12, digest, sizeof(digest)) == 0);
      bkvoice_gateway_snapshot(&fixture.gateway, &snapshot);
      assert(!snapshot.faulted && !snapshot.ota_active &&
             snapshot.companion_state == states[index]);
      fixture.gateway.companion.state = BKVOICE_COMPANION_IDLE;
      fixture.gateway.companion.turn_id = 0;
      fixture_finish(&fixture);
    }
}

int main(void)
{
  test_split_stale_frames_and_generation_limit();
  test_split_length_error_is_deferred();
  test_transport_guards();
  test_volume_dispatch();
  test_status_report();
  test_ota_dispatch_and_reports();
  test_ota_request_busy_voice_states();
  test_single_turn_vertical();
  test_downlink_credit_replenished_after_dac_accept();
  test_rejected_downlink_does_not_replenish_credit();
  test_downlink_credit_send_failure_faults_session();
  test_backpressure_retry_and_local_cancel();
  test_remote_cancel_dispatch();
  test_partial_send_failure_and_close_retry();
  test_sequence_gap_and_receive_timeout_fail_closed();
  puts("BKVOICE_GATEWAY_HOST_PASS");
  return 0;
}
