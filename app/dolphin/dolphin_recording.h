/****************************************************************************
 * app/dolphin/dolphin_recording.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_DOLPHIN_DOLPHIN_RECORDING_H
#define __APP_DOLPHIN_DOLPHIN_RECORDING_H

#include <stdint.h>

enum dolphin_recording_state_e
{
  DOLPHIN_RECORDING_IDLE = 0,
  DOLPHIN_RECORDING_STARTING,
  DOLPHIN_RECORDING_ACTIVE,
  DOLPHIN_RECORDING_STOPPING,
  DOLPHIN_RECORDING_SAVED,
  DOLPHIN_RECORDING_FAILED,
  DOLPHIN_RECORDING_CLEANUP_FAILED
};

struct dolphin_recording_snapshot_s
{
  enum dolphin_recording_state_e state;
  int error;
  uint32_t bytes;
  uint32_t milliseconds;
  uint16_t peak;
  uint32_t clipped;
};

/* The caller retains fd unless this returns zero. On success this module owns
 * and closes it on every terminal path. fd must be a new, exclusive writable
 * seekable file positioned at offset zero. */
int dolphin_recording_begin(int fd);
int dolphin_recording_stop(void);
int dolphin_recording_snapshot(struct dolphin_recording_snapshot_s *snapshot);

#endif /* __APP_DOLPHIN_DOLPHIN_RECORDING_H */
