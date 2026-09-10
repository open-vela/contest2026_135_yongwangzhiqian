/****************************************************************************
 * app/bk7258/bk7258_voice_turn_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BKVoice MIC/DAC lifecycle adapter.  Device paths, channels, PA polarity and
 * pin ownership stay behind the public recorder/player ABI and its lower
 * halves; this App layer owns only the product's fixed PCM tuple.
 ****************************************************************************/

#ifdef __NuttX__
#include <nuttx/config.h>
#endif

#include "bk7258_voice_turn_audio.h"

#include <errno.h>
#include <media_player.h>
#include <media_policy.h>
#include <media_recorder.h>
#ifdef CONFIG_BK7258_PREFERENCES
#include "bk7258_preferences.h"
#endif
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
#include "bk7258_voice_volume_store.h"
#endif
#include <stdint.h>
#include <string.h>

#define BKVOICE_TURN_AUDIO_OPTIONS \
  "format=s16le:sample_rate=16000:ch_layout=mono"

static int bkvoice_turn_audio_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

bool bkvoice_turn_audio_released(const struct bkvoice_turn_audio_s *audio)
{
  return audio != NULL && audio->mic_handle == NULL &&
         audio->dac_handle == NULL && !audio->mic_prepared &&
         !audio->mic_started &&
         !__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE) &&
         !audio->dac_prepared &&
         !audio->dac_started;
}

int bkvoice_turn_audio_initialize(struct bkvoice_turn_audio_s *audio)
{
  if (audio == NULL)
    {
      return -EINVAL;
    }

  memset(audio, 0, sizeof(*audio));
  return 0;
}

static int bkvoice_turn_audio_mic_acquire(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_handle != NULL || audio->dac_handle != NULL ||
      __atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  errno = 0;
  audio->mic_handle = media_recorder_open(MEDIA_SOURCE_MIC);
  return audio->mic_handle != NULL ? 0 : bkvoice_turn_audio_errno();
}

static int bkvoice_turn_audio_mic_prepare(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->mic_handle == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_prepared)
    {
      return -EALREADY;
    }

  ret = media_recorder_prepare(audio->mic_handle, NULL,
                               BKVOICE_TURN_AUDIO_OPTIONS);
  if (ret >= 0)
    {
      audio->mic_prepared = true;
      return 0;
    }

  return ret;
}

static int bkvoice_turn_audio_mic_start(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->mic_handle == NULL || !audio->mic_prepared)
    {
      return -EINVAL;
    }

  if (audio->mic_started)
    {
      return -EALREADY;
    }

  ret = media_recorder_start(audio->mic_handle);
  if (ret >= 0)
    {
      audio->mic_started = true;
      return 0;
    }

  return ret;
}

int bkvoice_turn_audio_reader_attach(struct bkvoice_turn_audio_s *audio)
{
  bool expected = false;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_handle == NULL || !audio->mic_prepared ||
      !audio->mic_started)
    {
      return -EPERM;
    }

  if (!__atomic_compare_exchange_n(&audio->mic_reader_active, &expected,
                                   true, false, __ATOMIC_ACQ_REL,
                                   __ATOMIC_ACQUIRE))
    {
      return -EALREADY;
    }

  return 0;
}

int bkvoice_turn_audio_reader_detach(struct bkvoice_turn_audio_s *audio)
{
  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (!__atomic_exchange_n(&audio->mic_reader_active, false,
                           __ATOMIC_ACQ_REL))
    {
      return -EALREADY;
    }

  return 0;
}

ssize_t bkvoice_turn_audio_read(struct bkvoice_turn_audio_s *audio,
                                void *pcm, size_t bytes)
{
  if (audio == NULL || pcm == NULL || bytes == 0)
    {
      return -EINVAL;
    }

  /* A registered reader pins the recorder handle until detach.  After stop,
   * the blocked public read returns -EPIPE; release refuses to destroy the
   * handle until the reader has observed that wake and detached.
   */

  if (audio->mic_handle == NULL ||
      !__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EPERM;
    }

  return media_recorder_read_data(audio->mic_handle, pcm, bytes);
}

