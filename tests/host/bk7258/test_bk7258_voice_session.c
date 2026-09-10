/* SPDX-License-Identifier: Apache-2.0 */

/* Reuse the byte-stream and audio fakes from the gateway component suite,
 * while still running that suite in this executable.  This keeps the session
 * test focused on orchestration instead of maintaining a second transport
 * implementation.
 */

#define main bkvoice_gateway_component_tests
#include "test_bk7258_voice_gateway.c"
#undef main

#include "bk7258_voice_session.h"

#include <semaphore.h>

struct session_source_s
{
  sem_t audio_sent;
  sem_t wake_reader;
  bool attached;
  int reads;
  int interrupts;
  int detaches;
};

struct session_fixture_s
{
  struct fake_transport_s transport;
  struct fake_audio_s audio;
  struct fake_clock_s clock;
  struct session_source_s source;
  struct bkvoice_ptt_s ptt;
  struct bkvoice_session_s session;
  uint32_t session_id;
  uint32_t server_sequence;
  unsigned int ota_calls;
  uint32_t ota_request_sequence;
};

struct session_prefill_s
{
  struct session_source_s *source;
  unsigned int read_calls;
  unsigned int live_calls;
  uint8_t live_marker;
};

static int session_source_attach(void *context)
{
  struct session_source_s *source = context;

  assert(!source->attached);
  source->attached = true;
  return 0;
}

static ssize_t session_source_read(void *context, void *pcm, size_t bytes)
{
  struct session_source_s *source = context;
  int ret;

  assert(source->attached && pcm != NULL);
  source->reads++;
  if (source->reads == 1 || source->reads == 3)
    {
      assert(bytes == BKVOICE_CAPTURE_FRAME_BYTES);
      memset(pcm, source->reads == 1 ? 0x51 : 0x52, bytes);
      assert(sem_post(&source->audio_sent) == 0);
      return (ssize_t)bytes;
    }

  do
    {
      ret = sem_wait(&source->wake_reader);
    }
  while (ret < 0 && errno == EINTR);

  assert(ret == 0);
  return -EPIPE;
}

static int session_source_interrupt(void *context)
{
  struct session_source_s *source = context;

  assert(source->attached);
  source->interrupts++;
  return sem_post(&source->wake_reader) == 0 ? 0 : -errno;
}

static int session_source_detach(void *context)
{
  struct session_source_s *source = context;

  assert(source->attached);
  source->attached = false;
  source->detaches++;
  return 0;
}

static const struct bkvoice_capture_source_ops_s g_session_source_ops =
{
  .attach = session_source_attach,
  .read = session_source_read,
  .interrupt = session_source_interrupt,
  .detach = session_source_detach,
};

static int session_prefill_read(
  void *context, size_t frame_index,
  uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
  struct session_prefill_s *prefill = context;

  assert(prefill != NULL && prefill->source != NULL && pcm != NULL);
  assert(prefill->source->attached && prefill->source->reads == 0);
  assert(frame_index == prefill->read_calls);
  prefill->read_calls++;
  memset(pcm, (int)(0xa0u + frame_index),
         BKVOICE_CAPTURE_FRAME_BYTES);
  return 0;
}

static void session_live_observe(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
  struct session_prefill_s *prefill = context;

  assert(prefill != NULL && prefill->source != NULL && token != NULL &&
         pcm != NULL);
  assert(token->boot_generation == 7 && token->session_id == 1 &&
         token->turn_id != 0);
  prefill->live_calls++;
  prefill->live_marker = pcm[0];
}

static void session_enqueue_server(
  struct session_fixture_s *fixture, uint8_t type, uint16_t flags,
  uint32_t turn_id, const uint8_t *payload, uint32_t payload_len)
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
  header.sequence = ++fixture->server_sequence;
  header.timestamp_ms = fixture->clock.now_ms;
  assert(bkvoice_companion_encode(&header, payload, frame, sizeof(frame),
                                  &frame_size) == 0);
  assert(fixture->transport.rx_size + frame_size <=
         sizeof(fixture->transport.rx));
  memcpy(fixture->transport.rx + fixture->transport.rx_size, frame,
         frame_size);
  fixture->transport.rx_size += frame_size;
}

