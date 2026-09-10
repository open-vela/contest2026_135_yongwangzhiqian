/* SPDX-License-Identifier: Apache-2.0 */
/* Host lifecycle test: listener/cloud calls are mocks.  It does not validate
 * a wake-word model or a target microphone. */
#include "bk7258_voice_wake_owner.h"

#include <assert.h>
#include <errno.h>
#include <semaphore.h>
#include <string.h>

static struct bkvoice_wake_listener_status_s g_listener;
static int g_start_calls;
static int g_start_result;
static int g_stop_calls;
static int g_stop_result;
static int g_begin_calls;
static int g_begin_result;
static int g_end_calls;
static int g_end_result;
static int g_cancel_calls;
static bool g_cloud_busy;
static bool g_cancel_pending;
static bool g_prefill_ok;

int bkvoice_wake_listener_start(struct bkvoice_wake_listener_s *listener,
                                uint64_t now_ms)
{
  (void)listener;
  assert(now_ms != 0);
  g_start_calls++;
  g_listener.state = g_start_result < 0 ? BKVOICE_WAKE_LISTENER_FAULTED :
    BKVOICE_WAKE_LISTENER_RUNNING;
  g_listener.result = g_start_result;
  return g_start_result;
}

int bkvoice_wake_listener_stop(struct bkvoice_wake_listener_s *listener)
{
  (void)listener;
  g_stop_calls++;
  if (g_stop_result < 0)
    {
      return g_stop_result;
    }
  g_listener.state = BKVOICE_WAKE_LISTENER_IDLE;
  return 0;
}

void bkvoice_wake_listener_status(const struct bkvoice_wake_listener_s *listener,
                                  struct bkvoice_wake_listener_status_s *status)
{
  (void)listener;
  *status = g_listener;
}

int bkcloud_runtime_auto_begin(
  struct bkcloud_runtime_s *cloud, bkvoice_capture_prefill_read_t read,
  void *context, size_t frames, bkvoice_capture_live_observer_t observer,
  void *observer_context)
{
  uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES];
  (void)cloud;
  (void)observer;
  (void)observer_context;
  g_begin_calls++;
  assert(frames != 0);
  g_prefill_ok = read(context, 0, pcm) == 0;
  if (g_begin_result == 0)
    {
      g_cloud_busy = true;
    }
  return g_begin_result;
}

int bkcloud_runtime_auto_end(struct bkcloud_runtime_s *cloud)
{
  (void)cloud;
  g_end_calls++;
  return g_end_result;
}

int bkcloud_runtime_cancel(struct bkcloud_runtime_s *cloud)
{
  (void)cloud;
  g_cancel_calls++;
  return 0;
}

int bkcloud_runtime_cancel_drain(struct bkcloud_runtime_s *cloud)
{
  (void)cloud;
  if (!g_cancel_pending)
    {
      g_cancel_pending = true;
      g_cancel_calls++;
    }
  if (g_cloud_busy)
    {
      return -EAGAIN;
    }
  g_cancel_pending = false;
  return 0;
}

bool bkcloud_runtime_busy(const struct bkcloud_runtime_s *cloud)
{
  (void)cloud;
  return g_cloud_busy;
}

static const struct bkvoice_wake_window_policy_s g_policy =
{
  .minimum_speech_mean_abs = 1,
  .speech_to_noise_q8 = 256,
  .speech_confirm_frames = 1,
  .silence_end_frames = 1,
  .no_speech_frames = 1,
  .maximum_turn_frames = 3,
};

static void reset_mocks(void)
{
  memset(&g_listener, 0, sizeof(g_listener));
  g_stop_result = 0;
  g_start_result = 0;
  g_begin_result = 0;
  g_end_result = 0;
  g_cloud_busy = false;
  g_cancel_pending = false;
  g_prefill_ok = false;
  g_start_calls = g_stop_calls = g_begin_calls = g_end_calls = g_cancel_calls = 0;
}

static void prime_window(struct bkvoice_wake_window_s *window,
                         int16_t *history)
{
  int16_t frame[BKVOICE_WAKE_FRAME_SAMPLES];
  memset(frame, 1, sizeof(frame));
  assert(bkvoice_wake_window_initialize(window, history,
         BKVOICE_WAKE_PRE_ROLL_SAMPLES, &g_policy) == 0);
  assert(bkvoice_wake_window_observe(window, frame,
         BKVOICE_WAKE_FRAME_SAMPLES, 20) == 0);
  assert(bkvoice_wake_window_trigger(window, 20) == 0);
}

