/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>
#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/clock.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "dolphin_adc_key.h"

#ifndef CONFIG_DOLPHIN_ADC_KEY_DEVPATH
#  define CONFIG_DOLPHIN_ADC_KEY_DEVPATH "/dev/adc0"
#endif
#ifndef CONFIG_DOLPHIN_ADC_KEY_CHANNEL
#  define CONFIG_DOLPHIN_ADC_KEY_CHANNEL 14
#endif
#ifndef CONFIG_DOLPHIN_ADC_KEY_RELEASE_MIN
#  define CONFIG_DOLPHIN_ADC_KEY_RELEASE_MIN 0
#endif

#define DOLPHIN_ADC_SAMPLE_MS          20u
#define DOLPHIN_ADC_STABLE_SAMPLES     3u
#define DOLPHIN_ADC_QUEUE_SIZE         4u
#define DOLPHIN_ADC_STABLE_TOLERANCE   8

struct dolphin_adc_key_s
{
  pthread_mutex_t lock;
  bool started;
  bool ready;
  bool down;
  bool long_sent;
  bool candidate;
  int baseline;
  int error;
  uint8_t stable;
  uint8_t head;
  uint8_t count;
  uint32_t down_ms;
  enum dolphin_adc_key_event_e events[DOLPHIN_ADC_QUEUE_SIZE];
};

static struct dolphin_adc_key_s g_key =
{
  .lock = PTHREAD_MUTEX_INITIALIZER
};

static void dolphin_adc_key_reset(struct dolphin_adc_key_s *key, int error)
{
  key->ready = false;
  key->down = false;
  key->long_sent = false;
  key->candidate = false;
  key->baseline = 0;
  key->stable = 0;
  key->head = 0;
  key->count = 0;
  key->error = error;
}

static void dolphin_adc_key_fail(struct dolphin_adc_key_s *key, int error)
{
  dolphin_adc_key_reset(key, error);
  key->events[0] = DOLPHIN_ADC_KEY_ERROR;
  key->count = 1;
}

static bool dolphin_adc_key_put(struct dolphin_adc_key_s *key,
                                enum dolphin_adc_key_event_e event)
{
  if (key->count >= DOLPHIN_ADC_QUEUE_SIZE)
    {
      dolphin_adc_key_fail(key, -ENOSPC);
      return false;
    }

  key->events[(key->head + key->count) % DOLPHIN_ADC_QUEUE_SIZE] = event;
  key->count++;
  return true;
}

static void dolphin_adc_key_sample(struct dolphin_adc_key_s *key, int value,
                                   uint32_t now)
{
  bool pressed;

  if (!key->ready)
    {
      if (value < CONFIG_DOLPHIN_ADC_KEY_RELEASE_MIN)
        {
          key->stable = 0;
          return;
        }

      if (key->stable == 0 ||
          value - key->baseline > DOLPHIN_ADC_STABLE_TOLERANCE ||
          key->baseline - value > DOLPHIN_ADC_STABLE_TOLERANCE)
        {
          key->baseline = value;
          key->stable = 1;
        }
      else if (++key->stable >= DOLPHIN_ADC_STABLE_SAMPLES)
        {
          key->ready = true;
          key->stable = 0;
        }
      return;
    }

  pressed = key->down ? value < key->baseline * 3 / 4 :
                        value <= key->baseline / 4;
  if (pressed != key->candidate)
    {
      key->candidate = pressed;
      key->stable = 1;
    }
  else if (key->stable < DOLPHIN_ADC_STABLE_SAMPLES)
    {
      key->stable++;
    }

  if (key->stable < DOLPHIN_ADC_STABLE_SAMPLES)
    {
      return;
    }

  if (pressed && !key->down)
    {
      key->down = true;
      key->long_sent = false;
      key->down_ms = now;
    }

  if (key->down && !key->long_sent && now - key->down_ms >= 1500)
    {
      if (dolphin_adc_key_put(key, DOLPHIN_ADC_KEY_HOME))
        {
          key->long_sent = true;
        }
    }

  if (!pressed && key->down)
    {
      uint32_t held = now - key->down_ms;

      key->down = false;
      if (!key->long_sent)
        {
          dolphin_adc_key_put(key, held < 400 ? DOLPHIN_ADC_KEY_NEXT :
                                               DOLPHIN_ADC_KEY_CONFIRM);
        }
    }
}