static void session_wait_audio(struct session_fixture_s *fixture)
{
  int ret;

  do
    {
      ret = sem_wait(&fixture->source.audio_sent);
    }
  while (ret < 0 && errno == EINTR);

  assert(ret == 0);
}

static uint8_t session_tx_payload_marker(
  const struct fake_transport_s *transport, size_t index)
{
  struct bkvoice_companion_header_s header;
  const uint8_t *payload;
  size_t offset = 0;
  size_t frame_size;

  for (size_t current = 0; current <= index; current++)
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
          assert(header.type == BKVOICE_COMPANION_AUDIO_UP);
          assert(header.payload_len == BKVOICE_CAPTURE_FRAME_BYTES);
          return payload[0];
        }

      offset += frame_size;
    }

  assert(false);
  return 0;
}

static int session_ota_request(
  void *context, uint32_t request_sequence,
  const uint8_t digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES])
{
  struct session_fixture_s *fixture = context;

  assert(digest != NULL);
  fixture->ota_calls++;
  fixture->ota_request_sequence = request_sequence;
  return 0;
}

static void session_fixture_initialize_options(struct session_fixture_s *fixture,
                                               bool ota)
{
  const struct bkvoice_ptt_worker_config_s worker_config =
  {
    .stack_size = 0,
    .priority = 0,
    .join_timeout_ms = 1000,
  };
  const struct bkvoice_turn_limits_s turn_limits =
  {
    .capture_timeout_ms = 1000,
    .waiting_tts_timeout_ms = 1000,
    .playback_timeout_ms = 1000,
    .audio_frame_bytes = BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
  };
  struct bkvoice_session_config_s session_config =
  {
    .gateway =
    {
      .io_timeout_ms = 50,
    },
    .initial_downlink_credit = BKVOICE_COMPANION_AUDIO_FRAME_BYTES,
  };
  struct bkvoice_session_snapshot_s snapshot;

  memset(fixture, 0, sizeof(*fixture));
  if (ota)
    {
      session_config.gateway.ota_request = session_ota_request;
      session_config.gateway.ota_context = fixture;
    }
  fake_transport_initialize(&fixture->transport);
  fixture->clock.now_ms = 100;
  assert(sem_init(&fixture->source.audio_sent, 0, 0) == 0);
  assert(sem_init(&fixture->source.wake_reader, 0, 0) == 0);
  assert(bkvoice_ptt_initialize(
           &fixture->ptt, &g_audio_ops, &fixture->audio,
           &g_session_source_ops, &fixture->source, &turn_limits,
           &worker_config, 7) == 0);
  assert(bkvoice_session_initialize(
           &fixture->session, &fixture->ptt, &g_transport_ops,
           &fixture->transport, fake_now_ms, &fixture->clock,
           &session_config, 7) == 0);
  assert(bkvoice_session_connect(&fixture->session, 500) == 0);
  bkvoice_session_snapshot(&fixture->session, &snapshot);
  fixture->session_id = snapshot.gateway.session_id;
  assert(fixture->session_id == 1);
  assert(snapshot.connected && !snapshot.ready);

  session_enqueue_server(fixture, BKVOICE_COMPANION_WELCOME, 0, 0,
                         NULL, 0);
  struct bkvoice_gateway_frame_s welcome;
  assert(bkvoice_gateway_receive_frame(&fixture->session.gateway,
                                        &welcome, 600) == 0);
  bkvoice_session_snapshot(&fixture->session, &snapshot);
  assert(snapshot.connected && !snapshot.ready);
  assert(!snapshot.ptt.session_open && !fixture->source.attached);
  assert(bkvoice_session_dispatch_frame(&fixture->session, &welcome) == 0);
  bkvoice_session_snapshot(&fixture->session, &snapshot);
  assert(snapshot.ready && snapshot.ptt.session_open);
}

