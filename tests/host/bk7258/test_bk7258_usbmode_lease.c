/****************************************************************************
 * tests/host/bk7258/test_bk7258_usbmode_lease.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/chip/bk7258_usbmode.h>

static unsigned int g_cdc_starts;
static unsigned int g_cdc_stops;
static unsigned int g_msc_starts;
static unsigned int g_msc_stops;

int bk7258_usbcdc_initialize(void)
{
  g_cdc_starts++;
  return 0;
}

int bk7258_usbcdc_uninitialize(void)
{
  g_cdc_stops++;
  return 0;
}

int bk7258_usbmsc_initialize(const char *blockdev)
{
  assert(strcmp(blockdev, "/dev/mmcsd0") == 0);
  g_msc_starts++;
  return 0;
}

int bk7258_usbmsc_uninitialize(void)
{
  g_msc_stops++;
  return 0;
}

int nxsig_usleep(uint32_t usec)
{
  assert(usec == 1000u);
  return 0;
}

int main(void)
{
  assert(bk7258_usbmode_get() == BK7258_USBMODE_NONE);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
  assert(bk7258_usbmode_blockdev_release() == -EINVAL);

  assert(bk7258_usbmode_initialize() == 0);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(g_cdc_starts == 1 && g_cdc_stops == 0);

  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(g_cdc_stops == 0 && g_msc_starts == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == -EBUSY);
  assert(bk7258_usbmode_blockdev_release() == 0);

  assert(bk7258_usbmode_set(BK7258_USBMODE_MSC) == 0);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_MSC);
  assert(g_cdc_stops == 1 && g_msc_starts == 1);
  assert(bk7258_usbmode_blockdev_acquire() == -EBUSY);

  assert(bk7258_usbmode_set(BK7258_USBMODE_CDC) == 0);
  assert(bk7258_usbmode_get() == BK7258_USBMODE_CDC);
  assert(g_msc_stops == 1 && g_cdc_starts == 2);
  assert(bk7258_usbmode_blockdev_acquire() == 0);
  assert(bk7258_usbmode_blockdev_release() == 0);
  assert(bk7258_usbmode_set(BK7258_USBMODE_NONE) == -EINVAL);

  printf("BK7258_USBMODE_LEASE_HOST_PASS\n");
  return 0;
}
