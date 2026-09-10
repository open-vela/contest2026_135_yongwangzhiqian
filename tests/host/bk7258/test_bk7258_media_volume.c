/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_media_volume.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

static int acquire_error;
static int release_error;
static int leases;
int bk7258_usbmode_blockdev_acquire(void)
{
  if (acquire_error)
    {
      return acquire_error;
    }
  leases++;
  return 0;
}
int bk7258_usbmode_blockdev_release(void)
{
  if (release_error)
    {
      return release_error;
    }
  assert(leases == 1);
  leases--;
  return 0;
}
int main(void)
{
  assert(bk7258_media_volume_acquire(0) == -EINVAL);
  acquire_error = -EBUSY;
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
  acquire_error = 0;
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == 0);
  assert(leases == 1);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == -EPERM);
  release_error = -EIO;
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_VISION) == -EIO);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
  release_error = 0;
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_VISION) == 0);
  assert(leases == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_PREFERENCES) == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_PREFERENCES) == 0);
  assert(leases == 0);
  puts("BKVISION_VOLUME_HOST_TEST_PASS");
  return 0;
}
