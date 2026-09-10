/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_capture.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_voice_capture.h"
#include "bk7258_voice_product.h"

enum fake_source_mode_e
{
  FAKE_SOURCE_NORMAL = 0,
  FAKE_SOURCE_PARTIAL,
  FAKE_SOURCE_CONTINUOUS,
  FAKE_SOURCE_ZERO,
};

struct fake_source_s
{
  struct bkvoice_capture_s *capture;
  enum fake_source_mode_e mode;
  unsigned int read_calls;
  unsigned int attach_calls;
  unsigned int interrupt_calls;
  unsigned int detach_calls;
  int attach_error;
  int interrupt_error;
  int detach_error;
  bool attached;
};

struct fake_sink_s
{
  struct bkvoice_turn_token_s token;
  unsigned int start_calls;
  unsigned int audio_calls;
  unsigned int end_calls;
  unsigned int cancel_calls;
  uint8_t audio_markers[64];
  size_t audio_bytes;
  int cancel_reason;
  int start_error;
  int audio_error;
  int end_error;
  int cancel_error;
};

struct fake_prefill_s
{
  unsigned int read_calls;
  size_t fail_index;
  int error;
};

struct fake_live_observer_s
{
  struct bkvoice_turn_token_s token;
  unsigned int calls;
  uint8_t markers[8];
};

static const struct bkvoice_turn_token_s g_token =
{
  .boot_generation = 7,
  .session_id = 3,
  .turn_id = 9,
  .sequence = 1,
};

static int fake_source_attach(void *context)
{
  struct fake_source_s *source = context;

  source->attach_calls++;
  if (source->attach_error < 0)
    {
      return source->attach_error;
    }

  assert(!source->attached);
  source->attached = true;
  return 0;
}

static ssize_t fake_source_read(void *context, void *pcm, size_t bytes)
{
  struct fake_source_s *source = context;
  size_t chunk;
  int ret;

  assert(source->attached && pcm != NULL && bytes != 0);
  source->read_calls++;

  switch (source->mode)
    {
      case FAKE_SOURCE_NORMAL:
        chunk = source->read_calls == 1 ? 200u :
                source->read_calls == 2 ? 440u :
                BKVOICE_CAPTURE_FRAME_BYTES;
        break;
      case FAKE_SOURCE_PARTIAL:
        chunk = 320u;
        break;
      case FAKE_SOURCE_CONTINUOUS:
        chunk = BKVOICE_CAPTURE_FRAME_BYTES;
        break;
      case FAKE_SOURCE_ZERO:
        return 0;
      default:
        return -EINVAL;
    }

  assert(chunk <= bytes);
  memset(pcm, (int)(0x30u + source->read_calls), chunk);

  if ((source->mode == FAKE_SOURCE_NORMAL && source->read_calls == 3) ||
      source->mode == FAKE_SOURCE_PARTIAL)
    {
      ret = bkvoice_capture_request_stop(source->capture);
      assert(ret == source->interrupt_error);
    }

  return (ssize_t)chunk;
}

static int fake_source_interrupt(void *context)
{
  struct fake_source_s *source = context;

  assert(source->attached);
  source->interrupt_calls++;
  return source->interrupt_error;
}

static int fake_source_detach(void *context)
{
  struct fake_source_s *source = context;

  source->detach_calls++;
  if (source->detach_error < 0)
    {
      return source->detach_error;
    }

  assert(source->attached);
  source->attached = false;
  return 0;
}

static int fake_sink_start(void *context,
                           const struct bkvoice_turn_token_s *token)
{
  struct fake_sink_s *sink = context;

  sink->start_calls++;
  memcpy(&sink->token, token, sizeof(*token));
  return sink->start_error;
}

static int fake_sink_audio(void *context,
                           const struct bkvoice_turn_token_s *token,
                           const uint8_t *pcm, size_t bytes)
{
  struct fake_sink_s *sink = context;

  assert(memcmp(token, &sink->token, sizeof(*token)) == 0);
  assert(pcm != NULL && bytes == BKVOICE_CAPTURE_FRAME_BYTES);
  assert(sink->audio_calls < sizeof(sink->audio_markers));
  sink->audio_markers[sink->audio_calls] = pcm[0];
  sink->audio_calls++;
  sink->audio_bytes += bytes;
  return sink->audio_error;
}

static int fake_prefill_read(
  void *context, size_t frame_index,
  uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
  struct fake_prefill_s *prefill = context;

  assert(prefill != NULL && pcm != NULL);
  assert(frame_index == prefill->read_calls);
  prefill->read_calls++;
  if (prefill->error < 0 && frame_index == prefill->fail_index)
    {
      return prefill->error;
    }

  memset(pcm, (int)(0xa0u + frame_index),
         BKVOICE_CAPTURE_FRAME_BYTES);
  return 0;
}

