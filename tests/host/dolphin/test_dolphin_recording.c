/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int recording_test_pthread_create(pthread_t *thread,
                                         const pthread_attr_t *attr,
                                         void *(*entry)(void *), void *arg);
#define pthread_create recording_test_pthread_create
#include "../../../app/dolphin/dolphin_recording.c"
#undef pthread_create

enum recording_test_mode_e
{
  RECORDING_TEST_WAIT,
  RECORDING_TEST_ENOSPC,
  RECORDING_TEST_OVERSIZE
};

static pthread_mutex_t g_mock_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_mock_cond = PTHREAD_COND_INITIALIZER;
static enum recording_test_mode_e g_mock_mode;
static bool g_mock_first_frame;
static bool g_mock_release;
static int g_mock_close_error;
static int g_mock_create_error;

void *media_recorder_open(const char *params)
{
  return params != NULL ? (void *)0x1 : NULL;
}

int media_recorder_prepare(void *handle, const char *url, const char *options)
{
  return handle != NULL && url == NULL &&
         strcmp(options, "format=s16le:sample_rate=16000:ch_layout=mono") == 0 ?
         0 : -EINVAL;
}

int media_recorder_start(void *handle)
{
  return handle != NULL ? 0 : -EINVAL;
}

ssize_t media_recorder_read_data(void *handle, void *data, size_t len)
{
  uint8_t *pcm = data;

  assert(handle != NULL);
  assert(len >= 640);
  pthread_mutex_lock(&g_mock_lock);
  if (g_mock_mode == RECORDING_TEST_ENOSPC)
    {
      pthread_mutex_unlock(&g_mock_lock);
      return -ENOSPC;
    }
  if (g_mock_mode == RECORDING_TEST_OVERSIZE)
    {
      pthread_mutex_unlock(&g_mock_lock);
      return 642;
    }
  if (!g_mock_first_frame)
    {
      g_mock_first_frame = true;
      pthread_cond_broadcast(&g_mock_cond);
      pthread_mutex_unlock(&g_mock_lock);
      memset(pcm, 0, 640);
      pcm[0] = 0xff;
      pcm[1] = 0x7f;
      return 640;
    }
  while (!g_mock_release)
    pthread_cond_wait(&g_mock_cond, &g_mock_lock);
  pthread_mutex_unlock(&g_mock_lock);
  return -EPIPE;
}

int media_recorder_stop(void *handle)
{
  return handle != NULL ? 0 : -EINVAL;
}

int media_recorder_close(void *handle)
{
  assert(handle != NULL);
  return g_mock_close_error;
}

static int recording_test_pthread_create(pthread_t *thread,
                                         const pthread_attr_t *attr,
                                         void *(*entry)(void *), void *arg)
{
  if (g_mock_create_error != 0)
    return g_mock_create_error;
  return pthread_create(thread, attr, entry, arg);
}

static int recording_test_file(char path[])
{
  int fd;

  strcpy(path, "/tmp/dolphin-recording-XXXXXX");
  fd = mkstemp(path);
  assert(fd >= 0);
  return fd;
}

static struct dolphin_recording_snapshot_s recording_test_wait_terminal(void)
{
  struct dolphin_recording_snapshot_s snapshot;

  for (unsigned int attempts = 0; attempts < 500; attempts++)
    {
      assert(dolphin_recording_snapshot(&snapshot) == 0);
      if (snapshot.state == DOLPHIN_RECORDING_SAVED ||
          snapshot.state == DOLPHIN_RECORDING_FAILED ||
          snapshot.state == DOLPHIN_RECORDING_CLEANUP_FAILED)
        return snapshot;
      usleep(1000);
    }
  assert(!"recorder worker timed out");
  return snapshot;
}

