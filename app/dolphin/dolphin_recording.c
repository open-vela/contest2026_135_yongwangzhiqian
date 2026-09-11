/****************************************************************************
 * app/dolphin/dolphin_recording.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <media_recorder.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "dolphin_recording.h"

#define DOLPHIN_RECORD_RATE       16000u
#define DOLPHIN_RECORD_BITS       16u
#define DOLPHIN_RECORD_CHANNELS   1u
#define DOLPHIN_RECORD_READ_BYTES 640u
#define DOLPHIN_RECORD_HEADER     44u
#define DOLPHIN_RECORD_MAX_BYTES  (UINT32_MAX - DOLPHIN_RECORD_HEADER)
#define DOLPHIN_RECORD_OPTIONS \
  "format=s16le:sample_rate=16000:ch_layout=mono"

struct dolphin_recording_s
{
  pthread_mutex_t lock;
  void *handle;
  int fd;
  enum dolphin_recording_state_e state;
  int error;
  uint32_t bytes;
  uint16_t peak;
  uint32_t clipped;
  bool stop_requested;
  bool worker_running;
};

static struct dolphin_recording_s g_recording =
{
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .fd = -1,
  .state = DOLPHIN_RECORDING_IDLE
};

static void dolphin_recording_put16(uint8_t *dest, uint16_t value)
{
  dest[0] = (uint8_t)value;
  dest[1] = (uint8_t)(value >> 8);
}

static void dolphin_recording_put32(uint8_t *dest, uint32_t value)
{
  dest[0] = (uint8_t)value;
  dest[1] = (uint8_t)(value >> 8);
  dest[2] = (uint8_t)(value >> 16);
  dest[3] = (uint8_t)(value >> 24);
}

static int dolphin_recording_write_all(int fd, const uint8_t *data, size_t length)
{
  size_t written = 0;

  while (written < length)
    {
      ssize_t ret = write(fd, data + written, length - written);
      if (ret <= 0)
        {
          return ret < 0 ? -errno : -EIO;
        }
      written += (size_t)ret;
    }

  return 0;
}

static int dolphin_recording_write_header(int fd, uint32_t bytes)
{
  uint8_t header[DOLPHIN_RECORD_HEADER] = {0};

  memcpy(header, "RIFF", 4);
  dolphin_recording_put32(header + 4, 36u + bytes);
  memcpy(header + 8, "WAVEfmt ", 8);
  dolphin_recording_put32(header + 16, 16);
  dolphin_recording_put16(header + 20, 1);
  dolphin_recording_put16(header + 22, DOLPHIN_RECORD_CHANNELS);
  dolphin_recording_put32(header + 24, DOLPHIN_RECORD_RATE);
  dolphin_recording_put32(header + 28,
                           DOLPHIN_RECORD_RATE * DOLPHIN_RECORD_CHANNELS * 2u);
  dolphin_recording_put16(header + 32, DOLPHIN_RECORD_CHANNELS * 2u);
  dolphin_recording_put16(header + 34, DOLPHIN_RECORD_BITS);
  memcpy(header + 36, "data", 4);
  dolphin_recording_put32(header + 40, bytes);
  if (lseek(fd, 0, SEEK_SET) < 0)
    {
      return -errno;
    }
  return dolphin_recording_write_all(fd, header, sizeof(header));
}

static void dolphin_recording_measure(const uint8_t *data, size_t bytes)
{
  uint16_t peak = 0;
  uint32_t clipped = 0;

  for (size_t offset = 0; offset + 1 < bytes; offset += 2)
    {
      int16_t sample = (int16_t)((uint16_t)data[offset] |
                                 ((uint16_t)data[offset + 1] << 8));
      uint16_t magnitude = sample < 0 ? (uint16_t)(-(int32_t)sample) :
                                        (uint16_t)sample;
      if (magnitude > peak) peak = magnitude;
      if (magnitude >= 32767u && clipped != UINT32_MAX) clipped++;
    }

  pthread_mutex_lock(&g_recording.lock);
  if (peak > g_recording.peak) g_recording.peak = peak;
  if (UINT32_MAX - g_recording.clipped < clipped)
    g_recording.clipped = UINT32_MAX;
  else
    g_recording.clipped += clipped;
  pthread_mutex_unlock(&g_recording.lock);
}

static void dolphin_recording_finish(int error, bool started)
{
  void *handle;
  int fd;
  uint32_t bytes;
  int ret = 0;

  pthread_mutex_lock(&g_recording.lock);
  handle = g_recording.handle;
  fd = g_recording.fd;
  bytes = g_recording.bytes;
  g_recording.state = DOLPHIN_RECORDING_STOPPING;
  pthread_mutex_unlock(&g_recording.lock);

  if (started && handle != NULL)
    {
      ret = media_recorder_stop(handle);
      if (error == 0 && ret < 0) error = ret;
    }
  if (fd >= 0 && error == 0)
    {
      ret = dolphin_recording_write_header(fd, bytes);
      if (ret < 0) error = ret;
    }
  if (fd >= 0 && error == 0 && fsync(fd) < 0) error = -errno;
  if (fd >= 0 && close(fd) < 0 && error == 0) error = -errno;
  if (handle != NULL)
    {
      ret = media_recorder_close(handle);
      if (ret < 0 && error == 0) error = ret;
    }

  pthread_mutex_lock(&g_recording.lock);
  if (handle != NULL && ret < 0)
    {
      /* The ABI retains the handle after incomplete close. Do not start a
       * second recorder against possibly reserved hardware. */
      g_recording.handle = handle;
      g_recording.fd = -1;
      g_recording.error = error != 0 ? error : ret;
      g_recording.state = DOLPHIN_RECORDING_CLEANUP_FAILED;
    }
  else
    {
      g_recording.handle = NULL;
      g_recording.fd = -1;
      g_recording.error = error;
      g_recording.state = error == 0 ? DOLPHIN_RECORDING_SAVED :
                                       DOLPHIN_RECORDING_FAILED;
    }
  g_recording.worker_running = false;
  pthread_mutex_unlock(&g_recording.lock);
}