static void *dolphin_adc_key_worker(void *arg)
{
  struct dolphin_adc_key_s *key = arg;
  struct adc_msg_s msg;
  ssize_t length;
  int64_t raw;
  int fd;
  int ret;

  fd = open(CONFIG_DOLPHIN_ADC_KEY_DEVPATH, O_RDONLY);
  ret = fd < 0 ? -errno : 0;
  while (ret == 0)
    {
      if (ioctl(fd, ANIOC_TRIGGER, 0) < 0)
        {
          ret = -errno;
          break;
        }

      length = read(fd, &msg, sizeof(msg));
      if (length < 0)
        {
          ret = -errno;
          break;
        }
      if (length != sizeof(msg))
        {
          ret = -EIO;
          break;
        }
      raw = (int64_t)msg.am_data;
      if (msg.am_channel != CONFIG_DOLPHIN_ADC_KEY_CHANNEL || raw < 0 ||
          raw > UINT16_MAX)
        {
          ret = -EINVAL;
          break;
        }

      pthread_mutex_lock(&key->lock);
      dolphin_adc_key_sample(key, (int)raw,
                             TICK2MSEC(clock_systime_ticks()));
      pthread_mutex_unlock(&key->lock);
      usleep(DOLPHIN_ADC_SAMPLE_MS * 1000);
    }

  if (fd >= 0)
    {
      close(fd);
    }

  pthread_mutex_lock(&key->lock);
  key->started = false;
  dolphin_adc_key_fail(key, ret != 0 ? ret : -EIO);
  pthread_mutex_unlock(&key->lock);
  return NULL;
}

int dolphin_adc_key_start(void)
{
  pthread_t thread;
  pthread_attr_t attr;
  int ret;

#ifndef CONFIG_DOLPHIN_ADC_KEY_ACTIVE_LOW
  pthread_mutex_lock(&g_key.lock);
  dolphin_adc_key_fail(&g_key, -ENOTSUP);
  pthread_mutex_unlock(&g_key.lock);
  return -ENOTSUP;
#endif
  if (CONFIG_DOLPHIN_ADC_KEY_RELEASE_MIN <= 0 ||
      CONFIG_DOLPHIN_ADC_KEY_RELEASE_MIN > UINT16_MAX)
    {
      pthread_mutex_lock(&g_key.lock);
      dolphin_adc_key_fail(&g_key, -EINVAL);
      pthread_mutex_unlock(&g_key.lock);
      return -EINVAL;
    }

  pthread_mutex_lock(&g_key.lock);
  if (g_key.started)
    {
      pthread_mutex_unlock(&g_key.lock);
      return 0;
    }
  g_key.started = true;
  pthread_mutex_unlock(&g_key.lock);

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr, 4096);
      if (ret == 0)
        {
          ret = pthread_create(&thread, &attr, dolphin_adc_key_worker,
                               &g_key);
        }
      pthread_attr_destroy(&attr);
    }
  if (ret != 0)
    {
      pthread_mutex_lock(&g_key.lock);
      g_key.started = false;
      dolphin_adc_key_fail(&g_key, -ret);
      pthread_mutex_unlock(&g_key.lock);
      return -ret;
    }

  pthread_detach(thread);
  return 0;
}

int dolphin_adc_key_poll(enum dolphin_adc_key_event_e *event, int *error)
{
  if (event == NULL || error == NULL)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_key.lock);
  if (g_key.count == 0)
    {
      pthread_mutex_unlock(&g_key.lock);
      return -EAGAIN;
    }

  *event = g_key.events[g_key.head];
  g_key.head = (g_key.head + 1) % DOLPHIN_ADC_QUEUE_SIZE;
  g_key.count--;
  *error = g_key.error;
  pthread_mutex_unlock(&g_key.lock);
  return 0;
}
