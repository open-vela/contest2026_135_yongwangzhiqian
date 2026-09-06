#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the real GPIO FF lower half with deterministic Linux host mocks.

Consumed by unittest discovery under tests/host/bk7258. Pthreads exercise
power-on cancellation and synchronous destruction; manually fired watchdogs
make timing assertions independent of host scheduling. No target headers or
generated ARM configuration enter the host compiler's include search path.
"""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
MOCK = r"""
#pragma once
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
/* NuttX ticks are unsigned; Linux's native clock_t is signed. */
typedef uint32_t mock_clock_t;
#define clock_t mock_clock_t
#define FAR
#define CODE
#define OK 0
#define CONFIG_PM 1
#define FF_RUMBLE 0
#define LPWORK 0
#define PM_IDLE_DOMAIN 0
#define PM_NORMAL 0
#define MSEC2TICK(n) ((clock_t)(n))
#define set_bit(n, a) ((a)[(n) / 64] |= 1ul << ((n) % 64))
typedef unsigned long irqstate_t;
typedef atomic_flag spinlock_t;
typedef pthread_mutex_t mutex_t;
typedef uintptr_t wdparm_t;
struct ff_effect {
  uint16_t type; int16_t id; uint16_t direction;
  struct { uint16_t button, interval; } trigger;
  struct { uint16_t length, delay; } replay;
  union { struct { uint16_t strong_magnitude, weak_magnitude; } rumble; } u;
};
struct ff_lowerhalf_s {
  int (*upload)(struct ff_lowerhalf_s *, struct ff_effect *, struct ff_effect *);
  int (*erase)(struct ff_lowerhalf_s *, int);
  int (*playback)(struct ff_lowerhalf_s *, int, int);
  void (*destroy)(struct ff_lowerhalf_s *);
  unsigned long ffbit[2];
};
struct wdog_s { bool active; clock_t ticks; void (*fn)(wdparm_t); wdparm_t arg; };
struct work_s { bool queued, running; void (*fn)(void *); void *arg; pthread_t tid; };
typedef struct {
  pthread_mutex_t lock; pthread_cond_t cond; unsigned int count;
} sem_t;
static atomic_uint_least32_t now;
static atomic_int stays, relaxes, allocations, wd_failure, queue_failure;
static atomic_int sem_waiting, power_lock_waiting;
static atomic_int trace_power_lock;
static struct wdog_s *active_wd;
static struct work_s *active_work;
static pthread_mutex_t wd_lock = PTHREAD_MUTEX_INITIALIZER;
static clock_t clock_systime_ticks(void) { return atomic_load(&now); }
static irqstate_t spin_lock_irqsave(spinlock_t *lock) {
  while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {}
  return 0;
}
static void spin_unlock_irqrestore(spinlock_t *lock, irqstate_t flags) {
  (void)flags; atomic_flag_clear_explicit(lock, memory_order_release);
}
static int nxmutex_init(mutex_t *lock) { return -pthread_mutex_init(lock, NULL); }
static int nxmutex_destroy(mutex_t *lock) { return -pthread_mutex_destroy(lock); }
static int nxmutex_lock(mutex_t *lock) {
  if (atomic_load(&trace_power_lock)) atomic_store(&power_lock_waiting, 1);
  return -pthread_mutex_lock(lock);
}
static int nxmutex_unlock(mutex_t *lock) { return -pthread_mutex_unlock(lock); }
static int nxsem_init(sem_t *s, int shared, unsigned int value) {
  (void)shared; s->count = value;
  assert(pthread_mutex_init(&s->lock, NULL) == 0);
  return -pthread_cond_init(&s->cond, NULL);
}
static int nxsem_destroy(sem_t *s) {
  assert(pthread_mutex_destroy(&s->lock) == 0);
  return -pthread_cond_destroy(&s->cond);
}
static int nxsem_post(sem_t *s) {
  pthread_mutex_lock(&s->lock); s->count++;
  pthread_cond_signal(&s->cond); pthread_mutex_unlock(&s->lock); return 0;
}
static int nxsem_trywait(sem_t *s) {
  int ret = -EAGAIN;
  pthread_mutex_lock(&s->lock);
  if (s->count) { s->count--; ret = 0; }
  pthread_mutex_unlock(&s->lock); return ret;
}
static int nxsem_tickwait_uninterruptible(sem_t *s, clock_t ticks) {
  (void)ticks;
  pthread_mutex_lock(&s->lock); atomic_store(&sem_waiting, 1);
  while (!s->count) pthread_cond_wait(&s->cond, &s->lock);
  s->count--; pthread_mutex_unlock(&s->lock); return 0;
}
static void wd_init(struct wdog_s *wd) { wd->active = false; }
static int wd_cancel(struct wdog_s *wd) {
  pthread_mutex_lock(&wd_lock); wd->active = false;
  if (active_wd == wd) active_wd = NULL;
  pthread_mutex_unlock(&wd_lock); return 0;
}
static int wd_start(struct wdog_s *wd, clock_t ticks,
                    void (*fn)(wdparm_t), wdparm_t arg) {
  if (atomic_load(&wd_failure)) return -EIO;
  pthread_mutex_lock(&wd_lock);
  wd->active = true; wd->ticks = ticks; wd->fn = fn; wd->arg = arg;
  active_wd = wd; pthread_mutex_unlock(&wd_lock); return 0;
}
static int work_queue(int q, struct work_s *w, void (*fn)(void *),
                      void *arg, clock_t delay) {
  (void)q; (void)delay;
  if (atomic_load(&queue_failure)) return -EIO;
  assert(!w->queued && !w->running);
  w->queued = true; w->fn = fn; w->arg = arg; active_work = w; return 0;
}
static int work_cancel_sync(int q, struct work_s *w) {
  (void)q; w->queued = false;
  if (w->running) { assert(pthread_join(w->tid, NULL) == 0); w->running = false; }
  if (active_work == w) active_work = NULL;
  return 0;
}
static void pm_stay(int domain, int state) {
  (void)domain; (void)state; atomic_fetch_add(&stays, 1);
}
static void pm_relax(int domain, int state) {
  (void)domain; (void)state;
  assert(atomic_fetch_add(&relaxes, 1) < atomic_load(&stays));
}
static void *kmm_zalloc(size_t size) {
  void *p = calloc(1, size); if (p) atomic_fetch_add(&allocations, 1); return p;
}
static void kmm_free(void *p) { atomic_fetch_sub(&allocations, 1); free(p); }
static int ff_register(struct ff_lowerhalf_s *lower, const char *path, int n) {
  (void)lower; (void)path; assert(n == 1); return 0;
}
"""

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <sched.h>
#include "gpio_ff.c"

static struct ff_lowerhalf_s *lower;
static atomic_int powered, output, rises, power_calls, off_failure, power_failure;
static atomic_int gate_power, entered_power, release_power, api_done;
static atomic_int gate_power_off, entered_power_off, release_power_off;
static atomic_int enable_failure;
static int api_ret;

/* No real-time sleeps: wait for an explicit phase transition, with the Python
 * subprocess timeout detecting deadlocks. */
static void wait_for(atomic_int *phase) {
  while (!atomic_load(phase)) sched_yield();
}
static int set_output(void *arg, bool on) {
  (void)arg;
  if (!on && atomic_load(&off_failure)) return -EIO;
  if (on) {
    assert(atomic_load(&powered));
    assert(atomic_load(&stays) == atomic_load(&relaxes) + 1);
    atomic_fetch_add(&rises, 1);
  }
  atomic_store(&output, on);
  return on && atomic_load(&enable_failure) ? -EIO : 0;
}
static int set_power(void *arg, bool on) {
  (void)arg;
  if (on) {
    atomic_fetch_add(&power_calls, 1);
    atomic_store(&powered, 1); /* Failure may follow partial power acquisition. */
    if (atomic_load(&gate_power)) {
      atomic_store(&entered_power, 1); wait_for(&release_power);
    }
    if (atomic_load(&power_failure)) return -EIO;
  } else {
    if (atomic_load(&gate_power_off)) {
      atomic_store(&entered_power_off, 1); wait_for(&release_power_off);
    }
    atomic_store(&powered, 0);
  }
  return 0;
}
static void setup(void) {
  struct gpio_ff_config_s config = {
    .set_output = set_output, .set_power = set_power,
    .max_on_ms = 100, .min_off_ms = 20
  };
  struct ff_effect effect = {
    .type = FF_RUMBLE, .replay.length = 60,
    .u.rumble.strong_magnitude = 1
  };
  assert(gpio_ff_register("/dev/ff0", &config, &lower) == 0);
  assert(lower->upload(lower, &effect, NULL) == 0);
}
static void *run_work(void *arg) {
  struct work_s *w = arg; w->fn(w->arg); return NULL;
}
static void launch(void) {
  assert(active_work && active_work->queued && !active_work->running);
  active_work->queued = false; active_work->running = true;
  atomic_store(&sem_waiting, 0);
  assert(pthread_create(&active_work->tid, NULL, run_work, active_work) == 0);
}
static void join_work(void) {
  assert(active_work && active_work->running);
  assert(pthread_join(active_work->tid, NULL) == 0);
  active_work->running = false;
  assert(!atomic_load(&powered));
  assert(atomic_load(&stays) == atomic_load(&relaxes));
}
static void start(void) { assert(lower->playback(lower, 0, 1) == 0); }
static void expire(void) {
  void (*fn)(wdparm_t); wdparm_t arg;
  pthread_mutex_lock(&wd_lock);
  assert(active_wd && active_wd->active);
  assert(active_wd->ticks == 60);
  atomic_fetch_add(&now, active_wd->ticks);
  fn = active_wd->fn; arg = active_wd->arg;
  active_wd->active = false; active_wd = NULL;
  pthread_mutex_unlock(&wd_lock); fn(arg);
}
static void cleanup(void) {
  atomic_store(&off_failure, 0);
  lower->destroy(lower); lower = NULL;
  assert(!active_work && !active_wd);
  assert(!atomic_load(&output) && !atomic_load(&powered));
  assert(atomic_load(&stays) == atomic_load(&relaxes));
  assert(atomic_load(&allocations) == 0);
}
static void *inhibit_thread(void *arg) {
  (void)arg; api_ret = gpio_ff_inhibit(lower, true);
  atomic_store(&api_done, 1); return NULL;
}
static void *destroy_thread(void *arg) {
  (void)arg; lower->destroy(lower); atomic_store(&api_done, 1); return NULL;
}
int main(int argc, char **argv) {
  pthread_t thread;
  assert(argc == 2); setup();
  if (!strcmp(argv[1], "timeout_cooldown")) {
    start(); assert(lower->playback(lower, 0, 1) == -EBUSY);
    launch(); wait_for(&sem_waiting);
    assert(atomic_load(&output));
    assert(lower->playback(lower, 0, 1) == -EBUSY);
    expire(); assert(!atomic_load(&output)); join_work();
    assert(lower->playback(lower, 0, 1) == -EBUSY);
    atomic_fetch_add(&now, 19);
    assert(lower->playback(lower, 0, 1) == -EBUSY);
    atomic_fetch_add(&now, 1); start(); launch(); wait_for(&sem_waiting);
    expire(); join_work(); assert(atomic_load(&rises) == 2);
  } else if (!strcmp(argv[1], "tick_wrap_cooldown")) {
    atomic_store(&now, UINT32_MAX - 69);
    start(); launch(); wait_for(&sem_waiting); expire(); join_work();
    assert(atomic_load(&now) == UINT32_MAX - 9);
    assert(lower->playback(lower, 0, 1) == -EBUSY);
    atomic_fetch_add(&now, 19);
    assert(atomic_load(&now) == 9);
    assert(lower->playback(lower, 0, 1) == -EBUSY);
    atomic_fetch_add(&now, 1);
    start(); launch(); wait_for(&sem_waiting); expire(); join_work();
    assert(atomic_load(&rises) == 2);
  } else if (!strcmp(argv[1], "cancel_power_on")) {
    atomic_store(&gate_power, 1); start(); launch(); wait_for(&entered_power);
    assert(lower->playback(lower, 0, 0) == 0);
    atomic_store(&release_power, 1); join_work();
    assert(atomic_load(&rises) == 0);
  } else if (!strcmp(argv[1], "inhibit_power_on")) {
    atomic_store(&gate_power, 1); start(); launch(); wait_for(&entered_power);
    atomic_store(&trace_power_lock, 1);
    assert(pthread_create(&thread, NULL, inhibit_thread, NULL) == 0);
    wait_for(&power_lock_waiting);
    assert(!atomic_load(&api_done));
    atomic_store(&release_power, 1);
    assert(pthread_join(thread, NULL) == 0); join_work();
    assert(api_ret == 0 && atomic_load(&rises) == 0);
    assert(lower->playback(lower, 0, 1) == -EPERM);
    assert(gpio_ff_inhibit(lower, false) == 0);
    start(); launch(); wait_for(&sem_waiting); expire(); join_work();
  } else if (!strcmp(argv[1], "watchdog_failure")) {
    atomic_store(&wd_failure, 1); start(); launch(); join_work();
    assert(atomic_load(&rises) == 1 && !atomic_load(&output));
    assert(!active_wd);
  } else if (!strcmp(argv[1], "power_failure")) {
    atomic_store(&power_failure, 1); start(); launch(); join_work();
    assert(atomic_load(&rises) == 0 && atomic_load(&stays) == 1);
    atomic_store(&power_failure, 0);
    start(); launch(); wait_for(&sem_waiting); expire(); join_work();
  } else if (!strcmp(argv[1], "power_failure_cleanup_busy")) {
    atomic_store(&power_failure, 1); atomic_store(&gate_power_off, 1);
    start(); launch(); wait_for(&entered_power_off);
    assert(atomic_load(&powered) && atomic_load(&rises) == 0);
    assert(atomic_load(&stays) == atomic_load(&relaxes) + 1);
    assert(lower->playback(lower, 0, 1) == -EBUSY);
    atomic_store(&release_power_off, 1); join_work();
    atomic_store(&gate_power_off, 0); atomic_store(&power_failure, 0);
    start(); launch(); wait_for(&sem_waiting); expire(); join_work();
  } else if (!strcmp(argv[1], "partial_enable_stop_failure")) {
    atomic_store(&enable_failure, 1); atomic_store(&off_failure, 1);
    start(); launch(); join_work();
    assert(atomic_load(&output) && atomic_load(&rises) == 1);
    assert(!active_wd && lower->playback(lower, 0, 1) == -EIO);
    assert(gpio_ff_inhibit(lower, true) == -EIO);
    assert(atomic_load(&output));
    atomic_store(&off_failure, 0);
    assert(gpio_ff_inhibit(lower, true) == 0);
    assert(!atomic_load(&output));
    assert(lower->playback(lower, 0, 1) == -EPERM);
    assert(gpio_ff_inhibit(lower, false) == 0);
    assert(lower->playback(lower, 0, 1) == -EIO);
  } else if (!strcmp(argv[1], "queue_failure")) {
    atomic_store(&queue_failure, 1);
    assert(lower->playback(lower, 0, 1) == -EIO);
    assert(!atomic_load(&output) && atomic_load(&stays) == 0);
    atomic_store(&queue_failure, 0);
    start(); launch(); wait_for(&sem_waiting); expire(); join_work();
  } else if (!strcmp(argv[1], "stop_failure") ||
             !strcmp(argv[1], "inhibit_failure") ||
             !strcmp(argv[1], "erase_failure")) {
    start(); launch(); wait_for(&sem_waiting); atomic_store(&off_failure, 1);
    if (!strcmp(argv[1], "stop_failure"))
      assert(lower->playback(lower, 0, 0) == -EIO);
    else if (!strcmp(argv[1], "inhibit_failure"))
      assert(gpio_ff_inhibit(lower, true) == -EIO);
    else assert(lower->erase(lower, 0) == -EIO);
    join_work();
    assert(gpio_ff_inhibit(lower, false) == 0);
    assert(lower->playback(lower, 0, 1) == -EIO);
  } else if (!strcmp(argv[1], "destroy_queued")) {
    start(); cleanup(); assert(atomic_load(&power_calls) == 0); return 0;
  } else if (!strcmp(argv[1], "destroy_running")) {
    start(); launch(); wait_for(&sem_waiting); cleanup(); return 0;
  } else if (!strcmp(argv[1], "destroy_power_on")) {
    atomic_store(&gate_power, 1); start(); launch(); wait_for(&entered_power);
    assert(pthread_create(&thread, NULL, destroy_thread, NULL) == 0);
    /* stop_sem is posted after destruction invalidates the start generation. */
    struct gpio_ff_dev_s *dev = (struct gpio_ff_dev_s *)lower;
    for (;;) {
      pthread_mutex_lock(&dev->stop_sem.lock);
      bool stopped = dev->stop_sem.count != 0;
      pthread_mutex_unlock(&dev->stop_sem.lock);
      if (stopped) break;
      sched_yield();
    }
    assert(!atomic_load(&api_done)); atomic_store(&release_power, 1);
    assert(pthread_join(thread, NULL) == 0);
    assert(atomic_load(&rises) == 0 && !active_work && !active_wd);
    assert(!atomic_load(&powered) && !atomic_load(&allocations));
    assert(atomic_load(&stays) == atomic_load(&relaxes)); return 0;
  } else assert(!"unknown scenario");
  cleanup(); return 0;
}
"""


class GpioFfTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temp = tempfile.TemporaryDirectory(prefix="gpio-ff-host-")
        cls.addClassCleanup(cls.temp.cleanup)
        tree = Path(cls.temp.name)
        (tree / "mock.h").write_text(MOCK)
        for name in (
            "config.h", "compiler.h", "bits.h", "clock.h", "input/ff.h",
            "irq.h", "kmalloc.h", "mutex.h", "power/pm.h", "semaphore.h",
            "spinlock.h", "wdog.h", "wqueue.h",
        ):
            header = tree / "nuttx" / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "mock.h"\n')
        harness = tree / "harness.c"
        harness.write_text(HARNESS)
        cls.binary = tree / "gpio_ff_host"
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pthread",
             "-fsanitize=undefined", "-fno-sanitize-recover=all",
             "-I", str(tree), "-I", str(REPOSITORY / "nuttx/include"),
             "-I", str(REPOSITORY / "nuttx/drivers/input"),
             str(harness), "-o", str(cls.binary)],
            check=True, capture_output=True, text=True,
        )

    def test_lowerhalf_lifecycle(self) -> None:
        for scenario in (
            "timeout_cooldown", "tick_wrap_cooldown",
            "cancel_power_on", "inhibit_power_on",
            "watchdog_failure", "power_failure", "queue_failure",
            "power_failure_cleanup_busy", "partial_enable_stop_failure",
            "stop_failure", "inhibit_failure", "erase_failure",
            "destroy_queued", "destroy_running", "destroy_power_on",
        ):
            with self.subTest(scenario=scenario):
                result = subprocess.run(
                    [str(self.binary), scenario], capture_output=True,
                    text=True, timeout=10,
                )
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
