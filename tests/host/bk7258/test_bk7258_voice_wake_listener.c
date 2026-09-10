/* SPDX-License-Identifier: Apache-2.0 */

#include "bk7258_voice_wake_listener.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum source_mode_e { SOURCE_TRIGGER, SOURCE_EOF, SOURCE_BLOCK };

struct fixture_s
{
  pthread_mutex_t lock;
  pthread_cond_t changed;
  enum source_mode_e mode;
  bool unblock;
  bool attached;
  bool fail_detach;
  bool fail_stop;
  bool fail_drain;
  unsigned int attach_calls;
  unsigned int interrupt_calls;
  unsigned int detach_calls;
  unsigned int release_calls;
  size_t produced;
};

static int infer(void *context, const float *features,
                 float scores[BKVOICE_KWS_CLASSES])
{
  (void)context;
  (void)features;
  scores[0] = 0.02f;
  scores[1] = 0.02f;
  scores[2] = 0.96f;
  return 0;
}

static int mic_acquire(void *context) { (void)context; return 0; }
static int mic_prepare(void *context) { (void)context; return 0; }
static int mic_start(void *context) { (void)context; return 0; }
static int mic_stop(void *context)
{
  struct fixture_s *f = context;
  return f->fail_stop ? -EIO : 0;
}
static int mic_drain(void *context)
{
  struct fixture_s *f = context;
  return f->fail_drain ? -EIO : 0;
}
static int mic_release(void *context)
{
  struct fixture_s *f = context;
  f->release_calls++;
  return 0;
}

static int attach(void *context)
{
  struct fixture_s *f = context;
  f->attached = true;
  f->attach_calls++;
  return 0;
}

static ssize_t read_pcm(void *context, void *pcm, size_t bytes)
{
  struct fixture_s *f = context;

  pthread_mutex_lock(&f->lock);
  if (f->mode == SOURCE_BLOCK)
    {
      while (!f->unblock)
        pthread_cond_wait(&f->changed, &f->lock);
      pthread_mutex_unlock(&f->lock);
      return -ECANCELED;
    }
  if (f->mode == SOURCE_EOF)
    {
      pthread_mutex_unlock(&f->lock);
      return 0;
    }

  bytes = bytes > 97u ? 97u : bytes;
  memset(pcm, 0, bytes);
  f->produced += bytes;
  pthread_mutex_unlock(&f->lock);
  return (ssize_t)bytes;
}

static int interrupt_source(void *context)
{
  struct fixture_s *f = context;
  pthread_mutex_lock(&f->lock);
  f->interrupt_calls++;
  pthread_mutex_unlock(&f->lock);
  return 0;
}

static int detach(void *context)
{
  struct fixture_s *f = context;
  f->detach_calls++;
  if (f->fail_detach) return -EIO;
  f->attached = false;
  return 0;
}

static const struct bkvoice_turn_audio_ops_s g_audio =
{
  .mic_acquire = mic_acquire, .mic_prepare = mic_prepare,
  .mic_start = mic_start, .mic_stop = mic_stop, .mic_drain = mic_drain,
  .mic_release = mic_release,
};
static const struct bkvoice_capture_source_ops_s g_source =
{
  .attach = attach, .read = read_pcm, .interrupt = interrupt_source,
  .detach = detach,
};

static void fixture_init(struct fixture_s *f)
{
  memset(f, 0, sizeof(*f));
  assert(pthread_mutex_init(&f->lock, NULL) == 0);
  assert(pthread_cond_init(&f->changed, NULL) == 0);
}

static void fixture_destroy(struct fixture_s *f)
{
  assert(pthread_cond_destroy(&f->changed) == 0);
  assert(pthread_mutex_destroy(&f->lock) == 0);
}

static void listener_init(struct bkvoice_wake_listener_s *listener,
                          struct bkvoice_kws_s *kws,
                          struct bkvoice_wake_window_s *window,
                          int16_t *pre_roll, struct fixture_s *fixture)
{
  const struct bkvoice_kws_policy_s kws_policy =
    {.threshold = .9f, .release_threshold = .2f, .consecutive = 1,
     .cooldown_ms = 0};
  const struct bkvoice_wake_window_policy_s window_policy =
    {.minimum_speech_mean_abs = 10, .speech_to_noise_q8 = 256,
     .speech_confirm_frames = 1, .silence_end_frames = 1,
     .no_speech_frames = 10, .maximum_turn_frames = 20};

  assert(bkvoice_kws_initialize(kws, &kws_policy, infer, NULL) == 0);
  assert(bkvoice_wake_window_initialize(window, pre_roll,
         BKVOICE_WAKE_PRE_ROLL_SAMPLES, &window_policy) == 0);
  assert(bkvoice_wake_listener_initialize(listener, &g_audio, fixture,
         &g_source, fixture, kws, window, NULL, 16384u, 20u) == 0);
}

static void listener_destroy(struct bkvoice_wake_listener_s *listener,
                             struct bkvoice_kws_s *kws,
                             struct bkvoice_wake_window_s *window)
{
  assert(bkvoice_wake_listener_uninitialize(listener) == 0);
  bkvoice_wake_window_uninitialize(window);
  bkvoice_kws_uninitialize(kws);
}

