/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "bk7258_media_volume.h"
#include <errno.h>
#ifdef CONFIG_BK7258_USBMODE
#  include <arch/chip/bk7258_usbmode.h>
#endif

static int g_volume_owner;

int bk7258_media_volume_acquire(enum bk7258_media_volume_owner_e owner)
{
  int expected = 0;
  if (owner != BK7258_MEDIA_VOLUME_DISPLAY &&
      owner != BK7258_MEDIA_VOLUME_VISION &&
      owner != BK7258_MEDIA_VOLUME_PREFERENCES)
    {
      return -EINVAL;
    }
  if (!__atomic_compare_exchange_n(&g_volume_owner, &expected, owner, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }
#ifdef CONFIG_BK7258_USBMODE
  {
    int ret = bk7258_usbmode_blockdev_acquire();
    if (ret < 0)
      {
        __atomic_store_n(&g_volume_owner, 0, __ATOMIC_RELEASE);
        return ret;
      }
  }
#endif
  return 0;
}

int bk7258_media_volume_release(enum bk7258_media_volume_owner_e owner)
{
  if (owner == 0 ||
      __atomic_load_n(&g_volume_owner, __ATOMIC_ACQUIRE) != (int)owner)
    {
      return -EPERM;
    }
#ifdef CONFIG_BK7258_USBMODE
  {
    int ret = bk7258_usbmode_blockdev_release();
    if (ret < 0)
      {
        return ret;
      }
  }
#endif
  __atomic_store_n(&g_volume_owner, 0, __ATOMIC_RELEASE);
  return 0;
}
