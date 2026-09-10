/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetic ownership coverage only; this does not calibrate target VAD. */

#include "bk7258_voice_wake_window.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

static const struct bkvoice_wake_window_policy_s g_policy =
{
  .minimum_speech_mean_abs = 100,
  .speech_to_noise_q8 = 512,
  .speech_confirm_frames = 2,
  .silence_end_frames = 3,
  .no_speech_frames = 5,
  .maximum_turn_frames = 10,
};

static void fill_frame(int16_t *pcm, int16_t value)
{
  for (size_t i = 0; i < BKVOICE_WAKE_FRAME_SAMPLES; i++)
    {
      pcm[i] = value;
    }
}

static void test_pre_roll_and_endpoint(void)
{
  struct bkvoice_wake_window_snapshot_s snapshot;
  struct bkvoice_wake_window_s window;
  int16_t storage[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  int16_t frame[BKVOICE_WAKE_FRAME_SAMPLES];
  enum bkvoice_wake_window_event_e event;

  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &g_policy) == 0);
  for (unsigned int i = 0; i < 60; i++)
    {
      fill_frame(frame, (int16_t)i);
      assert(bkvoice_wake_window_observe(&window, frame,
                                         BKVOICE_WAKE_FRAME_SAMPLES,
                                         (uint64_t)(i + 1u) * 20u) == 0);
    }

  assert(bkvoice_wake_window_trigger(&window, 1200) == 0);
  assert(bkvoice_wake_window_pre_roll_frames(&window) == 50);
  for (unsigned int i = 0; i < 50; i++)
    {
      memset(frame, 0, sizeof(frame));
      assert(bkvoice_wake_window_read_pre_roll(&window, i, frame) == 0);
      for (size_t sample = 0; sample < BKVOICE_WAKE_FRAME_SAMPLES; sample++)
        {
          assert(frame[sample] == (int16_t)(i + 10u));
        }
    }

  fill_frame(frame, 400);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 1220,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_NONE);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 1240,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_SPEECH_STARTED);

  fill_frame(frame, 0);
  for (unsigned int i = 0; i < 2; i++)
    {
      assert(bkvoice_wake_window_feed(&window, frame,
                                       BKVOICE_WAKE_FRAME_SAMPLES,
                                       1260u + i * 20u, &event) == 0);
      assert(event == BKVOICE_WAKE_EVENT_NONE);
    }

  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 1300,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_END_OF_SPEECH);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 1320,
                                   &event) == -EALREADY);

  bkvoice_wake_window_snapshot(&window, &snapshot);
  assert(snapshot.state == BKVOICE_WAKE_WINDOW_COMPLETE);
  assert(snapshot.pre_roll_frames == 50 && snapshot.post_trigger_frames == 5);
  assert(snapshot.trigger_ms == 1200 && snapshot.last_frame_ms == 1300);
  bkvoice_wake_window_uninitialize(&window);
  for (size_t i = 0; i < BKVOICE_WAKE_PRE_ROLL_SAMPLES; i++)
    {
      assert(storage[i] == 0);
    }
}

static void test_no_speech_and_maximum_duration(void)
{
  struct bkvoice_wake_window_policy_s policy = g_policy;
  struct bkvoice_wake_window_s window;
  int16_t storage[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  int16_t frame[BKVOICE_WAKE_FRAME_SAMPLES];
  enum bkvoice_wake_window_event_e event;

  fill_frame(frame, 10);
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &policy) == 0);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  assert(bkvoice_wake_window_trigger(&window, 20) == 0);
  for (unsigned int i = 0; i < 4; i++)
    {
      assert(bkvoice_wake_window_feed(&window, frame,
                                       BKVOICE_WAKE_FRAME_SAMPLES,
                                       40u + i * 20u, &event) == 0);
      assert(event == BKVOICE_WAKE_EVENT_NONE);
    }

  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 120,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_NO_SPEECH);

  bkvoice_wake_window_uninitialize(&window);
  policy.maximum_turn_frames = 5;
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &policy) == 0);
  fill_frame(frame, 10);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  assert(bkvoice_wake_window_trigger(&window, 20) == 0);
  fill_frame(frame, 500);
  for (unsigned int i = 0; i < 5; i++)
    {
      assert(bkvoice_wake_window_feed(&window, frame,
                                       BKVOICE_WAKE_FRAME_SAMPLES,
                                       40u + i * 20u, &event) == 0);
      if (i == 1)
        {
          assert(event == BKVOICE_WAKE_EVENT_SPEECH_STARTED);
        }
      else if (i == 4)
        {
          assert(event == BKVOICE_WAKE_EVENT_MAX_DURATION);
        }
      else
        {
          assert(event == BKVOICE_WAKE_EVENT_NONE);
        }
    }

  bkvoice_wake_window_uninitialize(&window);
}

