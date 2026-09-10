/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_voice_wake_listener.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static int deadline(uint32_t timeout_ms, struct timespec *out)
{
  uint64_t ns;
  if (clock_gettime(CLOCK_REALTIME, out) < 0) return -errno;
  ns = (uint64_t)out->tv_nsec + (uint64_t)(timeout_ms % 1000u) * 1000000u;
  out->tv_sec += timeout_ms / 1000u + ns / 1000000000u;
  out->tv_nsec = ns % 1000000000u;
  return 0;
}

static void notify(struct bkvoice_wake_listener_s *l)
{
  if (l->wake != NULL) (void)sem_post(l->wake);
}

static void *worker(void *arg)
{
  struct bkvoice_wake_listener_s *l = arg;
  int ret = 0;
  float score = 0.0f;

  for (;;)
    {
      ssize_t got = l->source_ops->read(l->source_context,
        l->frame + l->frame_used, sizeof(l->frame) - l->frame_used);
      if (got <= 0 || (size_t)got > sizeof(l->frame) - l->frame_used)
        {
          ret = got < 0 ? (int)got : -EIO;
          break;
        }

      l->frame_used += (size_t)got;
      if (l->frame_used != sizeof(l->frame)) continue;
      l->frame_used = 0;
      l->next_frame_ms += 20u;
      ret = bkvoice_wake_window_observe(l->window,
        (const int16_t *)l->frame, BKVOICE_WAKE_FRAME_SAMPLES,
        l->next_frame_ms);
      if (ret < 0) break;
      ret = bkvoice_kws_feed(l->kws, (const int16_t *)l->frame,
        BKVOICE_WAKE_FRAME_SAMPLES, l->next_frame_ms, &score);
      __atomic_store(&l->wake_score, &score, __ATOMIC_RELEASE);
      if (ret < 0) break;
      if (ret == 1)
        {
          ret = bkvoice_wake_window_trigger(l->window, l->next_frame_ms);
          if (ret == 0)
            __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_TRIGGERED,
                             __ATOMIC_RELEASE);
          break;
        }
    }

  if (ret < 0 && !__atomic_load_n(&l->stop_requested, __ATOMIC_ACQUIRE))
    __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_FAULTED,
                     __ATOMIC_RELEASE);
  __atomic_store_n(&l->result, ret, __ATOMIC_RELEASE);
  (void)sem_post(&l->worker_done);
  notify(l);
  return NULL;
}

static int release_audio(struct bkvoice_wake_listener_s *l)
{
  int ret;
  if (__atomic_load_n(&l->source_attached, __ATOMIC_ACQUIRE))
    {
      ret = l->source_ops->detach(l->source_context);
      if (ret < 0) return ret;
      else __atomic_store_n(&l->source_attached, false, __ATOMIC_RELEASE);
    }
  if (__atomic_load_n(&l->mic_started, __ATOMIC_ACQUIRE))
    {
      ret = l->audio_ops->mic_stop(l->audio_context);
      if (ret < 0) return ret;
      __atomic_store_n(&l->mic_started, false, __ATOMIC_RELEASE);
    }
  if (__atomic_load_n(&l->mic_prepared, __ATOMIC_ACQUIRE))
    {
      ret = l->audio_ops->mic_drain(l->audio_context);
      if (ret < 0) return ret;
    }
  if (__atomic_load_n(&l->mic_acquired, __ATOMIC_ACQUIRE))
    {
      ret = l->audio_ops->mic_release(l->audio_context);
      if (ret < 0) return ret;
      __atomic_store_n(&l->mic_acquired, false, __ATOMIC_RELEASE);
      __atomic_store_n(&l->mic_prepared, false, __ATOMIC_RELEASE);
      __atomic_store_n(&l->mic_started, false, __ATOMIC_RELEASE);
    }
  return 0;
}

int bkvoice_wake_listener_initialize(struct bkvoice_wake_listener_s *l,
  const struct bkvoice_turn_audio_ops_s *audio_ops, void *audio_context,
  const struct bkvoice_capture_source_ops_s *source_ops, void *source_context,
  struct bkvoice_kws_s *kws, struct bkvoice_wake_window_s *window,
  sem_t *wake, size_t stack_size, uint32_t join_timeout_ms)
{
  if (!l || !audio_ops || !source_ops || !audio_context || !source_context ||
      !kws || !window || !audio_ops->mic_acquire || !audio_ops->mic_prepare ||
      !audio_ops->mic_start || !audio_ops->mic_stop || !audio_ops->mic_drain ||
      !audio_ops->mic_release || !source_ops->attach || !source_ops->read ||
      !source_ops->interrupt || !source_ops->detach || stack_size == 0 ||
      join_timeout_ms == 0) return -EINVAL;
  memset(l, 0, sizeof(*l));
  l->audio_ops = audio_ops; l->audio_context = audio_context;
  l->source_ops = source_ops; l->source_context = source_context;
  l->kws = kws; l->window = window; l->wake = wake; l->stack_size = stack_size;
  l->join_timeout_ms = join_timeout_ms;
  if (sem_init(&l->worker_done, 0, 0) < 0) return -errno;
  __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_IDLE, __ATOMIC_RELEASE);
  l->initialized = true;
  return 0;
}