static int bkvoice_turn_audio_mic_stop(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->mic_handle == NULL)
    {
      return -EINVAL;
    }

  if (!audio->mic_started)
    {
      return 0;
    }

  ret = media_recorder_stop(audio->mic_handle);
  if (ret >= 0)
    {
      audio->mic_started = false;
      return 0;
    }

  return ret;
}

int bkvoice_turn_audio_reader_stop(struct bkvoice_turn_audio_s *audio)
{
  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EPERM;
    }

  /* PTT release stops the public recorder so the blocking reader wakes, but
   * deliberately leaves that reader attached.  The owner must join and
   * detach it before the turn arbiter may drain and release the MIC.
   */

  return bkvoice_turn_audio_mic_stop(audio);
}

static int bkvoice_turn_audio_mic_drain(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL || audio->mic_handle == NULL ||
      !audio->mic_prepared)
    {
      return -EINVAL;
    }

  /* media_recorder_stop() synchronously stops the lower half and wakes its
   * blocking mqueue reader.  The future capture owner must join that reader
   * before invoking this serialized drain/release sequence.
   */

  return audio->mic_started ||
         __atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE) ?
         -EBUSY : 0;
}

static int bkvoice_turn_audio_mic_release(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  if (audio->mic_handle == NULL)
    {
      return 0;
    }

  ret = media_recorder_close(audio->mic_handle);
  if (ret >= 0)
    {
      audio->mic_handle = NULL;
      audio->mic_prepared = false;
      audio->mic_started = false;
      __atomic_store_n(&audio->mic_reader_active, false,
                       __ATOMIC_RELEASE);
      return 0;
    }

  /* The BK7258 recorder bridge guarantees that a negative close result
   * leaves the handle alive for a later fail-closed recovery attempt.
   */

  return ret;
}

static void bkvoice_turn_audio_event(void *cookie, int event, int result,
                                     const char *extra)
{
  struct bkvoice_turn_audio_s *audio = cookie;
  (void)extra;
  if (event == MEDIA_EVENT_COMPLETED)
    {
      audio->dac_result = result;
      __atomic_store_n(&audio->dac_completed, true, __ATOMIC_RELEASE);
    }
}

