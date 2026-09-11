/* SPDX-License-Identifier: Apache-2.0 */

#ifndef __APPS_DOLPHIN_DOLPHIN_COPY_H
#define __APPS_DOLPHIN_DOLPHIN_COPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum dolphin_copy_state_e
{
  DOLPHIN_COPY_IDLE = 0,
  DOLPHIN_COPY_RUNNING,
  DOLPHIN_COPY_SUCCEEDED,
  DOLPHIN_COPY_FAILED,
  DOLPHIN_COPY_CANCELED
};

struct dolphin_copy_snapshot_s
{
  enum dolphin_copy_state_e state;
  uint64_t copied;
  uint64_t total;
  int error;
  bool partial;
};

int dolphin_copy_start(const char *source_path, const char *destination_path);
int dolphin_copy_cancel(void);
int dolphin_copy_snapshot(struct dolphin_copy_snapshot_s *snapshot);

#endif /* __APPS_DOLPHIN_DOLPHIN_COPY_H */