static void session_fixture_initialize(struct session_fixture_s *fixture)
{
  session_fixture_initialize_options(fixture, false);
}

static void session_start_capture(struct session_fixture_s *fixture,
                                  struct bkvoice_turn_token_s *token)
{
  uint8_t window[sizeof(uint32_t)];

  test_put_be32(window, BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  session_enqueue_server(fixture, BKVOICE_COMPANION_WINDOW_UPDATE, 0, 0,
                         window, sizeof(window));
  assert(bkvoice_session_receive_one(&fixture->session, 601) == 0);
  assert(bkvoice_session_ptt_down(&fixture->session,
                                  fixture->clock.now_ms, token) == 0);
  session_wait_audio(fixture);
  assert(bkvoice_session_ptt_up(&fixture->session,
                                fixture->clock.now_ms + 20) == 0);
}

static bool session_playback_done;

static void test_session_prefill_order(void)
{
  static const uint8_t expected_types[] =
  {
    BKVOICE_COMPANION_HELLO,
    BKVOICE_COMPANION_WINDOW_UPDATE,
    BKVOICE_COMPANION_TURN_START,
    BKVOICE_COMPANION_AUDIO_UP,
    BKVOICE_COMPANION_AUDIO_UP,
    BKVOICE_COMPANION_AUDIO_UP,
    BKVOICE_COMPANION_TURN_END,
  };
  struct bkvoice_session_snapshot_s snapshot;
  struct bkvoice_turn_token_s token;
  struct session_prefill_s prefill;
  struct session_fixture_s fixture;
  uint8_t window[sizeof(uint32_t)];

  session_fixture_initialize(&fixture);
  memset(&prefill, 0, sizeof(prefill));
  prefill.source = &fixture.source;
  test_put_be32(window, 3u * BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  session_enqueue_server(&fixture, BKVOICE_COMPANION_WINDOW_UPDATE, 0, 0,
                         window, sizeof(window));
  assert(bkvoice_session_receive_one(&fixture.session, 601) == 0);
  assert(bkvoice_session_ptt_down_prefill(
           &fixture.session, fixture.clock.now_ms, session_prefill_read,
           &prefill, 2, session_live_observe, &prefill, &token) == 0);
  session_wait_audio(&fixture);
  assert(bkvoice_session_ptt_up(&fixture.session,
                                fixture.clock.now_ms + 20) == 0);
  assert(fixture.transport.tx_size > 0);
  assert(fixture.session.gateway.tx_frames == sizeof(expected_types));
  for (size_t index = 0; index < sizeof(expected_types); index++)
    {
      assert(fake_tx_header(&fixture.transport, index).type ==
             expected_types[index]);
    }

  assert(session_tx_payload_marker(&fixture.transport, 3) == 0xa0);
  assert(session_tx_payload_marker(&fixture.transport, 4) == 0xa1);
  assert(session_tx_payload_marker(&fixture.transport, 5) == 0x51);
  assert(prefill.live_calls == 1 && prefill.live_marker == 0x51);
  bkvoice_session_snapshot(&fixture.session, &snapshot);
  assert(snapshot.ptt.capture.frames_sent == 3);
  assert(snapshot.ptt.capture.prefill_frames_sent == 2);
  assert(snapshot.ptt.turn.state == BKVOICE_TURN_WAITING_TTS);

  assert(bkvoice_session_interrupt(&fixture.session) == 0);
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_uninitialize(&fixture.session) == 0);
  assert(bkvoice_ptt_uninitialize(&fixture.ptt) == 0);
  assert(sem_destroy(&fixture.source.audio_sent) == 0);
  assert(sem_destroy(&fixture.source.wake_reader) == 0);
}

static int session_async_drain(void *context)
{
  (void)context;
  return -EINPROGRESS;
}

static int session_async_result(void *context)
{
  (void)context;
  return session_playback_done ? 0 : -EAGAIN;
}

static void test_session_single_turn_and_remote_cancel(void)
{
  struct bkvoice_session_snapshot_s snapshot;
  struct bkvoice_turn_token_s first;
  struct bkvoice_turn_token_s second;
  struct bkvoice_companion_header_s header;
  struct session_fixture_s fixture;
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  size_t index;
  static const uint8_t expected_types[] =
  {
    BKVOICE_COMPANION_HELLO,
    BKVOICE_COMPANION_WINDOW_UPDATE,
    BKVOICE_COMPANION_TURN_START,
    BKVOICE_COMPANION_AUDIO_UP,
    BKVOICE_COMPANION_TURN_END,
  };

  memset(audio, 0x61, sizeof(audio));
  session_fixture_initialize(&fixture);
  fixture.ptt.turn.ops.dac_drain = session_async_drain;
  fixture.ptt.turn.ops.dac_result = session_async_result;
  session_playback_done = false;
  session_start_capture(&fixture, &first);
  assert(fixture.audio.mic_release_calls == 1);

  session_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_START,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC,
                         first.turn_id, NULL, 0);
  assert(bkvoice_session_receive_one(&fixture.session, 700) == 0);
  for (index = 0; index < 6; index++)
    {
      session_enqueue_server(&fixture, BKVOICE_COMPANION_AUDIO_DOWN,
                             BKVOICE_COMPANION_FLAG_SYNTHETIC,
                             first.turn_id, audio, sizeof(audio));
      assert(bkvoice_session_receive_one(&fixture.session,
                                         701 + index) == 0);
      assert(fake_tx_window_credit(&fixture.transport, 5 + index) ==
             sizeof(audio));
    }

