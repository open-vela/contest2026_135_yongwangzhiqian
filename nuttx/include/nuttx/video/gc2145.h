/****************************************************************************
 * include/nuttx/video/gc2145.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __INCLUDE_NUTTX_VIDEO_GC2145_H
#define __INCLUDE_NUTTX_VIDEO_GC2145_H

#include <sys/video_controls.h>
#include <nuttx/video/imgsensor.h>

/* Register-control part of the GC2145 sensor lower half. The transport owns
 * power and stream initialization. lock() must exclude transport teardown
 * and other sensor-register transactions until unlock(). read/write return
 * zero or a negative errno. No SoC or SDK types cross this boundary.
 */

struct gc2145_control_s
{
  FAR void *arg;
  int (*lock)(FAR void *arg);
  void (*unlock)(FAR void *arg);
  int (*read)(FAR void *arg, uint8_t reg, FAR uint8_t *value);
  int (*write)(FAR void *arg, uint8_t reg, uint8_t value);
};

int gc2145_get_supported_value(uint32_t id,
                               FAR imgsensor_supported_value_t *value);
int gc2145_get_value(FAR const struct gc2145_control_s *control,
                     uint32_t id, uint32_t size,
                     FAR imgsensor_value_t *value);
int gc2145_set_value(FAR const struct gc2145_control_s *control,
                     uint32_t id, uint32_t size, imgsensor_value_t value);
#endif
