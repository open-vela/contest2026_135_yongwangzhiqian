/* SPDX-License-Identifier: Apache-2.0 */

#include <nuttx/config.h>

#ifdef DOLPHIN_HOST_TEST
#  include <fcntl.h>
#  include <pthread.h>
#  include <sys/stat.h>
#  include <unistd.h>
#else
#  include <fcntl.h>
#  include <pthread.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dolphin_copy.h"

#ifndef CONFIG_DOLPHIN_STORAGE_ROOT
#  define CONFIG_DOLPHIN_STORAGE_ROOT "/mnt/tf"
#endif
#ifndef CONFIG_DOLPHIN_USB_ROOT
#  define CONFIG_DOLPHIN_USB_ROOT "/mnt/usb"
#endif

#define DOLPHIN_COPY_PATH_MAX 512u
#define DOLPHIN_COPY_BUFFER   4096u
#ifdef DOLPHIN_COPY_TEST_DELAY_US
#  define DOLPHIN_COPY_DELAY_US DOLPHIN_COPY_TEST_DELAY_US
#else
#  define DOLPHIN_COPY_DELAY_US 0
#endif

struct dolphin_copy_job_s
{
  pthread_mutex_t lock;
  enum dolphin_copy_state_e state;
  uint64_t copied;
  uint64_t total;
  int error;
  bool partial;
  bool cancel_requested;
  char source[DOLPHIN_COPY_PATH_MAX];
  char destination[DOLPHIN_COPY_PATH_MAX];
};

static struct dolphin_copy_job_s g_copy =
{
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .state = DOLPHIN_COPY_IDLE
};

static bool dolphin_copy_under_root(const char *path, const char *root)
{
  size_t length = strlen(root);

  return length > 0 && strncmp(path, root, length) == 0 &&
         path[length] == '/' && path[length + 1] != '\0';
}

static int dolphin_copy_check_path(const char *path, const char *root,
                                   bool source)
{
  struct stat info;
  char current[DOLPHIN_COPY_PATH_MAX];
  const char *cursor;
  size_t used;
  bool last;

  if (!dolphin_copy_under_root(path, root))
    {
      return -EINVAL;
    }
  if (lstat(root, &info) < 0)
    {
      return -errno;
    }
  if (S_ISLNK(info.st_mode) || !S_ISDIR(info.st_mode))
    {
      return -ENOTDIR;
    }

  used = strlen(root);
  memcpy(current, root, used);
  current[used] = '\0';
  cursor = path + used + 1;
  while (*cursor != '\0')
    {
      const char *slash = strchr(cursor, '/');
      size_t component = slash == NULL ? strlen(cursor) :
                         (size_t)(slash - cursor);

      if (component == 0 || (component == 2 && strncmp(cursor, "..", 2) == 0))
        {
          return -EINVAL;
        }
      if (used + component + 1 >= sizeof(current))
        {
          return -ENAMETOOLONG;
        }
      current[used++] = '/';
      memcpy(current + used, cursor, component);
      used += component;
      current[used] = '\0';
      last = slash == NULL;
      if (lstat(current, &info) == 0)
        {
          if (S_ISLNK(info.st_mode))
            {
              return -ELOOP;
            }
          if (!last && !S_ISDIR(info.st_mode))
            {
              return -ENOTDIR;
            }
          if (last && source && !S_ISREG(info.st_mode))
            {
              return -EINVAL;
            }
        }
      else if (errno != ENOENT || !last)
        {
          return -errno;
        }

      if (last)
        {
          return 0;
        }
      cursor = slash + 1;
    }

  return -EINVAL;
}