  session_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_END,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC,
                         first.turn_id, NULL, 0);
  assert(bkvoice_session_receive_one(&fixture.session, 707) == 0);
  size_t before_complete = fixture.transport.tx_size;
  assert(bkvoice_session_poll(&fixture.session) == 0);
  assert(fixture.transport.tx_size == before_complete);
  assert(fixture.ptt.turn.state == BKVOICE_TURN_DRAINING);
  session_playback_done = true;
  assert(bkvoice_session_poll(&fixture.session) == 0);
  header = fake_tx_header(&fixture.transport,
                          fixture.session.gateway.tx_frames - 1);
  assert(header.type == BKVOICE_COMPANION_ACK &&
         header.turn_id == first.turn_id);
  before_complete = fixture.transport.tx_size;
  assert(bkvoice_session_poll(&fixture.session) == 0);
  assert(fixture.transport.tx_size == before_complete);
  bkvoice_session_snapshot(&fixture.session, &snapshot);
  assert(snapshot.ready && snapshot.ptt.turn.state == BKVOICE_TURN_IDLE);
  assert(fixture.audio.dac_write_calls == 6);
  assert(fixture.audio.dac_bytes == sizeof(audio) * 6u);
  assert(fixture.audio.dac_release_calls == 1);

  /* A late remote cancel after normal completion must not disconnect or
   * release the already closed audio path twice.
   */

  session_enqueue_server(&fixture, BKVOICE_COMPANION_CANCEL, 0,
                         first.turn_id, NULL, 0);
  assert(bkvoice_session_receive_one(&fixture.session, 708) == 0);
  bkvoice_session_snapshot(&fixture.session, &snapshot);
  assert(snapshot.ready && !snapshot.terminal_pending);
  assert(snapshot.ptt.turn.state == BKVOICE_TURN_IDLE);
  assert(fixture.audio.dac_release_calls == 1);

  for (index = 0; index < sizeof(expected_types); index++)
    {
      header = fake_tx_header(&fixture.transport, index);
      assert(header.type == expected_types[index]);
    }

  header = fake_tx_header(&fixture.transport,
                          fixture.session.gateway.tx_frames - 1);
  assert(header.type == BKVOICE_COMPANION_ACK);
  assert(header.session_id == first.session_id &&
         header.turn_id == first.turn_id);

  session_start_capture(&fixture, &second);
  session_enqueue_server(&fixture, BKVOICE_COMPANION_CANCEL, 0,
                         second.turn_id, NULL, 0);
  struct bkvoice_gateway_frame_s cancel;
  assert(bkvoice_gateway_receive_frame(&fixture.session.gateway,
                                        &cancel, 800) == 0);
  bkvoice_session_snapshot(&fixture.session, &snapshot);
  assert(!snapshot.terminal_pending && snapshot.ready);
  assert(snapshot.ptt.turn.state == BKVOICE_TURN_WAITING_TTS);
  size_t before_cancel_tx = fixture.transport.tx_size;
  assert(bkvoice_session_dispatch_frame(&fixture.session, &cancel) == 0);
  bkvoice_session_snapshot(&fixture.session, &snapshot);
  assert(snapshot.ready && !snapshot.terminal_pending);
  assert(snapshot.ptt.turn.state == BKVOICE_TURN_IDLE);
  assert(fixture.source.interrupts == 2);
  assert(fixture.source.detaches == 2);
  assert(!fixture.source.attached && !snapshot.ptt.worker_joinable);
  assert(fixture.transport.tx_size > before_cancel_tx);
  header = fake_tx_header(&fixture.transport,
                          fixture.session.gateway.tx_frames - 1);
  assert(header.type == BKVOICE_COMPANION_ACK);
  assert(header.session_id == second.session_id &&
         header.turn_id == second.turn_id);

  assert(bkvoice_session_interrupt(&fixture.session) == 0);
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_uninitialize(&fixture.session) == 0);
  assert(bkvoice_ptt_uninitialize(&fixture.ptt) == 0);
  assert(sem_destroy(&fixture.source.audio_sent) == 0);
  assert(sem_destroy(&fixture.source.wake_reader) == 0);
}