static void initialize_owner(struct bkvoice_wake_owner_s *owner,
                             struct bkvoice_wake_window_s *window,
                             sem_t *wake)
{
  static struct bkvoice_wake_listener_s listener;
  static struct bkcloud_runtime_s *cloud = (void *)1;
  assert(sem_init(wake, 0, 0) == 0);
  assert(bkvoice_wake_owner_initialize(owner, &listener, window, cloud, wake) == 0);
}

static void test_trigger_stops_before_begin_and_restarts(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks();
  prime_window(&window, history);
  initialize_owner(&owner, &window, &wake);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  assert(g_start_calls == 1);
  g_listener.state = BKVOICE_WAKE_LISTENER_TRIGGERED;
  assert(bkvoice_wake_owner_step(&owner, true, 120) == 0);
  assert(g_stop_calls == 1 && g_begin_calls == 1 && g_prefill_ok);
  assert(owner.state == BKVOICE_WAKE_OWNER_CAPTURE);
  g_cloud_busy = false;
  assert(bkvoice_wake_owner_step(&owner, true, 140) == 0);
  assert(g_start_calls == 2);
  assert(sem_destroy(&wake) == 0);
}

static void test_trigger_stop_failure_never_begins(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  g_listener.state = BKVOICE_WAKE_LISTENER_TRIGGERED;
  g_stop_result = -ETIMEDOUT;
  assert(bkvoice_wake_owner_step(&owner, true, 120) == -ETIMEDOUT);
  assert(g_begin_calls == 0);
  assert(sem_destroy(&wake) == 0);
}

static void test_begin_failure_cancels_without_upload(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  g_listener.state = BKVOICE_WAKE_LISTENER_TRIGGERED;
  g_begin_result = -ENOMEM;
  assert(bkvoice_wake_owner_step(&owner, true, 120) == -ENOMEM);
  assert(bkvoice_wake_owner_step(&owner, true, 140) == 0);
  assert(g_cancel_calls == 1 && g_end_calls == 0);
  assert(sem_destroy(&wake) == 0);
}

static void test_no_speech_cancels_and_end_retries(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  int16_t silence[BKVOICE_WAKE_FRAME_SAMPLES] = {0};
  int16_t speech[BKVOICE_WAKE_FRAME_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  owner.automatic_started = true;
  owner.automatic_owned = true;
  owner.state = BKVOICE_WAKE_OWNER_CAPTURE;
  owner.live_next_ms = 20;
  g_cloud_busy = true;
  bkvoice_wake_owner_live_observer(&owner, NULL, (const uint8_t *)silence);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == -EAGAIN);
  assert(g_cancel_calls == 1 && g_end_calls == 0);
  g_cloud_busy = false;
  assert(bkvoice_wake_owner_step(&owner, true, 120) == 0);
  assert(bkvoice_wake_owner_step(&owner, true, 140) == 0);
  assert(g_start_calls == 1);
  assert(sem_destroy(&wake) == 0);

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  owner.automatic_started = true;
  owner.automatic_owned = true;
  owner.state = BKVOICE_WAKE_OWNER_CAPTURE;
  owner.live_next_ms = 20;
  memset(speech, 1, sizeof(speech));
  /* Use the real endpoint window: speech starts the turn, silence terminates
   * it, then another capture frame arrives while auto_end is retried. */
  bkvoice_wake_owner_live_observer(&owner, NULL, (const uint8_t *)speech);
  assert(__atomic_load_n(&owner.event, __ATOMIC_ACQUIRE) == BKVOICE_WAKE_EVENT_NONE);
  bkvoice_wake_owner_live_observer(&owner, NULL, (const uint8_t *)silence);
  assert(__atomic_load_n(&owner.event, __ATOMIC_ACQUIRE) ==
         BKVOICE_WAKE_EVENT_END_OF_SPEECH);
  g_end_result = -EAGAIN;
  assert(bkvoice_wake_owner_step(&owner, true, 100) == -EAGAIN);
  bkvoice_wake_owner_live_observer(&owner, NULL, (const uint8_t *)silence);
  assert(__atomic_load_n(&owner.event, __ATOMIC_ACQUIRE) ==
         BKVOICE_WAKE_EVENT_END_OF_SPEECH);
  assert(__atomic_load_n(&owner.observer_error, __ATOMIC_ACQUIRE) == 0);
  g_end_result = 0;
  assert(bkvoice_wake_owner_step(&owner, true, 120) == 0);
  assert(g_end_calls == 2 && g_cancel_calls == 0);
  assert(sem_destroy(&wake) == 0);
}

