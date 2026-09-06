/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_ARCH_CHIP_BK7258_PM_H
#define __MOCK_ARCH_CHIP_BK7258_PM_H

enum bk7258_pm_clock_e
{
  BK7258_PM_CLOCK_AUDIO = 8,
};

int bk7258_pm_initialize(void);
int bk7258_pm_clock_get(enum bk7258_pm_clock_e clock);
int bk7258_pm_clock_put(enum bk7258_pm_clock_e clock);

#endif
