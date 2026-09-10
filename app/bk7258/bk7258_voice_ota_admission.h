/****************************************************************************
 * app/bk7258/bk7258_voice_ota_admission.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_OTA_ADMISSION_H
#define __APP_BK7258_BK7258_VOICE_OTA_ADMISSION_H

#include <stdbool.h>
#include <stdint.h>

enum bkvoice_ota_target_admission_e
{
  BKVOICE_OTA_TARGET_IDLE = 0,
  BKVOICE_OTA_TARGET_READY,
  BKVOICE_OTA_TARGET_COMMITTING,
  BKVOICE_OTA_TARGET_APPROVED,
  BKVOICE_OTA_TARGET_REJECTING,
  BKVOICE_OTA_TARGET_REJECTED,
};

static inline uint8_t bkvoice_ota_target_admission_load(
  const volatile uint8_t *state)
{
  return __atomic_load_n(state, __ATOMIC_ACQUIRE);
}

/* Publishing READY makes the worker-written manifest target visible to the
 * owner.  Claiming READY makes the owner the only persistent-store writer.
 */

static inline void bkvoice_ota_target_admission_publish(
  volatile uint8_t *state)
{
  __atomic_store_n(state, BKVOICE_OTA_TARGET_READY, __ATOMIC_RELEASE);
}

static inline bool bkvoice_ota_target_admission_claim(
  volatile uint8_t *state)
{
  uint8_t expected = BKVOICE_OTA_TARGET_READY;

  return __atomic_compare_exchange_n(state, &expected,
           BKVOICE_OTA_TARGET_COMMITTING, false,
           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static inline bool bkvoice_ota_target_admission_claim_rejection(
  volatile uint8_t *state)
{
  uint8_t expected = BKVOICE_OTA_TARGET_READY;

  return __atomic_compare_exchange_n(state, &expected,
           BKVOICE_OTA_TARGET_REJECTING, false,
           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static inline void bkvoice_ota_target_admission_finish(
  volatile uint8_t *state, bool approved)
{
  __atomic_store_n(state, approved ? BKVOICE_OTA_TARGET_APPROVED :
                                     BKVOICE_OTA_TARGET_REJECTED,
                   __ATOMIC_RELEASE);
}

#endif /* __APP_BK7258_BK7258_VOICE_OTA_ADMISSION_H */
