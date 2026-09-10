/****************************************************************************
 * chips/bk7258/include/bk7258_usbmode_rpmsg.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small CP-to-AP control plane for the USB mode manager.  It is deliberately
 * independent of the MCUboot/OTA transport so unsigned direct diagnostic
 * images can provision the soldered SD NAND without enabling Flash writers.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMODE_RPMSG_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMODE_RPMSG_H

#include <nuttx/config.h>

#include <stdint.h>

#include <arch/chip/bk7258_usbmode.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef CONFIG_BK7258_USBMODE_RPMSG
int bk7258_usbmode_rpmsg_initialize(void);

#ifndef CONFIG_BK7258_AP_CORE
int bk7258_usbmode_rpmsg_get(enum bk7258_usbmode_e *mode,
                             uint32_t timeout_ms);
int bk7258_usbmode_rpmsg_set(enum bk7258_usbmode_e mode,
                             enum bk7258_usbmode_e *actual,
                             uint32_t timeout_ms);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBMODE_RPMSG_H */