static void *dolphin_recording_worker(void *arg)
{
  uint8_t pcm[DOLPHIN_RECORD_READ_BYTES];
  void *handle;
  int fd;
  int error = 0;
  bool started = false;

  (void)arg;
  pthread_mutex_lock(&g_recording.lock);
  handle = g_recording.handle;
  fd = g_recording.fd;
  pthread_mutex_unlock(&g_recording.lock);

  if ((handle = media_recorder_open(MEDIA_SOURCE_MIC)) == NULL)
    {
      error = errno > 0 ? -errno : -EIO;
      dolphin_recording_finish(error, false);
      return NULL;
    }
  pthread_mutex_lock(&g_recording.lock);
  g_recording.handle = handle;
  pthread_mutex_unlock(&g_recording.lock);
  error = media_recorder_prepare(handle, NULL, DOLPHIN_RECORD_OPTIONS);
  if (error == 0) error = dolphin_recording_write_header(fd, 0);
  if (error == 0) error = media_recorder_start(handle);
  if (error == 0)
    {
      started = true;
      pthread_mutex_lock(&g_recording.lock);
      g_recording.state = DOLPHIN_RECORDING_ACTIVE;
      pthread_mutex_unlock(&g_recording.lock);
    }

  while (error == 0)
    {
      bool stop;
      pthread_mutex_lock(&g_recording.lock);
      stop = g_recording.stop_requested;
      pthread_mutex_unlock(&g_recording.lock);
      if (stop) break;
      ssize_t count = media_recorder_read_data(handle, pcm, sizeof(pcm));
      pthread_mutex_lock(&g_recording.lock);
      stop = g_recording.stop_requested;
      pthread_mutex_unlock(&g_recording.lock);
      if (count < 0)
        {
          error = stop && count == -EPIPE ? 0 : (int)count;
          break;
        }
      if (count == 0 || (size_t)count > sizeof(pcm) ||
          ((size_t)count & 1u) != 0 ||
          (uint32_t)count > DOLPHIN_RECORD_MAX_BYTES)
        {
          error = count == 0 ? -EPIPE : -EPROTO;
          break;
        }
      pthread_mutex_lock(&g_recording.lock);
      if (g_recording.bytes > DOLPHIN_RECORD_MAX_BYTES - (uint32_t)count)
        {
          pthread_mutex_unlock(&g_recording.lock);
          error = -EFBIG;
          break;
        }
      pthread_mutex_unlock(&g_recording.lock);
      error = dolphin_recording_write_all(fd, pcm, (size_t)count);
      if (error != 0) break;
      dolphin_recording_measure(pcm, (size_t)count);
      pthread_mutex_lock(&g_recording.lock);
      g_recording.bytes += (uint32_t)count;
      stop = g_recording.stop_requested;
      pthread_mutex_unlock(&g_recording.lock);
      if (stop) break;
    }

  dolphin_recording_finish(error, started);
  return NULL;
}

int dolphin_recording_begin(int fd)
{
  pthread_attr_t attr;
  pthread_t thread;
  int flags;
  int ret;

  if (fd < 0 || (flags = fcntl(fd, F_GETFL)) < 0 ||
      (flags & O_ACCMODE) == O_RDONLY || lseek(fd, 0, SEEK_CUR) != 0)
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_recording.lock);
  if (g_recording.worker_running || g_recording.handle != NULL ||
      g_recording.state == DOLPHIN_RECORDING_CLEANUP_FAILED)
    {
      pthread_mutex_unlock(&g_recording.lock);
      return -EBUSY;
    }
  g_recording.fd = fd;
  g_recording.bytes = 0;
  g_recording.peak = 0;
  g_recording.clipped = 0;
  g_recording.error = 0;
  g_recording.stop_requested = false;
  g_recording.worker_running = true;
  g_recording.state = DOLPHIN_RECORDING_STARTING;
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr, 16384);
      if (ret == 0) ret = pthread_create(&thread, &attr, dolphin_recording_worker, NULL);
      pthread_attr_destroy(&attr);
    }
  if (ret != 0)
    {
      g_recording.fd = -1;
      g_recording.worker_running = false;
      g_recording.state = DOLPHIN_RECORDING_IDLE;
      pthread_mutex_unlock(&g_recording.lock);
      return -ret;
    }
  pthread_detach(thread);
  pthread_mutex_unlock(&g_recording.lock);
  return 0;
}

int dolphin_recording_stop(void)
{
  pthread_mutex_lock(&g_recording.lock);
  if (!g_recording.worker_running)
    {
      pthread_mutex_unlock(&g_recording.lock);
      return -EALREADY;
    }
  g_recording.stop_requested = true;
  if (g_recording.state == DOLPHIN_RECORDING_ACTIVE)
    g_recording.state = DOLPHIN_RECORDING_STOPPING;
  pthread_mutex_unlock(&g_recording.lock);
  return 0;
}

int dolphin_recording_snapshot(struct dolphin_recording_snapshot_s *snapshot)
{
  if (snapshot == NULL) return -EINVAL;
  pthread_mutex_lock(&g_recording.lock);
  snapshot->state = g_recording.state;
  snapshot->error = g_recording.error;
  snapshot->bytes = g_recording.bytes;
  snapshot->milliseconds = g_recording.bytes / 32u;
  snapshot->peak = g_recording.peak;
  snapshot->clipped = g_recording.clipped;
  pthread_mutex_unlock(&g_recording.lock);
  return 0;
}