static void fake_live_observe(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
  struct fake_live_observer_s *observer = context;

  assert(observer != NULL && token != NULL && pcm != NULL);
  assert(memcmp(token, &observer->token, sizeof(*token)) == 0);
  assert(observer->calls < sizeof(observer->markers));
  observer->markers[observer->calls++] = pcm[0];
}

static int fake_sink_end(void *context,
                         const struct bkvoice_turn_token_s *token)
{
  struct fake_sink_s *sink = context;

  assert(memcmp(token, &sink->token, sizeof(*token)) == 0);
  sink->end_calls++;
  return sink->end_error;
}

static int fake_sink_cancel(void *context,
                            const struct bkvoice_turn_token_s *token,
                            int reason)
{
  struct fake_sink_s *sink = context;

  assert(memcmp(token, &sink->token, sizeof(*token)) == 0);
  assert(reason < 0);
  sink->cancel_calls++;
  sink->cancel_reason = reason;
  return sink->cancel_error;
}

static const struct bkvoice_capture_source_ops_s g_source_ops =
{
  .attach = fake_source_attach,
  .read = fake_source_read,
  .interrupt = fake_source_interrupt,
  .detach = fake_source_detach,
};

static const struct bkvoice_capture_sink_ops_s g_sink_ops =
{
  .start = fake_sink_start,
  .audio = fake_sink_audio,
  .end = fake_sink_end,
  .cancel = fake_sink_cancel,
};

static void fake_initialize(struct bkvoice_capture_s *capture,
                            struct fake_source_s *source,
                            struct fake_sink_s *sink,
                            enum fake_source_mode_e mode)
{
  memset(source, 0, sizeof(*source));
  memset(sink, 0, sizeof(*sink));
  source->capture = capture;
  source->mode = mode;
  assert(bkvoice_capture_initialize(capture, &g_source_ops, source,
                                    &g_sink_ops, sink) == 0);
}

static void test_normal_capture(void)
{
  struct bkvoice_capture_snapshot_s snapshot;
  struct bkvoice_capture_s capture;
  struct fake_source_s source;
  struct fake_sink_s sink;

  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_NORMAL);
  assert(strcmp(bkvoice_capture_state_name(capture.state), "idle") == 0);
  assert(bkvoice_capture_start(&capture, &g_token) == 0);
  assert(bkvoice_capture_start(&capture, &g_token) == -EBUSY);
  assert(bkvoice_capture_complete(&capture) == -EPERM);
  assert(bkvoice_capture_cancel(&capture, -ECANCELED) == -EBUSY);

  assert(bkvoice_capture_run(&capture) == 0);
  bkvoice_capture_snapshot(&capture, &snapshot);
  assert(snapshot.state == BKVOICE_CAPTURE_STOPPED);
  assert(snapshot.frames_sent == 2);
  assert(snapshot.bytes_sent == 2u * BKVOICE_CAPTURE_FRAME_BYTES);
  assert(snapshot.partial_bytes_discarded == 0);
  assert(!snapshot.source_attached && snapshot.sink_started);
  assert(snapshot.stop_requested && snapshot.last_error == 0);
  assert(source.attach_calls == 1 && source.interrupt_calls == 1 &&
         source.detach_calls == 1 && !source.attached);
  assert(sink.start_calls == 1 && sink.audio_calls == 2 &&
         sink.audio_bytes == 2u * BKVOICE_CAPTURE_FRAME_BYTES);

  assert(bkvoice_capture_complete(&capture) == 0);
  assert(capture.state == BKVOICE_CAPTURE_IDLE);
  assert(sink.end_calls == 1 && sink.cancel_calls == 0);
  assert(bkvoice_capture_complete(&capture) == -EPERM);
  assert(bkvoice_capture_cancel(&capture, -ECANCELED) == -EALREADY);
}

static void test_partial_frame_is_not_padded(void)
{
  struct bkvoice_capture_snapshot_s snapshot;
  struct bkvoice_capture_s capture;
  struct fake_source_s source;
  struct fake_sink_s sink;

  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_PARTIAL);
  assert(bkvoice_capture_start(&capture, &g_token) == 0);
  assert(bkvoice_capture_run(&capture) == 0);
  bkvoice_capture_snapshot(&capture, &snapshot);
  assert(snapshot.frames_sent == 0 && snapshot.bytes_sent == 0);
  assert(snapshot.partial_bytes_discarded == 320u);
  assert(sink.audio_calls == 0);
  assert(bkvoice_capture_complete(&capture) == 0);
}