static uint32_t recording_test_le32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void recording_test_stop_writes_pcm_wav(void)
{
  char path[64];
  uint8_t header[44];
  struct dolphin_recording_snapshot_s snapshot;
  int fd = recording_test_file(path);

  g_mock_mode = RECORDING_TEST_WAIT;
  g_mock_first_frame = false;
  g_mock_release = false;
  g_mock_close_error = 0;
  assert(dolphin_recording_begin(fd) == 0);
  pthread_mutex_lock(&g_mock_lock);
  while (!g_mock_first_frame)
    pthread_cond_wait(&g_mock_cond, &g_mock_lock);
  pthread_mutex_unlock(&g_mock_lock);
  assert(dolphin_recording_stop() == 0);
  pthread_mutex_lock(&g_mock_lock);
  g_mock_release = true;
  pthread_cond_broadcast(&g_mock_cond);
  pthread_mutex_unlock(&g_mock_lock);
  snapshot = recording_test_wait_terminal();
  assert(snapshot.state == DOLPHIN_RECORDING_SAVED);
  assert(snapshot.bytes == 640 && snapshot.milliseconds == 20);
  fd = open(path, O_RDONLY);
  assert(fd >= 0 && read(fd, header, sizeof(header)) == (ssize_t)sizeof(header));
  assert(memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0);
  assert(recording_test_le32(header + 24) == 16000);
  assert(header[22] == 1 && header[34] == 16);
  assert(recording_test_le32(header + 40) == 640);
  close(fd);
  unlink(path);
}

static void recording_test_error_and_launch_ownership(void)
{
  char path[64];
  struct dolphin_recording_snapshot_s snapshot;
  int fd = recording_test_file(path);

  g_mock_mode = RECORDING_TEST_ENOSPC;
  g_mock_close_error = 0;
  assert(dolphin_recording_begin(fd) == 0);
  snapshot = recording_test_wait_terminal();
  assert(snapshot.state == DOLPHIN_RECORDING_FAILED && snapshot.error == -ENOSPC);
  unlink(path);

  fd = recording_test_file(path);
  g_mock_mode = RECORDING_TEST_OVERSIZE;
  assert(dolphin_recording_begin(fd) == 0);
  snapshot = recording_test_wait_terminal();
  assert(snapshot.state == DOLPHIN_RECORDING_FAILED && snapshot.error == -EPROTO);
  unlink(path);

  fd = recording_test_file(path);
  g_mock_create_error = EAGAIN;
  assert(dolphin_recording_begin(fd) == -EAGAIN);
  assert(fcntl(fd, F_GETFD) >= 0);
  close(fd);
  unlink(path);
  g_mock_create_error = 0;
}

static void recording_test_close_failure_blocks_reuse(void)
{
  char path[64];
  struct dolphin_recording_snapshot_s snapshot;
  int fd = recording_test_file(path);

  g_mock_mode = RECORDING_TEST_WAIT;
  g_mock_first_frame = false;
  g_mock_release = false;
  g_mock_close_error = -EIO;
  assert(dolphin_recording_begin(fd) == 0);
  pthread_mutex_lock(&g_mock_lock);
  while (!g_mock_first_frame)
    pthread_cond_wait(&g_mock_cond, &g_mock_lock);
  pthread_mutex_unlock(&g_mock_lock);
  assert(dolphin_recording_stop() == 0);
  pthread_mutex_lock(&g_mock_lock);
  g_mock_release = true;
  pthread_cond_broadcast(&g_mock_cond);
  pthread_mutex_unlock(&g_mock_lock);
  snapshot = recording_test_wait_terminal();
  assert(snapshot.state == DOLPHIN_RECORDING_CLEANUP_FAILED);
  fd = recording_test_file(path);
  assert(dolphin_recording_begin(fd) == -EBUSY);
  close(fd);
  unlink(path);
}

int main(void)
{
  recording_test_stop_writes_pcm_wav();
  recording_test_error_and_launch_ownership();
  recording_test_close_failure_blocks_reuse();
  puts("dolphin recording host checks passed");
  return 0;
}
