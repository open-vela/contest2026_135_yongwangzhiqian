/****************************************************************************
 * app/bk7258/bk7258_voice_ota_cancel.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_OTA_CANCEL_H
#define __APP_BK7258_BK7258_VOICE_OTA_CANCEL_H

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

enum bkvoice_ota_job_state_e
{
  BKVOICE_OTA_JOB_EMPTY = 0,
  BKVOICE_OTA_JOB_QUEUED,
  BKVOICE_OTA_JOB_APPLYING,
  BKVOICE_OTA_JOB_DONE,
  BKVOICE_OTA_JOB_STAGED,
  BKVOICE_OTA_JOB_REBOOTING,
  BKVOICE_OTA_JOB_TRIAL,
};

enum bkvoice_ota_cancel_disposition_e
{
  BKVOICE_OTA_CANCEL_CLEAR = 0,
  BKVOICE_OTA_CANCEL_WAIT,
  BKVOICE_OTA_CANCEL_PRESERVE,
};

static inline bool bkvoice_ota_intent_is_commit_sensitive(
  bool intent_present, bool intent_downloading,
  uint32_t target_security_counter, bool target_commit_started)
{
  return target_commit_started ||
         (intent_present &&
          (!intent_downloading ||
           target_security_counter != 0u));
}

static inline bool bkvoice_ota_cancel_requires_join(uint8_t state,
                                                    bool worker_joinable)
{
  return worker_joinable &&
         (state == BKVOICE_OTA_JOB_APPLYING ||
          state == BKVOICE_OTA_JOB_DONE);
}

static inline enum bkvoice_ota_cancel_disposition_e
bkvoice_ota_cancel_disposition(uint8_t state, int manager_result,
                               int worker_result, bool target_valid,
                               bool committed_intent)
{
  /* Boot resets the in-memory job, not the already staged update. */

  if (committed_intent)
    {
      return BKVOICE_OTA_CANCEL_PRESERVE;
    }

  switch (state)
    {
      case BKVOICE_OTA_JOB_EMPTY:
      case BKVOICE_OTA_JOB_QUEUED:
        return BKVOICE_OTA_CANCEL_CLEAR;

      case BKVOICE_OTA_JOB_APPLYING:
        return manager_result == 0 || manager_result == -ENOENT ?
               BKVOICE_OTA_CANCEL_WAIT : BKVOICE_OTA_CANCEL_PRESERVE;

      case BKVOICE_OTA_JOB_DONE:
        return worker_result == -EALREADY ||
               (worker_result >= 0 && target_valid) ?
               BKVOICE_OTA_CANCEL_PRESERVE : BKVOICE_OTA_CANCEL_CLEAR;

      case BKVOICE_OTA_JOB_STAGED:
      case BKVOICE_OTA_JOB_REBOOTING:
      case BKVOICE_OTA_JOB_TRIAL:
      default:
        return BKVOICE_OTA_CANCEL_PRESERVE;
    }
}

#endif /* __APP_BK7258_BK7258_VOICE_OTA_CANCEL_H */