static int session_fail_dac_stop(void *context)
{
  (void)context;
  return -EIO;
}

static void test_cancel_cleanup_failure_has_no_ack(void)
{
  struct session_fixture_s fixture;
  struct bkvoice_turn_token_s token;
  size_t before;

  session_fixture_initialize(&fixture);
  session_start_capture(&fixture, &token);
  session_enqueue_server(&fixture, BKVOICE_COMPANION_TTS_START,
                         BKVOICE_COMPANION_FLAG_SYNTHETIC,
                         token.turn_id, NULL, 0);
  assert(bkvoice_session_receive_one(&fixture.session, 700) == 0);
  fixture.ptt.turn.ops.dac_stop = session_fail_dac_stop;
  before = fixture.transport.tx_size;
  session_enqueue_server(&fixture, BKVOICE_COMPANION_CANCEL, 0,
                         token.turn_id, NULL, 0);
  assert(bkvoice_session_receive_one(&fixture.session, 701) == -EIO);
  assert(fixture.transport.tx_size == before);
  fixture.ptt.turn.ops.dac_stop = fake_dac_stop;
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_uninitialize(&fixture.session) == 0);
  assert(bkvoice_ptt_uninitialize(&fixture.ptt) == 0);
  assert(sem_destroy(&fixture.source.audio_sent) == 0);
  assert(sem_destroy(&fixture.source.wake_reader) == 0);
}