static void test_config_disable_stops_then_drains(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  owner.automatic_started = true;
  owner.automatic_owned = true;
  g_cloud_busy = true;
  assert(bkvoice_wake_owner_step(&owner, false, 120) == -EAGAIN);
  assert(g_stop_calls == 1 && g_cancel_calls == 1);
  g_cloud_busy = false;
  assert(bkvoice_wake_owner_step(&owner, false, 140) == 0);
  assert(sem_destroy(&wake) == 0);
}

static void test_suspend_and_cancel_leave_other_cloud_turn(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history);
  initialize_owner(&owner, &window, &wake);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  g_cloud_busy = true;
  assert(bkvoice_wake_owner_suspend(&owner) == 0);
  assert(g_stop_calls == 1 && g_cancel_calls == 0);
  assert(bkvoice_wake_owner_cancel(&owner) == 0);
  assert(g_cancel_calls == 0);
  assert(sem_destroy(&wake) == 0);
}

static void test_completed_capture_remains_owned_until_cloud_idle(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history);
  initialize_owner(&owner, &window, &wake);
  owner.automatic_started = true;
  owner.automatic_owned = true;
  owner.state = BKVOICE_WAKE_OWNER_CAPTURE;
  __atomic_store_n(&owner.event, BKVOICE_WAKE_EVENT_MAX_DURATION,
                   __ATOMIC_RELEASE);
  g_cloud_busy = true;
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  assert(!owner.automatic_started && owner.automatic_owned);
  assert(bkvoice_wake_owner_cancel(&owner) == -EAGAIN);
  assert(g_cancel_calls == 1 && owner.automatic_owned);
  g_cloud_busy = false;
  assert(bkvoice_wake_owner_cancel(&owner) == 0);
  assert(!owner.automatic_owned);
  assert(sem_destroy(&wake) == 0);
}

static void test_close_retries_without_releasing_borrowed_context(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  g_stop_result = -ETIMEDOUT;
  assert(bkvoice_wake_owner_close(&owner) == -ETIMEDOUT);
  assert(owner.initialized && owner.listener != NULL && owner.window == &window);
  g_stop_result = 0;
  assert(bkvoice_wake_owner_close(&owner) == 0);
  assert(!owner.initialized && owner.state == BKVOICE_WAKE_OWNER_CLOSED);
  assert(sem_destroy(&wake) == 0);
}

static void test_failed_start_and_fault_recover_after_release(void)
{
  struct bkvoice_wake_owner_s owner;
  struct bkvoice_wake_window_s window;
  int16_t history[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  sem_t wake;

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  g_start_result = -EIO;
  assert(bkvoice_wake_owner_step(&owner, true, 100) == -EIO);
  assert(owner.listener_started);
  g_stop_result = -ETIMEDOUT;
  assert(bkvoice_wake_owner_step(&owner, true, 120) == -ETIMEDOUT);
  g_stop_result = 0;
  assert(bkvoice_wake_owner_step(&owner, true, 140) == -ETIMEDOUT);
  assert(!owner.listener_started);
  assert(bkvoice_wake_owner_step(&owner, true, 500) == -EAGAIN);
  assert(g_start_calls == 1);
  g_start_result = 0;
  assert(bkvoice_wake_owner_step(&owner, true, 1200) == 0);
  assert(g_start_calls == 2);
  assert(sem_destroy(&wake) == 0);

  reset_mocks(); prime_window(&window, history); initialize_owner(&owner, &window, &wake);
  assert(bkvoice_wake_owner_step(&owner, true, 100) == 0);
  g_listener.state = BKVOICE_WAKE_LISTENER_FAULTED;
  g_listener.result = -EIO;
  assert(bkvoice_wake_owner_step(&owner, true, 120) == -EIO);
  assert(bkvoice_wake_owner_step(&owner, true, 500) == -EAGAIN);
  assert(g_start_calls == 1);
  assert(bkvoice_wake_owner_step(&owner, true, 1200) == 0);
  assert(g_start_calls == 2);
  assert(sem_destroy(&wake) == 0);
}

int main(void)
{
  test_trigger_stops_before_begin_and_restarts();
  test_trigger_stop_failure_never_begins();
  test_begin_failure_cancels_without_upload();
  test_no_speech_cancels_and_end_retries();
  test_config_disable_stops_then_drains();
  test_suspend_and_cancel_leave_other_cloud_turn();
  test_completed_capture_remains_owned_until_cloud_idle();
  test_close_retries_without_releasing_borrowed_context();
  test_failed_start_and_fault_recover_after_release();
  return 0;
}
