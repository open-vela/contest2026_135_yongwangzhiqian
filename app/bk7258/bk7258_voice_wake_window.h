/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __APP_BK7258_BK7258_VOICE_WAKE_WINDOW_H
#define __APP_BK7258_BK7258_VOICE_WAKE_WINDOW_H

#include "bk7258_voice_kws_frontend.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BKVOICE_WAKE_FRAME_SAMPLES BKVOICE_KWS_HOP
#define BKVOICE_WAKE_PRE_ROLL_FRAMES \
  (BKVOICE_KWS_RATE / BKVOICE_WAKE_FRAME_SAMPLES)
#define BKVOICE_WAKE_PRE_ROLL_SAMPLES \
  (BKVOICE_WAKE_PRE_ROLL_FRAMES * BKVOICE_WAKE_FRAME_SAMPLES)

enum bkvoice_wake_window_state_e
{
  BKVOICE_WAKE_WINDOW_LISTENING = 0,
  BKVOICE_WAKE_WINDOW_WAITING_SPEECH,
  BKVOICE_WAKE_WINDOW_CAPTURING,
  BKVOICE_WAKE_WINDOW_COMPLETE,
  BKVOICE_WAKE_WINDOW_FAULTED,
};

enum bkvoice_wake_window_event_e
{
  BKVOICE_WAKE_EVENT_NONE = 0,
  BKVOICE_WAKE_EVENT_SPEECH_STARTED,
  BKVOICE_WAKE_EVENT_END_OF_SPEECH,
  BKVOICE_WAKE_EVENT_NO_SPEECH,
  BKVOICE_WAKE_EVENT_MAX_DURATION,
};

struct bkvoice_wake_window_policy_s
{
  uint32_t minimum_speech_mean_abs;
  uint16_t speech_to_noise_q8;
  uint16_t speech_confirm_frames;
  uint16_t silence_end_frames;
  uint16_t no_speech_frames;
  uint16_t maximum_turn_frames;
};

struct bkvoice_wake_window_snapshot_s
{
  enum bkvoice_wake_window_state_e state;
  size_t pre_roll_frames;
  uint32_t noise_mean_abs;
  uint32_t speech_threshold;
  uint32_t last_mean_abs;
  uint32_t post_trigger_frames;
  uint16_t speech_frames;
  uint16_t silence_frames;
  uint64_t last_frame_ms;
  uint64_t trigger_ms;
};

struct bkvoice_wake_window_s
{
  struct bkvoice_wake_window_policy_s policy;
  int16_t *pre_roll;
  size_t pre_roll_samples;
  size_t head_frame;
  size_t valid_frames;
  size_t frozen_start;
  size_t frozen_frames;
  uint32_t noise_mean_abs;
  uint32_t last_mean_abs;
  uint32_t post_trigger_frames;
  uint16_t speech_frames;
  uint16_t silence_frames;
  uint64_t last_frame_ms;
  uint64_t trigger_ms;
  enum bkvoice_wake_window_state_e state;
  int last_error;
  bool timestamp_valid;
};

/* This App-only core opens no recorder and does not perform wake inference.
 * While LISTENING, the single AP audio owner supplies each ordered 20 ms PCM
 * frame after giving the same frame to KWS.  trigger() freezes exactly the
 * available last second so a later Gateway turn can replay it in order.
 * feed() then provides a bounded endpoint decision for live post-trigger PCM.
 * If endpoint conditions collide on one frame, confirmed speech or silence
 * wins over the corresponding timeout; maximum duration ends any remaining
 * active turn.  All API calls must come from that one serialized owner.
 *
 * Thresholds are workload policy and require target calibration.  This core
 * is not a wake-word model and its energy gate must never be used as one.
 */

int bkvoice_wake_window_initialize(
  struct bkvoice_wake_window_s *window,
  int16_t *pre_roll, size_t pre_roll_samples,
  const struct bkvoice_wake_window_policy_s *policy);
void bkvoice_wake_window_uninitialize(struct bkvoice_wake_window_s *window);
void bkvoice_wake_window_reset(struct bkvoice_wake_window_s *window);
int bkvoice_wake_window_observe(struct bkvoice_wake_window_s *window,
                                const int16_t *pcm, size_t samples,
                                uint64_t end_ms);
int bkvoice_wake_window_trigger(struct bkvoice_wake_window_s *window,
                                uint64_t end_ms);
size_t bkvoice_wake_window_pre_roll_frames(
  const struct bkvoice_wake_window_s *window);
int bkvoice_wake_window_read_pre_roll(
  const struct bkvoice_wake_window_s *window, size_t frame,
  int16_t pcm[BKVOICE_WAKE_FRAME_SAMPLES]);
int bkvoice_wake_window_feed(
  struct bkvoice_wake_window_s *window, const int16_t *pcm, size_t samples,
  uint64_t end_ms, enum bkvoice_wake_window_event_e *event);
void bkvoice_wake_window_snapshot(
  const struct bkvoice_wake_window_s *window,
  struct bkvoice_wake_window_snapshot_s *snapshot);

#ifdef __cplusplus
}
#endif
#endif
