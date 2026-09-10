/****************************************************************************
 * chips/bk7258/include/bk7258_ble_scan.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BLE_SCAN_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_BLE_SCAN_H

#include <stdint.h>

#define BK7258_BLE_SCAN_MAX_RESULTS 16u
#define BK7258_BLE_SCAN_MAX_PAYLOAD 31u

enum bk7258_ble_scan_state_e
{
  BK7258_BLE_SCAN_IDLE = 0, BK7258_BLE_SCAN_STARTING,
  BK7258_BLE_SCAN_ACTIVE, BK7258_BLE_SCAN_STOPPING,
  BK7258_BLE_SCAN_FAULTED
};

struct bk7258_ble_scan_result_s
{
  uint8_t address[6]; uint8_t address_type; int8_t rssi;
  uint8_t advertising_type; uint8_t payload_length;
  uint8_t payload[BK7258_BLE_SCAN_MAX_PAYLOAD];
};

struct bk7258_ble_scan_snapshot_s
{
  uint32_t generation; uint32_t dropped; uint32_t active; uint32_t count;
  int32_t last_error; uint32_t state;
  struct bk7258_ble_scan_result_s results[BK7258_BLE_SCAN_MAX_RESULTS];
};

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_ble_scan_start(void);
int bk7258_ble_scan_poll(struct bk7258_ble_scan_snapshot_s *snapshot);
int bk7258_ble_scan_stop(void);
#endif
#endif
