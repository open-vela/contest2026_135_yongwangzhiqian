#!/usr/bin/env python3
"""Apply the FF lifecycle patch and exercise its upper-half ownership ABI."""

from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
UPSTREAM = REPOSITORY.parent / "nuttx"
PATCH = REPOSITORY / "nuttx/patches/input/0001-ff-close-and-write-lifecycle.patch"


HEADERS = {
    "nuttx/config.h": """#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define FAR
#define CODE
#define OK 0
#define DEBUGASSERT(x) ((void)0)
""",
    "nuttx/fs/fs.h": """#pragma once
#include <sys/types.h>
#include <string.h>
struct inode { void *i_private; };
struct file { struct inode *f_inode; };
struct file_operations {
  int (*open)(struct file *); int (*close)(struct file *); void *read;
  ssize_t (*write)(struct file *, const char *, size_t); void *seek;
  int (*ioctl)(struct file *, int, unsigned long); void *mmap;
  void *truncate; void *poll;
};
static inline int register_driver(const char *p,
  const struct file_operations *o, int m, void *x) { return 0; }
static inline int unregister_driver(const char *p) { return 0; }
""",
    "nuttx/kmalloc.h": """#pragma once
#include <stdlib.h>
static inline void *kmm_zalloc(size_t n) { return calloc(1, n); }
static inline void kmm_free(void *p) { free(p); }
""",
    "nuttx/mutex.h": """#pragma once
typedef int mutex_t;
#define NXMUTEX_INITIALIZER 0
static inline int nxmutex_init(mutex_t *m) { return 0; }
static inline int nxmutex_destroy(mutex_t *m) { return 0; }
static inline int nxmutex_lock(mutex_t *m) { return 0; }
static inline int nxmutex_unlock(mutex_t *m) { return 0; }
""",
    "nuttx/irq.h": "#pragma once\n",
    "nuttx/input/ff.h": """#pragma once
#include <stdint.h>
#define FF_RUMBLE 0
#define FF_PERIODIC 1
#define FF_EFFECT_MAX 7
#define FF_SINE 10
#define FF_WAVEFORM_MIN 8
#define FF_WAVEFORM_MAX 13
#define FF_GAIN 14
#define FF_AUTOCENTER 15
#define FF_MAX_EFFECTS 16
#define FF_CNT 128
#define BITS_TO_LONGS(n) (((n) + sizeof(unsigned long) * 8 - 1) / \\
                          (sizeof(unsigned long) * 8))
#define test_bit(n, a) (((a)[(n) / (sizeof(unsigned long) * 8)] >> \\
                        ((n) % (sizeof(unsigned long) * 8))) & 1ul)
#define EVIOCGBIT 1
#define EVIOCSFF 2
#define EVIOCRMFF 3
#define EVIOCGEFFECTS 4
#define EVIOCGDURATION 5
#define EVIOCSETCALIBDATA 6
#define EVIOCCALIBRATE 7
struct ff_periodic_effect {
  uint16_t waveform; uint16_t period; int16_t magnitude; int16_t offset;
  uint16_t phase;
  struct { uint16_t attack_length; uint16_t attack_level;
           uint16_t fade_length; uint16_t fade_level; } envelope;
};
struct ff_rumble_effect { uint16_t strong_magnitude; uint16_t weak_magnitude; };
struct ff_effect {
  int id; uint16_t type;
  union { struct ff_periodic_effect periodic; struct ff_rumble_effect rumble; } u;
};
struct ff_event_s { uint32_t code; int value; };
struct ff_lowerhalf_s {
  int (*upload)(struct ff_lowerhalf_s *, struct ff_effect *, struct ff_effect *);
  int (*erase)(struct ff_lowerhalf_s *, int);
  int (*playback)(struct ff_lowerhalf_s *, int, int);
  void (*set_gain)(struct ff_lowerhalf_s *, uint16_t);
  void (*set_autocenter)(struct ff_lowerhalf_s *, uint16_t);
  void (*destroy)(struct ff_lowerhalf_s *);
  int (*get_duration)(struct ff_lowerhalf_s *, struct ff_effect *);
  int (*set_calibvalue)(struct ff_lowerhalf_s *, unsigned long);
  int (*calibrate)(struct ff_lowerhalf_s *, unsigned long);
  int (*control)(struct ff_lowerhalf_s *, int, unsigned long);
  unsigned long ffbit[BITS_TO_LONGS(FF_CNT)]; void *priv;
};
int ff_event(struct ff_lowerhalf_s *, uint32_t, int);
""",
}