static void test_remote_cancel_before_local_release_is_idempotent(void)
{
  struct bkvoice_session_snapshot_s snapshot;
  struct bkvoice_turn_token_s first;
  struct bkvoice_turn_token_s second;
  struct session_fixture_s fixture;
  uint8_t window[sizeof(uint32_t)];

  session_fixture_initialize(&fixture);
  test_put_be32(window, BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  session_enqueue_server(&fixture, BKVOICE_COMPANION_WINDOW_UPDATE, 0, 0,
                         window, sizeof(window));
  assert(bkvoice_session_receive_one(&fixture.session, 601) == 0);
  assert(bkvoice_session_ptt_down(&fixture.session,
                                  fixture.clock.now_ms, &first) == 0);
  session_wait_audio(&fixture);

  session_enqueue_server(&fixture, BKVOICE_COMPANION_CANCEL, 0,
                         first.turn_id, NULL, 0);
  assert(bkvoice_session_receive_one(&fixture.session, 700) == 0);
  bkvoice_session_snapshot(&fixture.session, &snapshot);
  assert(snapshot.connected && snapshot.ready &&
         snapshot.ptt.turn.state == BKVOICE_TURN_IDLE);
  assert(!snapshot.ptt.worker_joinable && !fixture.source.attached);

  /* The local release can cross the already-completed remote cancellation. */

  assert(bkvoice_session_ptt_up(&fixture.session,
                                fixture.clock.now_ms + 20) == 0);
  bkvoice_session_snapshot(&fixture.session, &snapshot);
  assert(snapshot.connected && snapshot.ready && snapshot.last_error == 0);

  /* The same connection must accept the next complete capture. */

  test_put_be32(window, BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  session_enqueue_server(&fixture, BKVOICE_COMPANION_WINDOW_UPDATE, 0, 0,
                         window, sizeof(window));
  assert(bkvoice_session_receive_one(&fixture.session, 701) == 0);
  assert(bkvoice_session_ptt_down(&fixture.session,
                                  fixture.clock.now_ms + 30, &second) == 0);
  assert(second.turn_id == first.turn_id + 1);
  session_wait_audio(&fixture);
  assert(bkvoice_session_ptt_up(&fixture.session,
                                fixture.clock.now_ms + 50) == 0);

  assert(bkvoice_session_interrupt(&fixture.session) == 0);
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_uninitialize(&fixture.session) == 0);
  assert(bkvoice_ptt_uninitialize(&fixture.ptt) == 0);
  assert(sem_destroy(&fixture.source.audio_sent) == 0);
  assert(sem_destroy(&fixture.source.wake_reader) == 0);
}

static void test_session_split_reconnect(void)
{
  struct session_fixture_s fixture;
  struct bkvoice_session_snapshot_s before;
  struct bkvoice_session_snapshot_s after;
  struct bkvoice_gateway_frame_s old_frame;
  struct bkvoice_gateway_frame_s old_error;
  struct bkvoice_gateway_frame_s welcome;
  uint8_t window[4];

  session_fixture_initialize(&fixture);
  test_put_be32(window, BKVOICE_COMPANION_AUDIO_FRAME_BYTES);
  session_enqueue_server(&fixture, BKVOICE_COMPANION_WINDOW_UPDATE,
                         0, 0, window, sizeof(window));
  assert(bkvoice_gateway_receive_frame(&fixture.session.gateway,
                                        &old_frame, 700) == 0);
  fixture.transport.empty_recv_error = -ETIMEDOUT;
  assert(bkvoice_gateway_receive_frame(&fixture.session.gateway,
                                        &old_error, 701) == -ETIMEDOUT);
  bkvoice_session_snapshot(&fixture.session, &before);
  assert(before.ready && !before.terminal_pending && !before.gateway.faulted);
  assert(bkvoice_session_interrupt(&fixture.session) == 0);
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_connect(&fixture.session, 800) == 0);
  bkvoice_session_snapshot(&fixture.session, &before);
  fixture.session_id = before.gateway.session_id;
  fixture.server_sequence = 0;
  assert(!before.ready);
  assert(bkvoice_session_dispatch_frame(&fixture.session, &old_frame) ==
         -ESTALE);
  assert(bkvoice_session_dispatch_frame(&fixture.session, &old_error) ==
         -ESTALE);
  bkvoice_session_snapshot(&fixture.session, &after);
  assert(!after.ready && !after.terminal_pending && !after.gateway.faulted);
  assert(after.last_error == before.last_error);

  session_enqueue_server(&fixture, BKVOICE_COMPANION_WELCOME, 0, 0, NULL, 0);
  assert(bkvoice_gateway_receive_frame(&fixture.session.gateway,
                                        &welcome, 801) == 0);
  bkvoice_session_snapshot(&fixture.session, &after);
  assert(!after.ready);
  assert(bkvoice_session_dispatch_frame(&fixture.session, &welcome) == 0);
  assert(bkvoice_session_dispatch_frame(&fixture.session, &old_error) ==
         -ESTALE);
  bkvoice_session_snapshot(&fixture.session, &after);
  assert(after.ready && !after.terminal_pending && !after.gateway.faulted);
  assert(after.last_error == 0 && after.ptt.session_open);
  assert(bkvoice_session_interrupt(&fixture.session) == 0);
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_uninitialize(&fixture.session) == 0);
  assert(bkvoice_ptt_uninitialize(&fixture.ptt) == 0);
  assert(sem_destroy(&fixture.source.audio_sent) == 0);
  assert(sem_destroy(&fixture.source.wake_reader) == 0);
}

