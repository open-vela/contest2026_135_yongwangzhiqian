/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_provision_time.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <netutils/ntpclient.h>

/* Keep real clocks and NTP untouched. Hold the fake child until the test
 * releases it, proving the caller never waits on the network worker.
 */
static atomic_llong monotonic = 100, wall = 1800000000;
static atomic_int starts, child_result, sample_count = 3;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static bool released;
int __wrap_clock_gettime(clockid_t id, struct timespec *value)
{
  value->tv_sec = atomic_load(id == CLOCK_REALTIME ? &wall : &monotonic);
  value->tv_nsec = 0;
  return 0;
}
int ntpc_start(void) { atomic_fetch_add(&starts, 1); return 42; }
int ntpc_status(struct ntpc_status_s *samples)
{ samples->nsamples = atomic_load(&sample_count); return 0; }
pid_t __wrap_waitpid(pid_t child, int *status, int options)
{
  assert(child == 42 && options == 0);
  pthread_mutex_lock(&lock);
  while (!released) pthread_cond_wait(&wake, &lock);
  released = false;
  pthread_mutex_unlock(&lock);
  *status = atomic_load(&child_result) << 8;
  return child;
}
static int finish(uint64_t *utc)
{
  pthread_mutex_lock(&lock);
  released = true;
  pthread_cond_signal(&wake);
  pthread_mutex_unlock(&lock);
  for (unsigned i = 0; i < 2000; i++)
    {
      int ret = bkprov_time_get(1704067200, utc);
      if (ret != -EAGAIN) return ret;
      struct timespec delay = {.tv_nsec = 1000000};
      nanosleep(&delay, NULL);
    }
  assert(!"time worker failed to finish");
  return -ETIMEDOUT;
}
int main(void)
{
  uint64_t utc = 0;
  assert(bkprov_time_get(0, &utc) == -ENODEV);
  assert(bkprov_time_start() == 0 && bkprov_time_start() == 0);
  for (int i = 0; i < 100; i++) assert(bkprov_time_get(0, &utc) == -EAGAIN);
  assert(finish(&utc) == 0 && utc == 1800000000 && atomic_load(&starts) == 1);
  atomic_fetch_add(&monotonic, 10);
  assert(bkprov_time_get(0, &utc) == 0 && utc == 1800000010);
  assert(bkprov_time_get(1800000011, &utc) == -ERANGE);

  atomic_fetch_add(&monotonic, 21600);
  atomic_store(&child_result, 1);
  assert(bkprov_time_get(0, &utc) == -EAGAIN);
  /* Stale samples from the preceding successful run must not mask failure. */
  assert(finish(&utc) == -EIO && atomic_load(&starts) == 2);
  assert(bkprov_time_get(0, &utc) == -EIO && atomic_load(&starts) == 2);
  atomic_fetch_add(&monotonic, 61);
  atomic_store(&child_result, 0); atomic_store(&sample_count, 0);
  assert(bkprov_time_get(0, &utc) == -EAGAIN);
  assert(finish(&utc) == -ENODATA);
  atomic_fetch_add(&monotonic, 61);
  atomic_store(&sample_count, 3); atomic_store(&wall, 1);
  assert(bkprov_time_get(0, &utc) == -EAGAIN);
  assert(finish(&utc) == -ERANGE);
  puts("BKPROV_TIME_PASS: nonblocking worker, fresh completion, cache and retry");
  return 0;
}