int bkvoice_wake_listener_start(struct bkvoice_wake_listener_s *l,
                                uint64_t start_ms)
{
  pthread_attr_t attr;
  int ret;
  if (!l || !l->initialized || start_ms == 0) return -EINVAL;
  if (__atomic_load_n(&l->worker_joinable, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&l->source_attached, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&l->mic_acquired, __ATOMIC_ACQUIRE)) return -EBUSY;
  bkvoice_kws_pause(l->kws); bkvoice_wake_window_reset(l->window);
  l->frame_used = 0; l->next_frame_ms = start_ms;
  {
    const float score = 0.0f;
    __atomic_store(&l->wake_score, &score, __ATOMIC_RELEASE);
  }
  __atomic_store_n(&l->stop_requested, false, __ATOMIC_RELEASE);
  __atomic_store_n(&l->result, 0, __ATOMIC_RELEASE);
  ret = l->audio_ops->mic_acquire(l->audio_context);
  if (ret < 0) goto fail;
  __atomic_store_n(&l->mic_acquired, true, __ATOMIC_RELEASE);
  ret = l->audio_ops->mic_prepare(l->audio_context);
  if (ret < 0) goto fail;
  __atomic_store_n(&l->mic_prepared, true, __ATOMIC_RELEASE);
  ret = l->audio_ops->mic_start(l->audio_context);
  if (ret < 0) goto fail;
  __atomic_store_n(&l->mic_started, true, __ATOMIC_RELEASE);
  ret = l->source_ops->attach(l->source_context);
  if (ret < 0) goto fail;
  __atomic_store_n(&l->source_attached, true, __ATOMIC_RELEASE);
  while (sem_trywait(&l->worker_done) == 0) {}
  ret = pthread_attr_init(&attr);
  if (ret != 0) { ret = -ret; goto fail; }
  ret = pthread_attr_setstacksize(&attr, l->stack_size);
  if (ret == 0)
    {
      __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_RUNNING,
                       __ATOMIC_RELEASE);
      __atomic_store_n(&l->worker_joinable, true, __ATOMIC_RELEASE);
      ret = pthread_create(&l->worker, &attr, worker, l);
    }
  (void)pthread_attr_destroy(&attr);
  if (ret != 0)
    {
      __atomic_store_n(&l->worker_joinable, false, __ATOMIC_RELEASE);
      ret = -ret;
      goto fail;
    }
  return 0;
fail:
  (void)release_audio(l);
  __atomic_store_n(&l->result, ret, __ATOMIC_RELEASE);
  __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_FAULTED, __ATOMIC_RELEASE);
  return ret;
}

int bkvoice_wake_listener_stop(struct bkvoice_wake_listener_s *l)
{
  struct timespec when;
  int ret;
  if (!l || !l->initialized) return -EINVAL;
  if (__atomic_load_n(&l->worker_joinable, __ATOMIC_ACQUIRE))
    {
      __atomic_store_n(&l->stop_requested, true, __ATOMIC_RELEASE);
      if (__atomic_load_n(&l->state, __ATOMIC_ACQUIRE) ==
          BKVOICE_WAKE_LISTENER_RUNNING)
        __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_STOPPING,
                         __ATOMIC_RELEASE);
      ret = l->source_ops->interrupt(l->source_context);
      if (ret < 0) return ret;
      ret = deadline(l->join_timeout_ms, &when);
      if (ret < 0) return ret;
      do ret = sem_timedwait(&l->worker_done, &when); while (ret < 0 && errno == EINTR);
      if (ret < 0) return -errno;
      ret = pthread_join(l->worker, NULL);
      if (ret != 0) return -ret;
      __atomic_store_n(&l->worker_joinable, false, __ATOMIC_RELEASE);
    }
  ret = release_audio(l);
  if (ret < 0) return ret;
  if (__atomic_load_n(&l->state, __ATOMIC_ACQUIRE) == BKVOICE_WAKE_LISTENER_STOPPING)
    __atomic_store_n(&l->state, BKVOICE_WAKE_LISTENER_IDLE, __ATOMIC_RELEASE);
  return 0;
}

int bkvoice_wake_listener_uninitialize(struct bkvoice_wake_listener_s *l)
{
  int ret;

  if (l == NULL || !l->initialized)
    {
      return -EINVAL;
    }

  ret = bkvoice_wake_listener_stop(l);
  if (ret < 0)
    {
      return ret;
    }

  if (sem_destroy(&l->worker_done) < 0)
    {
      return -errno;
    }

  memset(l, 0, sizeof(*l));
  return 0;
}

void bkvoice_wake_listener_status(const struct bkvoice_wake_listener_s *l,
                                  struct bkvoice_wake_listener_status_s *s)
{
  if (!s) return;
  memset(s, 0, sizeof(*s)); if (!l) return;
  s->state = (enum bkvoice_wake_listener_state_e)
    __atomic_load_n(&l->state, __ATOMIC_ACQUIRE);
  s->result = __atomic_load_n(&l->result, __ATOMIC_ACQUIRE);
  __atomic_load(&l->wake_score, &s->wake_score, __ATOMIC_ACQUIRE);
  s->worker_active = __atomic_load_n(&l->worker_joinable, __ATOMIC_ACQUIRE);
  if (!s->worker_active)
    s->pre_roll_frames = bkvoice_wake_window_pre_roll_frames(l->window);
  s->source_attached = __atomic_load_n(&l->source_attached, __ATOMIC_ACQUIRE);
  s->mic_acquired = __atomic_load_n(&l->mic_acquired, __ATOMIC_ACQUIRE);
  s->mic_prepared = __atomic_load_n(&l->mic_prepared, __ATOMIC_ACQUIRE);
  s->mic_started = __atomic_load_n(&l->mic_started, __ATOMIC_ACQUIRE);
}