static int bkvoice_turn_audio_dac_acquire(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->mic_handle != NULL || audio->dac_handle != NULL ||
      __atomic_load_n(&audio->mic_reader_active, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  errno = 0;
  audio->dac_handle = media_player_open(MEDIA_STREAM_MUSIC);
  return audio->dac_handle != NULL ? 0 : bkvoice_turn_audio_errno();
}

static int bkvoice_turn_audio_volume_policy(struct bkvoice_turn_audio_s *audio,
                                            bool apply,
                                            unsigned int requested,
                                            unsigned int *volume)
{
  int minimum;
  int maximum;
  int index;
  int observed;
  int ret;

  if (audio == NULL || volume == NULL || (apply && requested > 100u))
    {
      return -EINVAL;
    }

  ret = media_policy_get_range(MEDIA_STREAM_MUSIC MEDIA_POLICY_VOLUME,
                                &minimum, &maximum);
  if (ret < 0)
    {
      return ret;
    }

  if (minimum < 0 || maximum <= minimum)
    {
      return -ERANGE;
    }

  index = minimum + (int)(((uint64_t)requested *
                           (maximum - minimum) + 50u) / 100u);
  if (apply)
    {
      ret = media_policy_set_stream_volume(MEDIA_STREAM_MUSIC, index);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = media_policy_get_stream_volume(MEDIA_STREAM_MUSIC, &observed);
  if (ret < 0)
    {
      return ret;
    }

  if (observed < minimum || observed > maximum ||
      (apply && observed != index))
    {
      return -EIO;
    }

  *volume = (unsigned int)(((uint64_t)(observed - minimum) * 100u +
                            (maximum - minimum) / 2u) / (maximum - minimum));
  if (apply)
    {
      audio->volume_override = true;
      audio->volume_percent = requested;
    }

  return 0;
}

static int bkvoice_turn_audio_volume(void *context, bool set,
                                     unsigned int requested,
                                     unsigned int *volume)
{
  struct bkvoice_turn_audio_s *audio = context;
  unsigned int persisted;
  int ret;

  if (audio == NULL || volume == NULL || (set && requested > 100u))
    {
      return -EINVAL;
    }

#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  if (set)
    {
      /* A successful report promises reboot persistence. Publish the small
       * LittleFS record before changing the live media policy.
       */
      ret = bkvoice_volume_store_set(requested);
      if (ret < 0)
        {
          return ret;
        }
    }
  else if (!audio->volume_override &&
           bkvoice_volume_store_get(&persisted) == 0)
    {
      /* The first post-boot query also reconciles the media policy, so the
       * App observes the restored value before the next TTS turn.
       */
      return bkvoice_turn_audio_volume_policy(audio, true, persisted, volume);
    }
#else
  (void)persisted;
  (void)ret;
#endif

  return bkvoice_turn_audio_volume_policy(audio, set, requested, volume);
}

static int bkvoice_turn_audio_apply_volume(struct bkvoice_turn_audio_s *audio)
{
  unsigned int desired = audio->volume_percent;
  unsigned int observed;
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  /* Every control path commits to the device volume store.  A previous
   * live override must not hide a later settings/CLI change on next reply.
   */
  bool override = false;
#else
  bool override = audio->volume_override;
#endif
  int ret;

  if (!override)
    {
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
      ret = bkvoice_volume_store_get(&desired);
      if (ret < 0)
        {
          /* Missing, not-yet-mounted or damaged preference data must not
           * take the core voice path offline. A later explicit mutation can
           * still report the precise storage failure to the App.
           */
          return 0;
        }
#elif defined(CONFIG_BK7258_PREFERENCES)
      ret = bk7258_preferences_playback_volume(&desired);
      if (ret < 0)
        {
          return ret;
        }
#else
      return 0;
#endif
    }

  if (desired > 100u)
    {
      return -ERANGE;
    }

  ret = bkvoice_turn_audio_volume_policy(audio, true, desired, &observed);
  audio->volume_override = override;
  return ret;
}

static int bkvoice_turn_audio_dac_prepare(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->dac_handle == NULL)
    {
      return -EINVAL;
    }

  if (audio->dac_prepared)
    {
      return -EALREADY;
    }

  __atomic_store_n(&audio->dac_completed, false, __ATOMIC_RELEASE);
  ret = media_player_set_event_callback(audio->dac_handle, audio,
                                        bkvoice_turn_audio_event);
  if (ret < 0)
    {
      return ret;
    }

  ret = media_player_prepare(audio->dac_handle, NULL,
                             BKVOICE_TURN_AUDIO_OPTIONS);
  if (ret >= 0)
    {
      audio->dac_prepared = true;
      /* Mark prepared before applying policy so failures still release the
       * player's reservation and buffers through the normal turn cleanup.
       */

      return bkvoice_turn_audio_apply_volume(audio);
    }

  return ret;
}

static int bkvoice_turn_audio_dac_start(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->dac_handle == NULL || !audio->dac_prepared)
    {
      return -EINVAL;
    }

  if (audio->dac_started)
    {
      return -EALREADY;
    }

  ret = media_player_start(audio->dac_handle);
  if (ret >= 0)
    {
      audio->dac_started = true;
      return 0;
    }

  return ret;
}

static ssize_t bkvoice_turn_audio_dac_write(void *context,
                                            const uint8_t *pcm,
                                            size_t bytes)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL || pcm == NULL || bytes == 0)
    {
      return -EINVAL;
    }

  if (audio->dac_handle == NULL || !audio->dac_prepared ||
      !audio->dac_started)
    {
      return -EPERM;
    }

  return media_player_write_data(audio->dac_handle, pcm, bytes);
}

