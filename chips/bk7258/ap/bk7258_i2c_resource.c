/****************************************************************************
 * chips/bk7258/ap/bk7258_i2c_resource.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-wide ownership for the Beken SDK I2C driver root.  The SDK exposes one
 * global driver init/deinit pair shared by both hardware controllers, while
 * individual clients initialize and deinitialize their own controller.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>

#include <nuttx/mutex.h>

#include <arch/chip/bk7258_i2c.h>

#include <common/bk_err.h>
#include <driver/i2c.h>
#include <driver/i2c_types.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

static mutex_t g_bk7258_i2c_driver_lock = NXMUTEX_INITIALIZER;
static uint32_t g_bk7258_i2c_driver_refs;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_i2c_driver_result(bk_err_t result)
{
  switch (result)
    {
      case BK_OK:
        return 0;

      case BK_ERR_I2C_ACK_TIMEOUT:
        return -ENXIO;

      case BK_ERR_I2C_SCL_TIMEOUT:
        return -ETIMEDOUT;

      case BK_ERR_I2C_NOT_INIT:
      case BK_ERR_I2C_ID_NOT_INIT:
        return -EAGAIN;

      case BK_ERR_I2C_INVALID_ID:
        return -EINVAL;

      case BK_ERR_I2C_SM_BUS_BUSY:
        return -EBUSY;

      default:
        return -EIO;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_i2c_driver_acquire(void)
{
  bk_err_t sdkret;
  int ret;

  ret = nxmutex_lock(&g_bk7258_i2c_driver_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_i2c_driver_refs == UINT32_MAX)
    {
      nxmutex_unlock(&g_bk7258_i2c_driver_lock);
      return -EOVERFLOW;
    }

  if (g_bk7258_i2c_driver_refs == 0)
    {
      sdkret = bk_i2c_driver_init();
      ret = bk7258_i2c_driver_result(sdkret);
      if (ret < 0)
        {
          nxmutex_unlock(&g_bk7258_i2c_driver_lock);
          return ret;
        }
    }

  g_bk7258_i2c_driver_refs++;
  nxmutex_unlock(&g_bk7258_i2c_driver_lock);
  return 0;
}

int bk7258_i2c_driver_release(void)
{
  bk_err_t sdkret;
  int ret;

  ret = nxmutex_lock(&g_bk7258_i2c_driver_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (g_bk7258_i2c_driver_refs == 0)
    {
      nxmutex_unlock(&g_bk7258_i2c_driver_lock);
      return -EALREADY;
    }

  if (g_bk7258_i2c_driver_refs == 1)
    {
      sdkret = bk_i2c_driver_deinit();
      ret = bk7258_i2c_driver_result(sdkret);
      if (ret < 0)
        {
          /* Keep the final reference when the SDK could not tear down its
           * root.  The same owner can retry instead of silently exposing a
           * false zero-reference state to another controller.
           */

          nxmutex_unlock(&g_bk7258_i2c_driver_lock);
          return ret;
        }
    }

  g_bk7258_i2c_driver_refs--;
  nxmutex_unlock(&g_bk7258_i2c_driver_lock);
  return 0;
}
