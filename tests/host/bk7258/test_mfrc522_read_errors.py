#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the patched MFRC522 read body with deterministic card outcomes."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
NUTTX = REPOSITORY.parent / "nuttx"
PATCH = (REPOSITORY / "nuttx/patches/contactless/"
         "0001-mfrc522-propagate-card-selection-errors.patch")
BASELINE = "76354c637858ecb0aa4601629327acb6f44a26bb"


def extract_function(source: str, marker: str) -> str:
    start = source.rindex(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS_PREFIX = r"""
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define FAR
#define OK 0
#define DEBUGASSERT(x) ((void)0)
#define ctlserr(...) ((void)0)
#define PICC_TYPE_NOT_COMPLETE 0x04

struct mfrc522_dev_s { int unused; };
struct picc_uid_s
{
  uint8_t size;
  uint8_t uid_data[10];
  uint8_t sak;
};
struct inode { void *i_private; };
struct file { struct inode *f_inode; };

static bool detected;
static int select_result;
static struct picc_uid_s selected_uid;

static bool mfrc522_picc_detect(struct mfrc522_dev_s *dev)
{
  (void)dev;
  return detected;
}

static int mfrc522_picc_select(struct mfrc522_dev_s *dev,
                               struct picc_uid_s *uid, uint8_t validbits)
{
  (void)dev;
  assert(validbits == 0);
  if (select_result < 0)
    {
      return select_result;
    }

  *uid = selected_uid;
  return 0;
}
"""


HARNESS_SUFFIX = r"""
int main(void)
{
  struct mfrc522_dev_s dev;
  struct inode inode = { .i_private = &dev };
  struct file file = { .f_inode = &inode };
  char buffer[32];

  detected = false;
  assert(mfrc522_read(&file, buffer, sizeof(buffer)) == -EAGAIN);

  detected = true;
  select_result = -ETIMEDOUT;
  memset(buffer, 0xa5, sizeof(buffer));
  assert(mfrc522_read(&file, buffer, sizeof(buffer)) == -ETIMEDOUT);
  assert((unsigned char)buffer[0] == 0xa5);

  select_result = -EIO;
  assert(mfrc522_read(&file, buffer, sizeof(buffer)) == -EIO);

  memset(&selected_uid, 0, sizeof(selected_uid));
  selected_uid.uid_data[0] = 0x12;
  selected_uid.uid_data[1] = 0x34;
  selected_uid.uid_data[2] = 0x56;
  selected_uid.uid_data[3] = 0x78;
  selected_uid.sak = 0;
  select_result = 0;
  memset(buffer, 0, sizeof(buffer));
  assert(mfrc522_read(&file, buffer, sizeof(buffer)) == sizeof(buffer));
  assert(strcmp(buffer, "0x12345678") == 0);

  assert(mfrc522_read(&file, NULL, sizeof(buffer)) == 0);
  assert(mfrc522_read(&file, buffer, 0) == 0);
  return 0;
}
"""


class Mfrc522ReadErrorsTest(unittest.TestCase):
    def test_patched_read_propagates_selection_failures(self) -> None:
        baseline_source = subprocess.check_output(
            ["git", "-C", str(NUTTX), "show",
             f"{BASELINE}:drivers/contactless/mfrc522.c"],
            text=True,
        )
        subprocess.run(
            ["git", "-C", str(NUTTX), "apply", "--check", str(PATCH)],
            check=True,
        )

        with tempfile.TemporaryDirectory(prefix="mfrc522-read-") as temporary:
            root = Path(temporary)
            source = root / "drivers/contactless/mfrc522.c"
            source.parent.mkdir(parents=True)
            source.write_text(baseline_source)
            subprocess.run(
                ["git", "init", "--quiet"], cwd=root, check=True,
            )
            subprocess.run(
                ["git", "apply", str(PATCH)], cwd=root, check=True,
            )

            body = extract_function(
                source.read_text(), "static ssize_t mfrc522_read("
            )
            harness = root / "mfrc522_read_harness.c"
            binary = root / "mfrc522_read_harness"
            harness.write_text(HARNESS_PREFIX + "\n" + body + "\n" +
                               HARNESS_SUFFIX)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 str(harness), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