static void test_event_precedence_and_reset(void)
{
  struct bkvoice_wake_window_policy_s policy = g_policy;
  struct bkvoice_wake_window_s window;
  int16_t storage[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  int16_t frame[BKVOICE_WAKE_FRAME_SAMPLES];
  enum bkvoice_wake_window_event_e event;

  policy.speech_confirm_frames = 2;
  policy.silence_end_frames = 2;
  policy.no_speech_frames = 2;
  policy.maximum_turn_frames = 3;
  fill_frame(frame, 10);
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &policy) == 0);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  assert(bkvoice_wake_window_trigger(&window, 20) == 0);
  fill_frame(frame, 500);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 40,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_NONE);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 60,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_SPEECH_STARTED);

  memset(storage, 0x5a, sizeof(storage));
  bkvoice_wake_window_reset(&window);
  assert(window.state == BKVOICE_WAKE_WINDOW_LISTENING);
  for (size_t i = 0; i < BKVOICE_WAKE_PRE_ROLL_SAMPLES; i++)
    {
      assert(storage[i] == 0);
    }

  policy.speech_confirm_frames = 1;
  policy.silence_end_frames = 2;
  policy.no_speech_frames = 3;
  policy.maximum_turn_frames = 3;
  bkvoice_wake_window_uninitialize(&window);
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &policy) == 0);
  fill_frame(frame, 10);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  assert(bkvoice_wake_window_trigger(&window, 20) == 0);
  fill_frame(frame, 500);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 40,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_SPEECH_STARTED);
  fill_frame(frame, 0);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 60,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_NONE);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 80,
                                   &event) == 0);
  assert(event == BKVOICE_WAKE_EVENT_END_OF_SPEECH);
  bkvoice_wake_window_uninitialize(&window);
}

static void test_timestamp_recovery_and_fault(void)
{
  struct bkvoice_wake_window_s window;
  int16_t storage[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  int16_t frame[BKVOICE_WAKE_FRAME_SAMPLES];
  enum bkvoice_wake_window_event_e event;

  fill_frame(frame, 20);
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &g_policy) == 0);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 60) == 0);
  assert(bkvoice_wake_window_trigger(&window, 60) == 0);
  assert(bkvoice_wake_window_pre_roll_frames(&window) == 1);
  assert(bkvoice_wake_window_feed(&window, frame,
                                   BKVOICE_WAKE_FRAME_SAMPLES, 100,
                                   &event) == -ESTALE);
  assert(window.state == BKVOICE_WAKE_WINDOW_FAULTED);

  bkvoice_wake_window_reset(&window);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) ==
         -ESTALE);
  assert(window.state == BKVOICE_WAKE_WINDOW_FAULTED);
  bkvoice_wake_window_uninitialize(&window);
}

static void test_guards_and_full_scale_sample(void)
{
  struct bkvoice_wake_window_policy_s policy = g_policy;
  struct bkvoice_wake_window_snapshot_s snapshot;
  struct bkvoice_wake_window_s window;
  int16_t storage[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  int16_t frame[BKVOICE_WAKE_FRAME_SAMPLES];

  assert(bkvoice_wake_window_initialize(NULL, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &policy) == -EINVAL);
  policy.speech_to_noise_q8 = 255;
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &policy) == -EINVAL);
  policy = g_policy;
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES - 1u,
                                         &policy) == -EINVAL);
  assert(bkvoice_wake_window_initialize(&window, storage,
                                         BKVOICE_WAKE_PRE_ROLL_SAMPLES,
                                         &policy) == 0);
  fill_frame(frame, INT16_MIN);
  assert(bkvoice_wake_window_observe(&window, frame,
                                     BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  bkvoice_wake_window_snapshot(&window, &snapshot);
  assert(snapshot.last_mean_abs == 32768u);
  assert(bkvoice_wake_window_trigger(&window, 40) == -EINVAL);
  assert(bkvoice_wake_window_trigger(&window, 20) == 0);
  assert(bkvoice_wake_window_read_pre_roll(&window, 1, frame) == -EINVAL);
  bkvoice_wake_window_uninitialize(&window);
}

int main(void)
{
  test_pre_roll_and_endpoint();
  test_no_speech_and_maximum_duration();
  test_event_precedence_and_reset();
  test_timestamp_recovery_and_fault();
  test_guards_and_full_scale_sample();
  return 0;
}
