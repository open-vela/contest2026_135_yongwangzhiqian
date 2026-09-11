/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_BK7258_USBHOST_H
#define TEST_BK7258_USBHOST_H

#include <stdbool.h>
#include <stdint.h>

#define BK7258_USBHOST_DEVICE_DESC_SIZE 18u
#define BK7258_USBHOST_CONFIG_DESC_MAX 256u

enum bk7258_usbhost_enumeration_state_e
{
  BK7258_USBHOST_ENUMERATION_IDLE = 0,
  BK7258_USBHOST_ENUMERATION_RUNNING,
  BK7258_USBHOST_ENUMERATION_COMPLETE,
  BK7258_USBHOST_ENUMERATION_FAILED
};

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

int bk7258_usbhost_snapshot(struct bk7258_usbhost_snapshot_s *snapshot);

#endif
