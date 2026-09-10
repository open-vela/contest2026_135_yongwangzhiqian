/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_volume_store.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#define _POSIX_C_SOURCE 200809L

#include "bk7258_voice_volume_store.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nuttx/mutex.h>

static int g_fsync_calls;
static int g_fail_fsync_call;
static int g_fail_rename;

int nxmutex_lock(mutex_t *mutex)
{
  return -pthread_mutex_lock(mutex);
}

int nxmutex_unlock(mutex_t *mutex)
{
  return -pthread_mutex_unlock(mutex);
}

int __real_fsync(int fd);
int __real_rename(const char *oldpath, const char *newpath);

int __wrap_fsync(int fd)
{
  g_fsync_calls++;
  if (g_fail_fsync_call == g_fsync_calls)
    {
      errno = EIO;
      return -1;
    }

  return __real_fsync(fd);
}

int __wrap_rename(const char *oldpath, const char *newpath)
{
  if (g_fail_rename)
    {
      errno = EIO;
      return -1;
    }

  return __real_rename(oldpath, newpath);
}

static void reset_faults(void)
{
  g_fsync_calls = 0;
  g_fail_fsync_call = 0;
  g_fail_rename = 0;
}

int main(int argc, char **argv)
{
  char active[256];
  unsigned int volume = 101;
  uint8_t corrupt = 0xff;
  int fd;

  assert(argc == 2);
  assert(bkvoice_volume_store_start(NULL) == -EINVAL);
  assert(bkvoice_volume_store_start("relative") == -EINVAL);
  assert(bkvoice_volume_store_get(NULL) == -EINVAL);
  assert(bkvoice_volume_store_set(101) == -ERANGE);
  assert(bkvoice_volume_store_reload() == -ENODEV);
  assert(bkvoice_volume_store_start(argv[1]) == 0);
  assert(bkvoice_volume_store_start(argv[1]) == -EALREADY);

  assert(bkvoice_volume_store_get(&volume) == -ENOENT && volume == 101);
  assert(bkvoice_volume_store_set(73) == 0);
  assert(bkvoice_volume_store_get(&volume) == 0 && volume == 73);
  assert(bkvoice_volume_store_reload() == 0);
  assert(bkvoice_volume_store_get(&volume) == 0 && volume == 73);

  /* A failed rename leaves the old active file selected. */

  reset_faults();
  g_fail_rename = 1;
  assert(bkvoice_volume_store_set(64) == -EINPROGRESS);
  reset_faults();
  assert(bkvoice_volume_store_reload() == 0);
  assert(bkvoice_volume_store_get(&volume) == 0 && volume == 73);
  assert(bkvoice_volume_store_set(64) == 0);

  /* Failure syncing the directory follows publication. Reload determines
   * that the new active record won the uncertain operation.
   */

  reset_faults();
  g_fail_fsync_call = 2;
  assert(bkvoice_volume_store_set(70) == -EINPROGRESS);
  reset_faults();
  assert(bkvoice_volume_store_reload() == 0);
  assert(bkvoice_volume_store_get(&volume) == 0 && volume == 70);

  /* Digest corruption is never treated as first boot and cannot be silently
   * overwritten by a new setting.
   */

  assert(snprintf(active, sizeof(active), "%s/config.bin", argv[1]) > 0);
  fd = open(active, O_WRONLY);
  assert(fd >= 0);
  assert(pwrite(fd, &corrupt, sizeof(corrupt), 65) == 1);
  assert(close(fd) == 0);
  assert(bkvoice_volume_store_reload() == 0);
  assert(bkvoice_volume_store_get(&volume) == -EBADMSG);
  assert(bkvoice_volume_store_set(42) == -EBADMSG);

  puts("BKVOICE_VOLUME_STORE_HOST_PASS");
  return 0;
}