static void wait_result(struct bkvoice_wake_listener_s *listener, int expected)
{
  struct bkvoice_wake_listener_status_s status;
  const struct timespec pause = {.tv_nsec = 1000000L};

  for (unsigned int attempt = 0; attempt < 1000u; attempt++)
    {
      bkvoice_wake_listener_status(listener, &status);
      if (status.result == expected)
        {
          return;
        }

      nanosleep(&pause, NULL);
    }

  assert(!"listener worker did not finish");
}

static void test_partial_frames_trigger(void)
{
  struct fixture_s f;
  struct bkvoice_wake_listener_s l;
  struct bkvoice_kws_s kws;
  struct bkvoice_wake_window_s window;
  int16_t pre_roll[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  struct bkvoice_wake_listener_status_s status;

  fixture_init(&f); f.mode = SOURCE_TRIGGER;
  listener_init(&l, &kws, &window, pre_roll, &f);
  assert(bkvoice_wake_listener_start(&l, 100u) == 0);
  assert(bkvoice_wake_listener_stop(&l) == 0);
  bkvoice_wake_listener_status(&l, &status);
  assert(status.state == BKVOICE_WAKE_LISTENER_TRIGGERED);
  assert(status.wake_score == .96f && status.pre_roll_frames != 0u);
  assert(!status.worker_active && !status.source_attached &&
         !status.mic_acquired);
  listener_destroy(&l, &kws, &window); fixture_destroy(&f);
}

static void test_eof_fault(void)
{
  struct fixture_s f; struct bkvoice_wake_listener_s l; struct bkvoice_kws_s kws;
  struct bkvoice_wake_window_s window; int16_t pre_roll[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  struct bkvoice_wake_listener_status_s status;
  fixture_init(&f); f.mode = SOURCE_EOF; listener_init(&l, &kws, &window, pre_roll, &f);
  assert(bkvoice_wake_listener_start(&l, 100u) == 0);
  wait_result(&l, -EIO);
  assert(bkvoice_wake_listener_stop(&l) == 0); bkvoice_wake_listener_status(&l, &status);
  assert(status.state == BKVOICE_WAKE_LISTENER_FAULTED && status.result == -EIO);
  listener_destroy(&l, &kws, &window); fixture_destroy(&f);
}

static void test_timeout_then_retry(void)
{
  struct fixture_s f; struct bkvoice_wake_listener_s l; struct bkvoice_kws_s kws;
  struct bkvoice_wake_window_s window; int16_t pre_roll[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  struct bkvoice_wake_listener_status_s status;
  fixture_init(&f); f.mode = SOURCE_BLOCK; listener_init(&l, &kws, &window, pre_roll, &f);
  assert(bkvoice_wake_listener_start(&l, 100u) == 0);
  assert(bkvoice_wake_listener_stop(&l) == -ETIMEDOUT); bkvoice_wake_listener_status(&l, &status);
  assert(status.worker_active && status.source_attached && status.mic_acquired && f.release_calls == 0u);
  pthread_mutex_lock(&f.lock); f.unblock = true; pthread_cond_broadcast(&f.changed); pthread_mutex_unlock(&f.lock);
  assert(bkvoice_wake_listener_stop(&l) == 0); bkvoice_wake_listener_status(&l, &status);
  assert(!status.worker_active && !status.source_attached && !status.mic_acquired && f.release_calls == 1u);
  listener_destroy(&l, &kws, &window); fixture_destroy(&f);
}

static void test_release_failures_retry(void)
{
  struct fixture_s f; struct bkvoice_wake_listener_s l; struct bkvoice_kws_s kws;
  struct bkvoice_wake_window_s window; int16_t pre_roll[BKVOICE_WAKE_PRE_ROLL_SAMPLES];
  struct bkvoice_wake_listener_status_s status;
  fixture_init(&f); f.mode = SOURCE_EOF; listener_init(&l, &kws, &window, pre_roll, &f);
  assert(bkvoice_wake_listener_start(&l, 100u) == 0); wait_result(&l, -EIO); f.fail_detach = true;
  assert(bkvoice_wake_listener_stop(&l) == -EIO); bkvoice_wake_listener_status(&l, &status);
  assert(status.source_attached && status.mic_acquired && f.release_calls == 0u);
  f.fail_detach = false; f.fail_stop = true; assert(bkvoice_wake_listener_stop(&l) == -EIO);
  bkvoice_wake_listener_status(&l, &status); assert(!status.source_attached && status.mic_started && status.mic_acquired);
  f.fail_stop = false; f.fail_drain = true; assert(bkvoice_wake_listener_stop(&l) == -EIO);
  bkvoice_wake_listener_status(&l, &status); assert(!status.mic_started && status.mic_prepared && status.mic_acquired && f.release_calls == 0u);
  f.fail_drain = false; assert(bkvoice_wake_listener_stop(&l) == 0); assert(f.release_calls == 1u);
  listener_destroy(&l, &kws, &window); fixture_destroy(&f);
}

int main(void)
{
  test_partial_frames_trigger(); test_eof_fault(); test_timeout_then_retry();
  test_release_failures_retry();
  puts("bk7258 voice wake listener host tests: PASS");
  return 0;
}