static int dolphin_copy_validate(const char *source, const char *destination)
{
  bool source_storage = dolphin_copy_under_root(source,
                                                 CONFIG_DOLPHIN_STORAGE_ROOT);
  bool source_usb = dolphin_copy_under_root(source, CONFIG_DOLPHIN_USB_ROOT);
  bool destination_storage = dolphin_copy_under_root(
    destination, CONFIG_DOLPHIN_STORAGE_ROOT);
  bool destination_usb = dolphin_copy_under_root(destination,
                                                 CONFIG_DOLPHIN_USB_ROOT);
  int ret;

  if ((!source_storage && !source_usb) ||
      (!destination_storage && !destination_usb) ||
      source_storage == destination_storage)
    {
      return -EINVAL;
    }

  ret = dolphin_copy_check_path(source, source_storage ?
                                CONFIG_DOLPHIN_STORAGE_ROOT :
                                CONFIG_DOLPHIN_USB_ROOT, true);
  if (ret < 0)
    {
      return ret;
    }
  return dolphin_copy_check_path(destination, destination_storage ?
                                 CONFIG_DOLPHIN_STORAGE_ROOT :
                                 CONFIG_DOLPHIN_USB_ROOT, false);
}

static bool dolphin_copy_cancelled(void)
{
  bool cancelled;

  pthread_mutex_lock(&g_copy.lock);
  cancelled = g_copy.cancel_requested;
  pthread_mutex_unlock(&g_copy.lock);
  return cancelled;
}

static void dolphin_copy_finish(enum dolphin_copy_state_e state, int error,
                                bool partial)
{
  pthread_mutex_lock(&g_copy.lock);
  g_copy.state = state;
  g_copy.error = error;
  g_copy.partial = partial;
  pthread_mutex_unlock(&g_copy.lock);
}

static void *dolphin_copy_worker(void *arg)
{
  (void)arg;
  uint8_t buffer[DOLPHIN_COPY_BUFFER];
  struct stat before;
  struct stat after;
  ssize_t length;
  ssize_t written;
  uint64_t remaining;
  int source_fd = -1;
  int destination_fd = -1;
  int error = 0;
  bool destination_created = false;

  error = dolphin_copy_validate(g_copy.source, g_copy.destination);
  if (error < 0)
    {
      goto done;
    }
  source_fd = open(g_copy.source, O_RDONLY
#ifdef O_CLOEXEC
                   | O_CLOEXEC
#endif
#ifdef O_NOFOLLOW
                   | O_NOFOLLOW
#endif
                   );
  if (source_fd < 0)
    {
      error = -errno;
      goto done;
    }
  if (fstat(source_fd, &before) < 0)
    {
      error = -errno;
      goto done;
    }
  if (!S_ISREG(before.st_mode))
    {
      error = -EINVAL;
      goto done;
    }
  remaining = (uint64_t)before.st_size;
  pthread_mutex_lock(&g_copy.lock);
  g_copy.total = remaining;
  pthread_mutex_unlock(&g_copy.lock);
  if (dolphin_copy_cancelled())
    {
      error = -ECANCELED;
      goto done;
    }

  destination_fd = open(g_copy.destination, O_WRONLY | O_CREAT | O_EXCL
#ifdef O_CLOEXEC
                        | O_CLOEXEC
#endif
                        , 0600);
  if (destination_fd < 0)
    {
      error = -errno;
      goto done;
    }
  destination_created = true;

  while (remaining > 0)
    {
      size_t request = remaining > sizeof(buffer) ? sizeof(buffer) :
                       (size_t)remaining;

      if (dolphin_copy_cancelled())
        {
          error = -ECANCELED;
          goto done;
        }
      length = read(source_fd, buffer, request);
      if (length < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          error = -errno;
          goto done;
        }
      if (length == 0)
        {
          error = -ESTALE;
          goto done;
        }
      if (dolphin_copy_cancelled())
        {
          error = -ECANCELED;
          goto done;
        }
      written = write(destination_fd, buffer, (size_t)length);
      if (written < 0)
        {
          error = -errno;
          goto done;
        }
      if (written != length)
        {
          error = -EIO;
          goto done;
        }
      remaining -= (uint64_t)length;
      pthread_mutex_lock(&g_copy.lock);
      g_copy.copied += (uint64_t)length;
      pthread_mutex_unlock(&g_copy.lock);
#if DOLPHIN_COPY_DELAY_US > 0
      usleep(DOLPHIN_COPY_DELAY_US);
#endif
    }

  if (fstat(source_fd, &after) < 0)
    {
      error = -errno;
      goto done;
    }
  if (after.st_size != before.st_size || after.st_mtime != before.st_mtime)
    {
      error = -ESTALE;
      goto done;
    }
  if (fsync(destination_fd) < 0)
    {
      error = -errno;
      goto done;
    }

done:
  if (destination_fd >= 0 && close(destination_fd) < 0 && error == 0)
    {
      error = -errno;
    }
  if (source_fd >= 0 && close(source_fd) < 0 && error == 0)
    {
      error = -errno;
    }
  if (error == 0)
    {
      dolphin_copy_finish(DOLPHIN_COPY_SUCCEEDED, 0, false);
    }
  else if (error == -ECANCELED)
    {
      dolphin_copy_finish(DOLPHIN_COPY_CANCELED, error, destination_created);
    }
  else
    {
      dolphin_copy_finish(DOLPHIN_COPY_FAILED, error, destination_created);
    }
  return NULL;
}

