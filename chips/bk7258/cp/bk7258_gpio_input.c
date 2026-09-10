/****************************************************************************
 * chips/bk7258/cp/bk7258_gpio_input.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal CP GPIO input adapter for board-owned polled consumers.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <arch/chip/bk7258_pinmux.h>

#include <driver/gpio.h>
#include <soc/bk7258/gpio_cap.h>

static int bk7258_gpio_input_result(bk_err_t error)
{
  return error == BK_OK ? OK : -EIO;
}

int bk7258_gpio_configure_input(uint8_t pin,
                                enum bk7258_gpio_pull_e pull)
{
  gpio_config_t config =
  {
    .io_mode = GPIO_INPUT_ENABLE,
    .pull_mode = GPIO_PULL_DISABLE,
    .func_mode = GPIO_SECOND_FUNC_DISABLE,
  };
  int ret;

  if (pin >= SOC_GPIO_NUM || pull > BK7258_GPIO_PULL_UP)
    {
      return -ERANGE;
    }

  switch (pull)
    {
      case BK7258_GPIO_PULL_DOWN:
        config.pull_mode = GPIO_PULL_DOWN_EN;
        break;

      case BK7258_GPIO_PULL_UP:
        config.pull_mode = GPIO_PULL_UP_EN;
        break;

      case BK7258_GPIO_PULL_NONE:
        break;
    }

  ret = bk7258_gpio_input_result(bk_gpio_driver_init());
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_gpio_input_result(
    bk_gpio_set_config((gpio_id_t)pin, &config));
}

int bk7258_gpio_read_input(uint8_t pin, FAR bool *high)
{
  if (pin >= SOC_GPIO_NUM || high == NULL)
    {
      return -EINVAL;
    }

  *high = bk_gpio_get_input((gpio_id_t)pin);
  return OK;
}
