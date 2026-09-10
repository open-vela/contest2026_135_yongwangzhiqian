#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise mmcsd_transferready with a bounded card/status model."""

from pathlib import Path
import subprocess
import sys
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[3]
if len(sys.argv) > 1:
    source_path = Path(sys.argv[1])
    source = source_path.read_text()
else:
    source_path = ROOT.parent / "nuttx/drivers/mmcsd/mmcsd_sdio.c"
    patch_path = ROOT.parent / "nuttx/patches/mmcsd/0004-wait-ready-for-data.patch"
    if not patch_path.is_file():
        raise SystemExit(f"missing canonical patch: {patch_path}")
    source = source_path.read_text()
start = source.index("static int mmcsd_transferready(")
end = source.index("\n/****************************************************************************\n * Name: mmcsd_stoptransmission", start)
body = source[start:end]

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
typedef long clock_t;
typedef struct { int present; } sdio_dev_s;
struct mmcsd_state_s { sdio_dev_s *dev; unsigned type; bool wrbusy; };
#define FAR
#define OK 0
#define TICK_PER_SEC 5
#define MMCSD_CARDTYPE_UNKNOWN 0
#define MMCSD_R1_STATE_STBY (3u << 9)
#define MMCSD_R1_STATE_RCV  (6u << 9)
#define MMCSD_R1_STATE_PRG  (7u << 9)
#define MMCSD_R1_STATE_TRAN (4u << 9)
#define MMCSD_R1_READYFORDATA (1u << 8)
#define IS_STATE(r,s) (((r) & (0xfu << 9)) == (s))
#define IS_EMPTY(p) ((p)->type == MMCSD_CARDTYPE_UNKNOWN)
#define SDIO_PRESENT(d) ((d)->present)
static clock_t now;
static uint32_t statuses[32];
static unsigned status_count, status_index, sleeps;
static int status_error;
static uint32_t fallback_status;
static clock_t clock_systime_ticks(void) { return now++; }
static __attribute__((unused)) void test_sleep(unsigned usec)
  { (void)usec; sleeps++; now++; }
#define MMCSD_USLEEP(u) test_sleep(u)
#ifdef CONFIG_MMCSD_CHECK_READY_STATUS_WITHOUT_SLEEP
static int sched_yield(void) { sleeps++; now++; return 0; }
#endif
static void ferr(const char *fmt, ...) { (void)fmt; }
static __attribute__((unused)) int mmcsd_eventwait(struct mmcsd_state_s *p,
                                                   unsigned e)
  { (void)p; (void)e; return OK; }
static int mmcsd_get_r1(struct mmcsd_state_s *p, uint32_t *r1)
  { (void)p; if (status_error) return status_error;
    if (status_index < status_count) *r1 = statuses[status_index++];
    else *r1 = fallback_status;
    return OK; }
'''

suffix = r'''
static void setup(struct mmcsd_state_s *p, sdio_dev_s *dev)
{
  *dev = (sdio_dev_s){.present = 1};
  *p = (struct mmcsd_state_s){.dev = dev, .type = 1, .wrbusy = true};
  now = 0; status_index = 0; status_count = 0; status_error = 0; sleeps = 0;
  fallback_status = MMCSD_R1_STATE_PRG;
}
static void statuses_set(const uint32_t *v, unsigned n)
{
  for (unsigned i = 0; i < n; i++) statuses[i] = v[i];
  status_count = n;
}
int main(void)
{
  struct mmcsd_state_s p; sdio_dev_s dev;
  uint32_t ready_path[] = {MMCSD_R1_STATE_TRAN,
                           MMCSD_R1_STATE_PRG,
                           MMCSD_R1_STATE_TRAN | MMCSD_R1_READYFORDATA};
  setup(&p, &dev); statuses_set(ready_path, 3);
  assert(mmcsd_transferready(&p) == OK && !p.wrbusy);
  assert(status_index == 3 && sleeps == 2);

  setup(&p, &dev); statuses[0] = MMCSD_R1_STATE_PRG; status_count = 1;
  assert(mmcsd_transferready(&p) == -ETIMEDOUT && p.wrbusy);
  setup(&p, &dev); fallback_status = MMCSD_R1_STATE_TRAN;
  assert(mmcsd_transferready(&p) == -ETIMEDOUT && p.wrbusy);

  setup(&p, &dev); status_error = -EIO;
  assert(mmcsd_transferready(&p) == -EIO && p.wrbusy);
  setup(&p, &dev); statuses[0] = MMCSD_R1_STATE_STBY; status_count = 1;
  assert(mmcsd_transferready(&p) == -EINVAL && p.wrbusy);

  setup(&p, &dev); p.type = MMCSD_CARDTYPE_UNKNOWN;
  assert(mmcsd_transferready(&p) == -ENODEV && p.wrbusy);
  setup(&p, &dev); dev.present = 0;
  assert(mmcsd_transferready(&p) == -ENODEV && p.wrbusy);
  setup(&p, &dev); p.wrbusy = false;
  assert(mmcsd_transferready(&p) == OK && status_index == 0);
  puts("MMCSD_TRANSFERREADY_HOST_TEST_PASS");
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="mmcsd-transferready-") as directory:
    path = Path(directory)
    if len(sys.argv) == 1:
        copied = path / "nuttx/drivers/mmcsd/mmcsd_sdio.c"
        copied.parent.mkdir(parents=True)
        shutil.copy2(source_path, copied)
        subprocess.run(["patch", "-p1", "--batch", "--forward", "-d",
                        directory, "-i", str(patch_path)], check=True)
        source = copied.read_text()
    test_c = path / "test.c"
    test_c.write_text(prefix + body + suffix)
    for defines in ([], ["-DCONFIG_MMCSD_CHECK_READY_STATUS_WITHOUT_SLEEP"]):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        *defines, str(test_c), "-o", str(path / "test")],
                       check=True)
        subprocess.run([str(path / "test")], check=True)
