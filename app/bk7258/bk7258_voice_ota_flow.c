/****************************************************************************
 * app/bk7258/bk7258_voice_ota_flow.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_voice_ota_flow.h"

#include <errno.h>
#include <stdbool.h>

static bool bkvoice_ota_pair_matches(
  const struct bk7258_ota_pair_snapshot_s *pair,
  const struct bk7258_mcuboot_version_s *version,
  uint32_t security_counter)
{
  return pair->security_counter_present && security_counter != 0u &&
         pair->security_counter == security_counter &&
         bk7258_mcuboot_version_equal(&pair->version, version);
}

int bkvoice_ota_flow_decide(
  const struct bkvoice_ota_intent_s *intent,
  uint32_t current_boot_generation,
  const struct bk7258_ota_pair_snapshot_s *pair,
  enum bkvoice_ota_flow_action_e *action)
{
  bool source;
  bool target;
  bool same_boot;

  if (intent == NULL || pair == NULL || action == NULL ||
      current_boot_generation == 0u || current_boot_generation == UINT32_MAX ||
      (pair->state != BK7258_OTA_PAIR_PENDING &&
       pair->state != BK7258_OTA_PAIR_CONFIRMED))
    {
      return -EINVAL;
    }

  source = bkvoice_ota_pair_matches(pair, &intent->source_version,
                                    intent->source_security_counter);
  target = bkvoice_ota_pair_matches(pair, &intent->target_version,
                                    intent->target_security_counter);
  same_boot = current_boot_generation == intent->source_boot_generation;

  if (intent->state == BKVOICE_OTA_DOWNLOADING)
    {
      if (source && pair->state == BK7258_OTA_PAIR_CONFIRMED)
        {
          *action = BKVOICE_OTA_FLOW_RESTAGE;
          return 0;
        }

      if (!same_boot && target)
        {
          *action = pair->state == BK7258_OTA_PAIR_PENDING ?
                    BKVOICE_OTA_FLOW_TRIAL :
                    BKVOICE_OTA_FLOW_CONFIRMED;
          return 0;
        }

      return -ESTALE;
    }

  if (intent->state == BKVOICE_OTA_STAGED ||
      intent->state == BKVOICE_OTA_REBOOTING)
    {
      if (same_boot)
        {
          if (source && pair->state == BK7258_OTA_PAIR_CONFIRMED)
            {
              *action = BKVOICE_OTA_FLOW_REBOOT;
              return 0;
            }

          return -ESTALE;
        }
    }
  else if (intent->state != BKVOICE_OTA_TRIAL)
    {
      return -EINVAL;
    }

  if (same_boot)
    {
      return -ESTALE;
    }

  if (target)
    {
      *action = pair->state == BK7258_OTA_PAIR_PENDING ?
                BKVOICE_OTA_FLOW_TRIAL : BKVOICE_OTA_FLOW_CONFIRMED;
      return 0;
    }

  if (source && pair->state == BK7258_OTA_PAIR_CONFIRMED)
    {
      *action = BKVOICE_OTA_FLOW_ROLLED_BACK;
      return 0;
    }

  return -ESTALE;
}