static void test_prefill_precedes_live_audio(void)
{
  static const uint8_t expected[] = {0xa0, 0xa1, 0xa2, 0x31, 0x33};
  static const uint8_t expected_live[] = {0x31, 0x33};
  struct bkvoice_capture_snapshot_s snapshot;
  struct bkvoice_capture_s capture;
  struct fake_live_observer_s observer;
  struct fake_prefill_s prefill;
  struct fake_source_s source;
  struct fake_sink_s sink;

  memset(&prefill, 0, sizeof(prefill));
  memset(&observer, 0, sizeof(observer));
  observer.token = g_token;
  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_NORMAL);
  assert(bkvoice_capture_start(&capture, &g_token) == 0);
  assert(bkvoice_capture_prefill(&capture, fake_prefill_read, &prefill,
                                 3) == 0);
  bkvoice_capture_snapshot(&capture, &snapshot);
  assert(snapshot.frames_sent == 3 && snapshot.prefill_frames_sent == 3);
  assert(snapshot.bytes_sent == 3u * BKVOICE_CAPTURE_FRAME_BYTES);
  assert(!snapshot.run_started && source.read_calls == 0);
  assert(bkvoice_capture_prefill(&capture, fake_prefill_read, &prefill,
                                 1) == -EBUSY);
  assert(bkvoice_capture_set_live_observer(
           &capture, fake_live_observe, &observer) == 0);
  assert(bkvoice_capture_set_live_observer(
           &capture, fake_live_observe, &observer) == -EALREADY);

  assert(bkvoice_capture_run(&capture) == 0);
  bkvoice_capture_snapshot(&capture, &snapshot);
  assert(snapshot.frames_sent == 5 && snapshot.prefill_frames_sent == 3);
  assert(snapshot.run_started && sink.audio_calls == 5);
  assert(memcmp(sink.audio_markers, expected, sizeof(expected)) == 0);
  assert(observer.calls == sizeof(expected_live));
  assert(memcmp(observer.markers, expected_live,
                sizeof(expected_live)) == 0);
  assert(bkvoice_capture_complete(&capture) == 0);
  assert(capture.live_observer == NULL &&
         capture.live_observer_context == NULL);
}

static void test_prefill_failure_is_cancelled(void)
{
  struct bkvoice_capture_s capture;
  struct fake_prefill_s prefill;
  struct fake_source_s source;
  struct fake_sink_s sink;

  memset(&prefill, 0, sizeof(prefill));
  prefill.fail_index = 1;
  prefill.error = -EIO;
  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_NORMAL);
  assert(bkvoice_capture_start(&capture, &g_token) == 0);
  assert(bkvoice_capture_prefill(&capture, fake_prefill_read, &prefill,
                                 2) == -EIO);
  assert(capture.state == BKVOICE_CAPTURE_IDLE);
  assert(capture.frames_sent == 1 && capture.prefill_frames_sent == 1);
  assert(!source.attached && source.interrupt_calls == 1 &&
         source.detach_calls == 1 && sink.audio_calls == 1);
  assert(sink.cancel_calls == 1 && sink.cancel_reason == -EIO);
  assert(capture.last_error == -EIO);
}

static void test_sink_failure_is_cancelled(void)
{
  struct bkvoice_capture_s capture;
  struct fake_live_observer_s observer;
  struct fake_source_s source;
  struct fake_sink_s sink;

  memset(&observer, 0, sizeof(observer));
  observer.token = g_token;
  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_CONTINUOUS);
  sink.audio_error = -ENETDOWN;
  assert(bkvoice_capture_start(&capture, &g_token) == 0);
  assert(bkvoice_capture_set_live_observer(
           &capture, fake_live_observe, &observer) == 0);
  assert(bkvoice_capture_run(&capture) == -ENETDOWN);
  assert(observer.calls == 0);
  assert(capture.state == BKVOICE_CAPTURE_FAULTED);
  assert(!capture.source_attached && capture.sink_started);
  assert(bkvoice_capture_complete(&capture) == -EPERM);
  assert(bkvoice_capture_cancel(&capture, -ENETDOWN) == 0);
  assert(capture.state == BKVOICE_CAPTURE_IDLE);
  assert(sink.cancel_calls == 1 && sink.cancel_reason == -ENETDOWN);
}

