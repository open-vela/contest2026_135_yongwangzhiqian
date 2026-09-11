/****************************************************************************
 * chips/bk7258/include/bk7258_usbhost.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBHOST_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBHOST_H

#include <nuttx/compiler.h>
#include <nuttx/usb/usbhost.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Return the AP USB host connection object.  The caller owns the NuttX
 * waiter/enumeration policy and registers the enabled NuttX USB host classes.
 * The BK7258 SDK CherryUSB stack is deliberately not used as an upper layer;
 * the implementation only adapts its immutable HCD/pipe/URB ABI.  Board
 * linking wraps usbh_initialize(), usbh_deinitialize(), and the root-hub
 * event queue so bk_usb_open()/bk_usb_close() perform the official SDK
 * power/PHY sequence while CherryUSB upper-layer entry points are redirected
 * to this adapter's HCD-only lifecycle and ISR-deferred event path.
 *
 * This controller has one physical root port.  No board VBUS or external hub
 * policy is implied by this API; that policy belongs to board integration.
 */

#define BK7258_USBHOST_DEVICE_DESC_SIZE 18u
#define BK7258_USBHOST_CONFIG_DESC_MAX  256u

enum bk7258_usbhost_enumeration_state_e
{
  BK7258_USBHOST_ENUMERATION_IDLE = 0,
  BK7258_USBHOST_ENUMERATION_RUNNING,
  BK7258_USBHOST_ENUMERATION_COMPLETE,
  BK7258_USBHOST_ENUMERATION_FAILED
};

/* This is a read-only copy of descriptors already requested by NuttX during
 * enumeration.  No vendor objects or live transfer buffers are exposed. */
struct bk7258_usbhost_snapshot_s
{
  bool initialized;
  bool connected;
  uint8_t speed;
  uint32_t connection_generation;
  enum bk7258_usbhost_enumeration_state_e enumeration_state;
  int32_t enumeration_result;
  bool device_descriptor_valid;
  uint8_t device_descriptor[BK7258_USBHOST_DEVICE_DESC_SIZE];
  bool configuration_descriptor_valid;
  uint16_t configuration_length;
  bool configuration_truncated;
  uint8_t configuration_descriptor[BK7258_USBHOST_CONFIG_DESC_MAX];
};

FAR struct usbhost_connection_s *bk7258_usbhost_initialize(void);
int bk7258_usbhost_uninitialize(void);
/* Returns -EAGAIN while initialization or teardown owns the lifecycle lock. */
int bk7258_usbhost_snapshot(FAR struct bk7258_usbhost_snapshot_s *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_USBHOST_H */