int dolphin_copy_start(const char *source_path, const char *destination_path)
{
  pthread_t thread;
  pthread_attr_t attr;
  int ret;

  if (source_path == NULL || destination_path == NULL ||
      strlen(source_path) >= DOLPHIN_COPY_PATH_MAX ||
      strlen(destination_path) >= DOLPHIN_COPY_PATH_MAX)
    {
      return -EINVAL;
    }
  if ((!dolphin_copy_under_root(source_path, CONFIG_DOLPHIN_STORAGE_ROOT) &&
       !dolphin_copy_under_root(source_path, CONFIG_DOLPHIN_USB_ROOT)) ||
      (!dolphin_copy_under_root(destination_path,
                                 CONFIG_DOLPHIN_STORAGE_ROOT) &&
       !dolphin_copy_under_root(destination_path, CONFIG_DOLPHIN_USB_ROOT)) ||
      (dolphin_copy_under_root(source_path, CONFIG_DOLPHIN_STORAGE_ROOT) ==
       dolphin_copy_under_root(destination_path, CONFIG_DOLPHIN_STORAGE_ROOT)))
    {
      return -EINVAL;
    }

  pthread_mutex_lock(&g_copy.lock);
  if (g_copy.state == DOLPHIN_COPY_RUNNING)
    {
      pthread_mutex_unlock(&g_copy.lock);
      return -EBUSY;
    }
  snprintf(g_copy.source, sizeof(g_copy.source), "%s", source_path);
  snprintf(g_copy.destination, sizeof(g_copy.destination), "%s",
           destination_path);
  g_copy.state = DOLPHIN_COPY_RUNNING;
  g_copy.copied = 0;
  g_copy.total = 0;
  g_copy.error = 0;
  g_copy.partial = false;
  g_copy.cancel_requested = false;
  pthread_mutex_unlock(&g_copy.lock);

  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr, 16384);
      if (ret == 0)
        {
          ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        }
      if (ret == 0)
        {
          ret = pthread_create(&thread, &attr, dolphin_copy_worker, NULL);
        }
      pthread_attr_destroy(&attr);
    }
  if (ret != 0)
    {
      pthread_mutex_lock(&g_copy.lock);
      g_copy.state = DOLPHIN_COPY_FAILED;
      g_copy.error = -ret;
      g_copy.partial = false;
      pthread_mutex_unlock(&g_copy.lock);
      return -ret;
    }
  return 0;
}

int dolphin_copy_cancel(void)
{
  pthread_mutex_lock(&g_copy.lock);
  if (g_copy.state != DOLPHIN_COPY_RUNNING)
    {
      pthread_mutex_unlock(&g_copy.lock);
      return -EALREADY;
    }
  g_copy.cancel_requested = true;
  pthread_mutex_unlock(&g_copy.lock);
  return 0;
}

int dolphin_copy_snapshot(struct dolphin_copy_snapshot_s *snapshot)
{
  if (snapshot == NULL)
    {
      return -EINVAL;
    }
  pthread_mutex_lock(&g_copy.lock);
  snapshot->state = g_copy.state;
  snapshot->copied = g_copy.copied;
  snapshot->total = g_copy.total;
  snapshot->error = g_copy.error;
  snapshot->partial = g_copy.partial;
  pthread_mutex_unlock(&g_copy.lock);
  return 0;
}