static void test_start_and_source_failures(void)
{
  struct bkvoice_capture_s capture;
  struct fake_source_s source;
  struct fake_sink_s sink;

  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_NORMAL);
  source.attach_error = -ENODEV;
  assert(bkvoice_capture_start(&capture, &g_token) == -ENODEV);
  assert(capture.state == BKVOICE_CAPTURE_IDLE && !source.attached);

  source.attach_error = 0;
  sink.start_error = -ECONNREFUSED;
  assert(bkvoice_capture_start(&capture, &g_token) == -ECONNREFUSED);
  assert(capture.state == BKVOICE_CAPTURE_IDLE && !source.attached);
  assert(source.attach_calls == 2 && source.detach_calls == 1);
  assert(sink.cancel_calls == 1 &&
         sink.cancel_reason == -ECONNREFUSED);

  sink.cancel_error = -EAGAIN;
  assert(bkvoice_capture_start(&capture, &g_token) == -ECONNREFUSED);
  assert(capture.state == BKVOICE_CAPTURE_FAULTED &&
         !capture.source_attached && capture.sink_started);
  sink.cancel_error = 0;
  assert(bkvoice_capture_cancel(&capture, -ECONNREFUSED) == 0);
  assert(capture.state == BKVOICE_CAPTURE_IDLE && sink.cancel_calls == 3);
}

static void test_interrupt_and_zero_read_fail_closed(void)
{
  struct bkvoice_capture_s capture;
  struct fake_source_s source;
  struct fake_sink_s sink;

  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_PARTIAL);
  source.interrupt_error = -EIO;
  assert(bkvoice_capture_start(&capture, &g_token) == 0);
  assert(bkvoice_capture_run(&capture) == -EIO);
  assert(capture.state == BKVOICE_CAPTURE_FAULTED);
  assert(bkvoice_capture_cancel(&capture, -EIO) == 0);

  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_ZERO);
  assert(bkvoice_capture_start(&capture, &g_token) == 0);
  assert(bkvoice_capture_run(&capture) == -EPIPE);
  assert(bkvoice_capture_cancel(&capture, -EPIPE) == 0);
}

static void test_guards_and_product_identity(void)
{
  struct bkvoice_capture_source_ops_s bad_source = g_source_ops;
  struct bkvoice_capture_s capture;
  struct fake_source_s source;
  struct fake_sink_s sink;
  struct bkvoice_turn_token_s bad_token = g_token;

  memset(&source, 0, sizeof(source));
  memset(&sink, 0, sizeof(sink));
  bad_source.interrupt = NULL;
  assert(bkvoice_capture_initialize(NULL, &g_source_ops, &source,
                                    &g_sink_ops, &sink) == -EINVAL);
  assert(bkvoice_capture_initialize(&capture, &bad_source, &source,
                                    &g_sink_ops, &sink) == -EINVAL);
  assert(bkvoice_capture_initialize(&capture, &g_source_ops, NULL,
                                    &g_sink_ops, &sink) == -EINVAL);

  fake_initialize(&capture, &source, &sink, FAKE_SOURCE_NORMAL);
  bad_token.turn_id = 0;
  assert(bkvoice_capture_start(&capture, &bad_token) == -EINVAL);
  assert(bkvoice_capture_request_stop(&capture) == -EPERM);
  assert(bkvoice_capture_run(&capture) == -EPERM);
  assert(bkvoice_capture_set_live_observer(
           &capture, fake_live_observe, NULL) == -EBUSY);
  assert(bkvoice_capture_set_live_observer(&capture, NULL, NULL) ==
         -EINVAL);
  assert(bkvoice_capture_prefill(&capture, fake_prefill_read, NULL, 1) ==
         -EBUSY);
  assert(bkvoice_capture_prefill(&capture, NULL, NULL, 1) == -EINVAL);
  assert(bkvoice_capture_prefill(
           &capture, fake_prefill_read, NULL,
           BKVOICE_CAPTURE_MAX_PREFILL_FRAMES + 1u) == -EINVAL);
  assert(strcmp(bkvoice_capture_state_name((enum bkvoice_capture_state_e)99),
                "invalid") == 0);

  assert(strcmp(BKVOICE_PRODUCT_PERSONA_ID, "shaniu") == 0);
  assert(strcmp(BKVOICE_PRODUCT_DISPLAY_NAME,
                "\xe5\x82\xbb\xe5\xa6\x9e") == 0);
  assert(strcmp(BKVOICE_PRODUCT_ROLE, "ai-companion") == 0);
  assert(strcmp(BKVOICE_PRODUCT_DISCLOSURE, "synthetic-ai") == 0);
}

int main(void)
{
  test_normal_capture();
  test_partial_frame_is_not_padded();
  test_prefill_precedes_live_audio();
  test_prefill_failure_is_cancelled();
  test_sink_failure_is_cancelled();
  test_start_and_source_failures();
  test_interrupt_and_zero_read_fail_closed();
  test_guards_and_product_identity();
  puts("BKVOICE_CAPTURE_HOST_PASS");
  return 0;
}
