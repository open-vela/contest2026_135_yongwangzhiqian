/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VOICE_WAKE_LISTENER_H
#define __APP_BK7258_VOICE_WAKE_LISTENER_H

#include "bk7258_voice_kws.h"
#include "bk7258_voice_wake_window.h"
#include "bk7258_voice_turn.h"
#include "bk7258_voice_capture.h"

#include <pthread.h>
#include <semaphore.h>

enum bkvoice_wake_listener_state_e
{
  BKVOICE_WAKE_LISTENER_IDLE = 0,
  BKVOICE_WAKE_LISTENER_RUNNING,
  BKVOICE_WAKE_LISTENER_STOPPING,
  BKVOICE_WAKE_LISTENER_TRIGGERED,
  BKVOICE_WAKE_LISTENER_FAULTED,
};

struct bkvoice_wake_listener_status_s
{
  enum bkvoice_wake_listener_state_e state;
  int result;
  float wake_score;
  size_t pre_roll_frames;
  bool worker_active;
  bool source_attached;
  bool mic_acquired;
  bool mic_prepared;
  bool mic_started;
};

struct bkvoice_wake_listener_s
{
  const struct bkvoice_turn_audio_ops_s *audio_ops;
  void *audio_context;
  const struct bkvoice_capture_source_ops_s *source_ops;
  void *source_context;
  struct bkvoice_kws_s *kws;
  struct bkvoice_wake_window_s *window;
  sem_t *wake;
  sem_t worker_done;
  pthread_t worker;
  uint8_t frame[BKVOICE_WAKE_FRAME_SAMPLES * sizeof(int16_t)];
  size_t frame_used;
  size_t stack_size;
  uint32_t join_timeout_ms;
  uint64_t next_frame_ms;
  volatile int state;
  volatile int result;
  volatile bool worker_joinable;
  volatile bool stop_requested;
  volatile bool source_attached;
  volatile bool mic_acquired;
  volatile bool mic_prepared;
  volatile bool mic_started;
  float wake_score;
  bool initialized;
};

/* All lifecycle calls are serialized by the AP owner. kws and window must be
 * initialized before initialize(), and are owned by the listener worker from
 * successful start() until stop() has joined it. The owner supplies start_ms
 * from its monotonic timebase; frame timestamps advance by sampled 20 ms only.
 */
int bkvoice_wake_listener_initialize(
  struct bkvoice_wake_listener_s *listener,
  const struct bkvoice_turn_audio_ops_s *audio_ops, void *audio_context,
  const struct bkvoice_capture_source_ops_s *source_ops, void *source_context,
  struct bkvoice_kws_s *kws, struct bkvoice_wake_window_s *window,
  sem_t *wake, size_t stack_size, uint32_t join_timeout_ms);
int bkvoice_wake_listener_start(struct bkvoice_wake_listener_s *listener,
                                uint64_t start_ms);
int bkvoice_wake_listener_stop(struct bkvoice_wake_listener_s *listener);
/* Retriable: a stop/join/release failure leaves every borrowed context and
 * semaphore intact.  Success destroys worker_done and clears the object. */
int bkvoice_wake_listener_uninitialize(
  struct bkvoice_wake_listener_s *listener);
void bkvoice_wake_listener_status(const struct bkvoice_wake_listener_s *listener,
                                  struct bkvoice_wake_listener_status_s *status);

#endif
