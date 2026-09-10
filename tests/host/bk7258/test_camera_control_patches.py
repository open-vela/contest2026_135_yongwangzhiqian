#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile patched camera boundaries and inject transport failures; no hardware."""

from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[3]
SDK = REPO.parent / "vendor/beken/bk_avdk_smp"
NUTTX = REPO.parent / "nuttx"
PROFILE = REPO / "chips/bk7258/bk_idk/sdk-profiles/v3.1.1.9"


def function(source, name):
    start = source.rfind("\n", 0, source.index(name)) + 1
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class CameraPatches(unittest.TestCase):
    def test_sdk_transport_errors_and_invalid_arguments(self):
        paths = ["ap/components/bk_dvp/src/bk_dvp.c",
                 "ap/components/bk_peripheral/src/dvp/dvp_gc2145.c"]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for path in paths:
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((SDK / path).read_bytes())
            for patch in ("ap-gc2145-timing.patch", "ap-dvp-register-errors.patch"):
                subprocess.run(["git", "apply", str(PROFILE / patch)],
                               cwd=root, check=True, capture_output=True)
            dvp = (root / paths[0]).read_text()
            sensor = (root / paths[1]).read_text()
            source = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef int bk_err_t;
typedef void *camera_handle_t;
#define BK_OK 0
#define BK_FAIL (-1)
#define GC2145_WRITE_ADDRESS 0x78
typedef struct { uint32_t reg, val; } dvp_sensor_reg_val_t;
struct sensor_s {
  int (*write_register)(uint32_t, uint32_t);
  int (*read_register)(uint32_t, uint32_t *);
};
typedef struct { struct sensor_s *sensor; } dvp_driver_handle_t;
static int result;
static int calls;
static int dvp_camera_i2c_write_uint8(uint8_t addr, uint8_t reg, uint8_t val)
{
  assert(addr == 0x3c && reg == 0xb6 && val == 1);
  calls++;
  return result;
}
static int read_register(uint32_t reg, uint32_t *value)
{
  assert(reg == 0xb6);
  *value = 17;
  calls++;
  return result;
}
'''
            source += function(sensor, "int gc2145_write_register(") + "\n"
            source += function(dvp, "bk_err_t bk_dvp_sensor_write_register(") + "\n"
            source += function(dvp, "bk_err_t bk_dvp_sensor_read_register(") + "\n"
            source += r'''
int main(void)
{
  struct sensor_s sensor = {gc2145_write_register, read_register};
  dvp_driver_handle_t handle = {&sensor};
  dvp_sensor_reg_val_t reg = {0xb6, 1};
  result = -7;
  assert(bk_dvp_sensor_write_register(&handle, &reg) == -7);
  assert(bk_dvp_sensor_read_register(&handle, &reg) == -7);
  assert(reg.val == 1 && calls == 2);
  result = 0;
  assert(bk_dvp_sensor_write_register(&handle, &reg) == 0);
  assert(bk_dvp_sensor_read_register(&handle, &reg) == 0);
  assert(reg.val == 17 && calls == 4);
  assert(bk_dvp_sensor_read_register(NULL, &reg) == BK_FAIL);
  assert(bk_dvp_sensor_write_register(&handle, NULL) == BK_FAIL);
  sensor.read_register = NULL;
  sensor.write_register = NULL;
  assert(bk_dvp_sensor_read_register(&handle, &reg) == BK_FAIL);
  assert(bk_dvp_sensor_write_register(&handle, &reg) == BK_FAIL);
  handle.sensor = NULL;
  assert(bk_dvp_sensor_read_register(&handle, &reg) == BK_FAIL);
  assert(calls == 4);
  return 0;
}
'''
            cfile = root / "check.c"
            binary = root / "check"
            cfile.write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(cfile), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_gain_ids_do_not_alias_existing_controls(self):
        paths = ["include/sys/video_controls.h", "include/nuttx/video/imgsensor.h",
                 "drivers/video/v4l2_cap.c"]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for path in paths:
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((NUTTX / path).read_bytes())
            subprocess.run(["git", "apply", str(REPO / "nuttx/patches/video/"
                            "0001-disambiguate-gain-controls.patch")],
                           cwd=root, check=True, capture_output=True)
            source = '#include "include/sys/video_controls.h"\n'
            source += '_Static_assert(V4L2_CID_HFLIP == USER_CID(10), "ABI");\n'
            source += '_Static_assert(V4L2_CID_VFLIP == USER_CID(11), "ABI");\n'
            source += 'int unique_ids(int id) { switch (id) {\n'
            for name in ("AUTOGAIN", "GAIN", "HFLIP", "VFLIP", "HFLIP_STILL",
                         "VFLIP_STILL", "EXPOSURE", "ROTATE"):
                source += f'case V4L2_CID_{name}: return 0;\n'
            source += 'default: return -1; }}\n'
            cfile = root / "check.c"
            cfile.write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-c",
                            str(cfile), "-o", str(root / "check.o")], check=True)


if __name__ == "__main__":
    unittest.main()