static int bkvoice_turn_audio_dac_drain(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;

  if (audio == NULL || audio->dac_handle == NULL)
    {
      return -EINVAL;
    }

  if (!audio->dac_prepared && !audio->dac_started)
    {
      return 0;
    }

  media_player_close_socket(audio->dac_handle);
  return -EINPROGRESS;
}

static int bkvoice_turn_audio_dac_result(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  if (!__atomic_exchange_n(&audio->dac_completed, false, __ATOMIC_ACQ_REL))
    {
      return -EAGAIN;
    }

  return audio->dac_result;
}

static int bkvoice_turn_audio_dac_stop(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL || audio->dac_handle == NULL)
    {
      return -EINVAL;
    }

  if (!audio->dac_prepared && !audio->dac_started)
    {
      return 0;
    }

  /* Cancellation skips drain; the public close API stops immediately. */

  ret = media_player_close(audio->dac_handle, 0);
  if (ret >= 0)
    {
      audio->dac_handle = NULL;
      audio->dac_prepared = false;
      audio->dac_started = false;
      return 0;
    }

  return ret;
}

static int bkvoice_turn_audio_dac_release(void *context)
{
  struct bkvoice_turn_audio_s *audio = context;
  int ret;

  if (audio == NULL)
    {
      return -EINVAL;
    }

  if (audio->dac_handle == NULL)
    {
      return 0;
    }

  ret = media_player_close(audio->dac_handle, 0);
  if (ret >= 0)
    {
      audio->dac_handle = NULL;
      audio->dac_prepared = false;
      audio->dac_started = false;
      return 0;
    }

  /* The BK7258 player bridge guarantees that a negative close result leaves
   * the handle alive for a later fail-closed recovery attempt.
   */

  return ret;
}

static const struct bkvoice_turn_audio_ops_s g_bkvoice_turn_audio_ops =
{
  .mic_acquire = bkvoice_turn_audio_mic_acquire,
  .mic_prepare = bkvoice_turn_audio_mic_prepare,
  .mic_start = bkvoice_turn_audio_mic_start,
  .mic_stop = bkvoice_turn_audio_mic_stop,
  .mic_drain = bkvoice_turn_audio_mic_drain,
  .mic_release = bkvoice_turn_audio_mic_release,
  .dac_acquire = bkvoice_turn_audio_dac_acquire,
  .dac_prepare = bkvoice_turn_audio_dac_prepare,
  .dac_start = bkvoice_turn_audio_dac_start,
  .dac_write = bkvoice_turn_audio_dac_write,
  .dac_drain = bkvoice_turn_audio_dac_drain,
  .dac_result = bkvoice_turn_audio_dac_result,
  .dac_stop = bkvoice_turn_audio_dac_stop,
  .dac_release = bkvoice_turn_audio_dac_release,
  .volume = bkvoice_turn_audio_volume,
};

static int bkvoice_turn_capture_attach(void *context)
{
  return bkvoice_turn_audio_reader_attach(context);
}

static ssize_t bkvoice_turn_capture_read(void *context, void *pcm,
                                         size_t bytes)
{
  return bkvoice_turn_audio_read(context, pcm, bytes);
}

static int bkvoice_turn_capture_interrupt(void *context)
{
  return bkvoice_turn_audio_reader_stop(context);
}

static int bkvoice_turn_capture_detach(void *context)
{
  return bkvoice_turn_audio_reader_detach(context);
}

static const struct bkvoice_capture_source_ops_s
  g_bkvoice_turn_capture_source_ops =
{
  .attach = bkvoice_turn_capture_attach,
  .read = bkvoice_turn_capture_read,
  .interrupt = bkvoice_turn_capture_interrupt,
  .detach = bkvoice_turn_capture_detach,
};

const struct bkvoice_capture_source_ops_s *
bkvoice_turn_audio_capture_source_ops(void)
{
  return &g_bkvoice_turn_capture_source_ops;
}

const struct bkvoice_turn_audio_ops_s *bkvoice_turn_audio_ops(void)
{
  return &g_bkvoice_turn_audio_ops;
}
