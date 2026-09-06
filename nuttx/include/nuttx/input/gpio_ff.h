/****************************************************************************
 * include/nuttx/input/gpio_ff.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_INPUT_GPIO_FF_H
#define __INCLUDE_NUTTX_INPUT_GPIO_FF_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/compiler.h>
#include <nuttx/input/ff.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* set_output() may run in interrupt context and must not block.  set_power()
 * runs in task context (LPWORK or device destruction) and may sleep.
 */

struct gpio_ff_config_s
{
  FAR void *arg;
  CODE int (*set_output)(FAR void *arg, bool on);
  CODE int (*set_power)(FAR void *arg, bool on);
  uint32_t max_on_ms;
  uint32_t min_off_ms;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

int gpio_ff_register(FAR const char *path,
                     FAR const struct gpio_ff_config_s *config,
                     FAR struct ff_lowerhalf_s **lower_out);
int gpio_ff_inhibit(FAR struct ff_lowerhalf_s *lower, bool inhibited);

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_INPUT_GPIO_FF_H */
