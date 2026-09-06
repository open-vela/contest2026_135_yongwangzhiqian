#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the complete generic GC2145 driver with a failing register bus."""
from pathlib import Path
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[3]
MOCK_IMGSENSOR = r'''
#ifndef MOCK_IMGSENSOR_H
#define MOCK_IMGSENSOR_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define FAR
#define IMGSENSOR_EXPOSURE_AUTO 0
#define IMGSENSOR_EXPOSURE_MANUAL 1
#define IMGSENSOR_CTRL_TYPE_INTEGER 1
#define IMGSENSOR_CTRL_TYPE_BOOLEAN 2
#define IMGSENSOR_CTRL_TYPE_INTEGER_MENU 9
#define IMGSENSOR_ID_EXPOSURE 0x10
#define IMGSENSOR_ID_HFLIP_VIDEO 0x11
#define IMGSENSOR_ID_VFLIP_VIDEO 0x12
#define IMGSENSOR_ID_EXPOSURE_AUTO 0x13
#define IMGSENSOR_ID_GAIN 0x14
typedef struct { int64_t minimum; int64_t maximum; uint64_t step; int64_t default_value; } imgsensor_capability_range_t;
typedef struct { int8_t nr_values; const int32_t *values; int32_t default_value; } imgsensor_capability_discrete_t;
typedef struct { int type; union { imgsensor_capability_range_t range; imgsensor_capability_discrete_t discrete; } u; } imgsensor_supported_value_t;
typedef union { int32_t value32; int64_t value64; uint8_t *p_u8; } imgsensor_value_t;
#endif
'''

HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <nuttx/video/gc2145.h>

struct fake_s { uint8_t r[256]; int lock_ret; int fail_read_reg; int fail_write_reg; int read_ops; int write_ops; int locks; int unlocks; };
static int lockit(void *arg) { struct fake_s *f = arg; f->locks++; return f->lock_ret; }
static void unlockit(void *arg) { ((struct fake_s *)arg)->unlocks++; }
static int readit(void *arg, uint8_t reg, uint8_t *v) { struct fake_s *f = arg; f->read_ops++; if (f->fail_read_reg == reg) return -EIO; *v = f->r[reg]; return 0; }
static int writeit(void *arg, uint8_t reg, uint8_t v) { struct fake_s *f = arg; f->write_ops++; if (f->fail_write_reg == reg) return -EIO; f->r[reg] = v; return 0; }
#define CTL(f) (&(struct gc2145_control_s){(f), lockit, unlockit, readit, writeit})
static void reset(struct fake_s *f) { memset(f, 0, sizeof(*f)); f->fail_read_reg = -1; f->fail_write_reg = -1; }

static void test_ae_manual_preserves_bits_and_page(void) {
  struct fake_s f; imgsensor_value_t v = {.value32 = IMGSENSOR_EXPOSURE_MANUAL}; reset(&f); f.r[0xfe] = 2; f.r[0xb6] = 0xa5;
  assert(gc2145_set_value(CTL(&f), IMGSENSOR_ID_EXPOSURE_AUTO, 0, v) == 0);
  assert(f.r[0xb6] == 0xa4 && f.r[0xfe] == 2 && f.locks == 1 && f.unlocks == 1);
}
static void test_exposure_rejected_while_ae(void) {
  struct fake_s f; imgsensor_value_t v = {.value32 = 300}; reset(&f); f.r[0xb6] = 1;
  assert(gc2145_set_value(CTL(&f), IMGSENSOR_ID_EXPOSURE, 0, v) == -EBUSY);
  assert(f.write_ops == 0 && f.locks == f.unlocks);
}
static void test_invalid_and_unsupported_do_not_touch_bus(void) {
  struct fake_s f; imgsensor_value_t v = {.value32 = 0}; reset(&f);
  assert(gc2145_set_value(CTL(&f), IMGSENSOR_ID_EXPOSURE, 0, v) == -ERANGE);
  v.value32 = 1;
  assert(gc2145_set_value(CTL(&f), 0xdead, 0, v) == -ENOTTY);
  assert(f.locks == 0 && f.unlocks == 0 && f.read_ops == 0 && f.write_ops == 0);
}
static void test_gain_and_flips_are_distinct(void) {
  struct fake_s f; imgsensor_value_t v; reset(&f); f.r[0x17] = 0xa0;
  v.value32 = 32; assert(gc2145_set_value(CTL(&f), IMGSENSOR_ID_GAIN, 0, v) == 0); assert(f.r[0xb0] == 32);
  v.value32 = 1; assert(gc2145_set_value(CTL(&f), IMGSENSOR_ID_HFLIP_VIDEO, 0, v) == 0); assert(f.r[0x17] == 0xa1);
  assert(gc2145_set_value(CTL(&f), IMGSENSOR_ID_VFLIP_VIDEO, 0, v) == 0); assert(f.r[0x17] == 0xa3);
}
static void test_read_failure_preserves_output_and_lock_failure(void) {
  struct fake_s f; imgsensor_value_t out = {.value32 = 777}; reset(&f); f.r[0xfe] = 3; f.fail_read_reg = 0xb0;
  assert(gc2145_get_value(CTL(&f), IMGSENSOR_ID_GAIN, 0, &out) == -EIO);
  assert(out.value32 == 777 && f.r[0xfe] == 3 && f.locks == f.unlocks);
  reset(&f); f.lock_ret = -EAGAIN;
  assert(gc2145_get_value(CTL(&f), IMGSENSOR_ID_GAIN, 0, &out) == -EAGAIN);
  assert(f.locks == 1 && f.unlocks == 0 && f.read_ops == 0 && f.write_ops == 0);
}
static void test_second_exposure_byte_failure_propagates_and_restores_page(void) {
  struct fake_s f; imgsensor_value_t v = {.value32 = 0x123}; reset(&f); f.r[0xfe] = 2; f.r[0xb6] = 0; f.fail_write_reg = 0x04;
  assert(gc2145_set_value(CTL(&f), IMGSENSOR_ID_EXPOSURE, 0, v) == -EIO);
  assert(f.r[0x03] == 1 && f.r[0xfe] == 2 && f.locks == f.unlocks);
}
int main(void) { test_ae_manual_preserves_bits_and_page(); test_exposure_rejected_while_ae(); test_invalid_and_unsupported_do_not_touch_bus(); test_gain_and_flips_are_distinct(); test_read_failure_preserves_output_and_lock_failure(); test_second_exposure_byte_failure_propagates_and_restores_page(); return 0; }
'''

def main():
    with tempfile.TemporaryDirectory(prefix="gc2145-controls-") as temporary:
        root = Path(temporary)
        (root / "nuttx/video").mkdir(parents=True)
        (root / "sys").mkdir()
        (root / "sys/video_controls.h").write_text("")
        (root / "nuttx/config.h").write_text("#define CONFIG_VIDEO_GC2145_CONTROLS 1\n")
        (root / "nuttx/video/imgsensor.h").write_text(MOCK_IMGSENSOR)
        (root / "nuttx/video/gc2145.h").write_bytes(
            (REPO / "nuttx/include/nuttx/video/gc2145.h").read_bytes())
        (root / "harness.c").write_text(HARNESS)
        binary = root / "gc2145-controls"
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I", str(root),
                        str(REPO / "nuttx/drivers/video/gc2145.c"),
                        str(root / "harness.c"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print("GC2145_CONTROLS_PASS")

if __name__ == "__main__":
    main()