HARNESS = r"""
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "drivers/input/ff_upper.c"

static int played[2];
static int erased[2];
static int fail_play;
static int fail_erase;
static int destroyed;

static int playback(struct ff_lowerhalf_s *lower, int id, int value)
{
  played[id]++;
  return fail_play ? -EIO : 0;
}

static int erase(struct ff_lowerhalf_s *lower, int id)
{
  erased[id]++;
  return fail_erase ? -EIO : 0;
}

static void destroy(struct ff_lowerhalf_s *lower)
{
  destroyed++;
  free(lower);
}

int main(void)
{
  struct ff_lowerhalf_s *lower = calloc(1, sizeof(*lower));
  struct ff_upperhalf_s *upper = calloc(1, sizeof(*upper) +
                                        2 * sizeof(*upper->effects));
  struct ff_effect_s *effects = (struct ff_effect_s *)(upper + 1);
  struct inode inode = { .i_private = upper };
  struct file owner = { .f_inode = &inode };
  struct file other = { .f_inode = &inode };
  struct ff_event_s event = { .code = 1, .value = 1 };

  assert(lower != NULL && upper != NULL);
  lower->playback = playback;
  lower->erase = erase;
  lower->destroy = destroy;
  upper->lower = lower;
  upper->max_effects = 2;
  upper->effects = effects;
  lower->priv = upper;
  effects[0].owner = &owner;
  effects[1].owner = &other;

  assert(ff_close(&owner) == 0);
  assert(played[0] == 1 && erased[0] == 1 && effects[0].owner == NULL);
  assert(played[1] == 0 && erased[1] == 0 && effects[1].owner == &other);
  assert(ff_write(&other, (const char *)&event, sizeof(event) - 1) == -EINVAL);
  assert(ff_write(&owner, (const char *)&event, sizeof(event)) == -EACCES);
  fail_play = 1;
  assert(ff_write(&other, (const char *)&event, sizeof(event)) == -EIO);

  effects[0].owner = &owner;
  assert(ff_close(&owner) == -EIO && erased[0] == 2 &&
         effects[0].owner == NULL);

  fail_play = 0;
  fail_erase = 1;
  effects[0].owner = &owner;
  assert(ff_close(&owner) == -EIO && effects[0].owner == NULL);
  ff_unregister(lower, "/dev/ff-lifecycle");
  assert(destroyed == 1);
  return 0;
}
"""


class FfLifecycleTest(unittest.TestCase):
    def test_patched_upperhalf_owner_and_write_behavior(self) -> None:
        self.assertTrue(PATCH.is_file())
        self.assertTrue((UPSTREAM / "drivers/input/ff_upper.c").is_file())

        with tempfile.TemporaryDirectory(prefix="ff-lifecycle-") as temp:
            tree = Path(temp)
            source = tree / "drivers/input/ff_upper.c"
            source.parent.mkdir(parents=True)
            shutil.copy2(UPSTREAM / "drivers/input/ff_upper.c", source)
            for relative, text in HEADERS.items():
                header = tree / relative
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(text)

            subprocess.run(
                ["patch", "--batch", "--silent", "-p1", "-d", str(tree)],
                input=PATCH.read_text(), text=True, check=True,
            )
            harness = tree / "ff_lifecycle_harness.c"
            binary = tree / "ff_lifecycle_harness"
            harness.write_text(HARNESS)
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Werror", "-fsanitize=address",
                 "-fno-omit-frame-pointer", "-I", str(tree),
                 str(harness), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True,
                           env={"ASAN_OPTIONS": "detect_leaks=0"})


if __name__ == "__main__":
    unittest.main()
