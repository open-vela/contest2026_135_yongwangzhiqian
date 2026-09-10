/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_BK7258_VOICE_WAKE_SESSION_H
#define __APP_BK7258_BK7258_VOICE_WAKE_SESSION_H

#include "bk7258_cloud_runtime.h"
#include "bk7258_voice_ptt.h"

#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct bkvoice_wake_session_s;

struct bkvoice_wake_session_config_s
{
  const char *model_path;
  const char *model_sha256_hex;
  size_t arena_bytes;
  size_t listener_stack_size;
  uint32_t listener_join_timeout_ms;
};

/* The configured path is fixed by the signed firmware.  The file itself is
 * untrusted until its complete bounded contents match model_sha256_hex.
 * open() never starts or acquires the microphone. */
int bkvoice_wake_session_open(
  struct bkvoice_wake_session_s **session,
  const struct bkvoice_wake_session_config_s *config,
  struct bkvoice_ptt_s *ptt, struct bkcloud_runtime_s *cloud, sem_t *wake);
int bkvoice_wake_session_step(struct bkvoice_wake_session_s *session,
                              bool allowed, uint64_t now_ms);
int bkvoice_wake_session_suspend(struct bkvoice_wake_session_s *session);
/* Retriable. Failure retains the complete object and borrowed contexts. */
int bkvoice_wake_session_close(struct bkvoice_wake_session_s **session);

#endif