static void test_session_timeout_poll(void)
{
  struct session_fixture_s fixture;
  struct bkvoice_turn_token_s token;

  session_fixture_initialize(&fixture);
  assert(bkvoice_session_timeout(&fixture.session, fixture.clock.now_ms) == 0);
  assert(fixture.session.ready && fixture.session.last_error == 0);
  session_start_capture(&fixture, &token);
  assert(bkvoice_session_timeout(&fixture.session,
                                fixture.ptt.turn.deadline_ms - 1) == 0);
  assert(fixture.ptt.turn.state == BKVOICE_TURN_WAITING_TTS);
  assert(bkvoice_session_timeout(&fixture.session,
                                fixture.ptt.turn.deadline_ms) == 0);
  assert(fixture.ptt.last_error == -ETIMEDOUT);
  assert(fixture.ptt.turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_uninitialize(&fixture.session) == 0);
  assert(bkvoice_ptt_uninitialize(&fixture.ptt) == 0);
  assert(sem_destroy(&fixture.source.audio_sent) == 0);
  assert(sem_destroy(&fixture.source.wake_reader) == 0);
}

static void test_session_ota_adapter(void)
{
  struct session_fixture_s fixture;
  struct bkvoice_companion_header_s header;
  const uint8_t *payload;
  uint8_t digest[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES];
  size_t offset;

  memset(digest, 0x7b, sizeof(digest));
  session_fixture_initialize_options(&fixture, true);
  assert(test_get_be32(fixture.transport.tx + BKVOICE_COMPANION_HEADER_BYTES) &
         BKVOICE_COMPANION_CAP_OTA);
  offset = fixture.transport.tx_size;
  session_enqueue_server(&fixture, BKVOICE_COMPANION_OTA_REQUEST, 0, 0,
                         digest, sizeof(digest));
  assert(bkvoice_session_receive_one(&fixture.session, 700) == 0);
  assert(fixture.ota_calls == 1 && fixture.ota_request_sequence ==
         fixture.server_sequence);
  assert(bkvoice_companion_decode(fixture.transport.tx + offset,
                                  fixture.transport.tx_size - offset,
                                  &header, &payload) == 0);
  assert(header.type == BKVOICE_COMPANION_OTA_REPORT &&
         test_get_be32(payload) == fixture.ota_request_sequence);
  assert(bkvoice_gateway_report_ota(&fixture.session.gateway,
                                     fixture.ota_request_sequence, digest,
                                     -EIO, BKVOICE_COMPANION_OTA_FAILED,
                                     0) == 0);
  assert(bkvoice_session_disconnect(&fixture.session, -ENOTCONN) == 0);
  assert(bkvoice_session_uninitialize(&fixture.session) == 0);
  assert(bkvoice_ptt_uninitialize(&fixture.ptt) == 0);
  assert(sem_destroy(&fixture.source.audio_sent) == 0);
  assert(sem_destroy(&fixture.source.wake_reader) == 0);
}

int main(void)
{
  assert(bkvoice_gateway_component_tests() == 0);
  test_session_prefill_order();
  test_session_single_turn_and_remote_cancel();
  test_cancel_cleanup_failure_has_no_ack();
  test_remote_cancel_before_local_release_is_idempotent();
  test_session_split_reconnect();
  test_session_timeout_poll();
  test_session_ota_adapter();
  puts("BKVOICE_SESSION_HOST_PASS");
  return 0;
}
