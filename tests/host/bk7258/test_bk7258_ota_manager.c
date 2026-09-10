/****************************************************************************
 * tests/host/bk7258/test_bk7258_ota_manager.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <arch/chip/bk7258_ota_manager.h>
#include <arch/chip/bk7258_ota_rpmsg.h>

struct fixture_s
{
  pthread_mutex_t lock;
  pthread_cond_t changed;
  bool stage_entered;
  bool allow_close;
  bool close_entered;
  bool allow_close_return;
  bool closed;
  bool cancel_thread_entered;
  bool cancel_thread_returned;
  int cancel_calls;
  int close_calls;
  int rpmsg_cancel_calls;
  int apply_result;
  int cancel_result;
};

static struct fixture_s *g_fixture;

static int fake_open(void *context, struct bk7258_ota_manifest_s *manifest)
{
  (void)context;
  (void)manifest;
  return 0;
}

static int fake_read(void *context, enum bk7258_ota_image_e image,
                     uint32_t offset, uint8_t *buffer, size_t nbytes)
{
  (void)context;
  (void)image;
  (void)offset;
  (void)buffer;
  (void)nbytes;
  return 0;
}

static int fake_cancel(void *context)
{
  struct fixture_s *fixture = context;

  pthread_mutex_lock(&fixture->lock);
  assert(!fixture->closed);
  fixture->cancel_calls++;
  pthread_mutex_unlock(&fixture->lock);
  return 0;
}

static void fake_close(void *context)
{
  struct fixture_s *fixture = context;

  pthread_mutex_lock(&fixture->lock);
  fixture->close_entered = true;
  fixture->close_calls++;
  pthread_cond_broadcast(&fixture->changed);
  while (!fixture->allow_close_return)
    {
      pthread_cond_wait(&fixture->changed, &fixture->lock);
    }

  fixture->closed = true;
  pthread_mutex_unlock(&fixture->lock);
}

static const struct bk7258_ota_source_ops_s g_source_ops =
{
  .open = fake_open,
  .read_at = fake_read,
  .cancel = fake_cancel,
  .close = fake_close,
};

int bk7258_ota_rpmsg_stage(const struct bk7258_ota_source_ops_s *source,
                           void *context, uint32_t timeout_ms)
{
  struct fixture_s *fixture = g_fixture;

  (void)timeout_ms;
  pthread_mutex_lock(&fixture->lock);
  fixture->stage_entered = true;
  pthread_cond_broadcast(&fixture->changed);
  while (!fixture->allow_close)
    {
      pthread_cond_wait(&fixture->changed, &fixture->lock);
    }
  pthread_mutex_unlock(&fixture->lock);

  source->close(context);
  return 0;
}

int bk7258_ota_rpmsg_cancel(void)
{
  pthread_mutex_lock(&g_fixture->lock);
  g_fixture->rpmsg_cancel_calls++;
  pthread_mutex_unlock(&g_fixture->lock);
  return 0;
}

static void fixture_initialize(struct fixture_s *fixture)
{
  memset(fixture, 0, sizeof(*fixture));
  assert(pthread_mutex_init(&fixture->lock, NULL) == 0);
  assert(pthread_cond_init(&fixture->changed, NULL) == 0);
  g_fixture = fixture;
}

static void fixture_destroy(struct fixture_s *fixture)
{
  assert(pthread_cond_destroy(&fixture->changed) == 0);
  assert(pthread_mutex_destroy(&fixture->lock) == 0);
}

static void wait_flag(struct fixture_s *fixture, const bool *flag)
{
  pthread_mutex_lock(&fixture->lock);
  while (!*flag)
    {
      pthread_cond_wait(&fixture->changed, &fixture->lock);
    }
  pthread_mutex_unlock(&fixture->lock);
}

static void set_flag(struct fixture_s *fixture, bool *flag)
{
  pthread_mutex_lock(&fixture->lock);
  *flag = true;
  pthread_cond_broadcast(&fixture->changed);
  pthread_mutex_unlock(&fixture->lock);
}

static void *apply_thread(void *context)
{
  struct fixture_s *fixture = context;

  fixture->apply_result =
    bk7258_ota_manager_apply(&g_source_ops, fixture, 1000u);
  return NULL;
}

static void *cancel_thread(void *context)
{
  struct fixture_s *fixture = context;

  pthread_mutex_lock(&fixture->lock);
  fixture->cancel_thread_entered = true;
  pthread_cond_broadcast(&fixture->changed);
  pthread_mutex_unlock(&fixture->lock);

  fixture->cancel_result = bk7258_ota_manager_cancel();

  pthread_mutex_lock(&fixture->lock);
  fixture->cancel_thread_returned = true;
  pthread_cond_broadcast(&fixture->changed);
  pthread_mutex_unlock(&fixture->lock);
  return NULL;
}

static void test_cancel_before_close(void)
{
  struct fixture_s fixture;
  pthread_t worker;

  fixture_initialize(&fixture);
  assert(pthread_create(&worker, NULL, apply_thread, &fixture) == 0);
  wait_flag(&fixture, &fixture.stage_entered);

  assert(bk7258_ota_manager_cancel() == 0);
  assert(fixture.cancel_calls == 1);
  assert(fixture.rpmsg_cancel_calls == 1);

  set_flag(&fixture, &fixture.allow_close);
  set_flag(&fixture, &fixture.allow_close_return);
  assert(pthread_join(worker, NULL) == 0);
  assert(fixture.apply_result == 0);
  assert(fixture.close_calls == 1);
  fixture_destroy(&fixture);
}

static void test_cancel_waits_for_close(void)
{
  struct fixture_s fixture;
  struct timespec pause = {.tv_nsec = 20 * 1000 * 1000};
  pthread_t worker;
  pthread_t canceler;

  fixture_initialize(&fixture);
  assert(pthread_create(&worker, NULL, apply_thread, &fixture) == 0);
  wait_flag(&fixture, &fixture.stage_entered);
  set_flag(&fixture, &fixture.allow_close);
  wait_flag(&fixture, &fixture.close_entered);

  assert(pthread_create(&canceler, NULL, cancel_thread, &fixture) == 0);
  wait_flag(&fixture, &fixture.cancel_thread_entered);
  nanosleep(&pause, NULL);

  pthread_mutex_lock(&fixture.lock);
  assert(!fixture.cancel_thread_returned);
  assert(fixture.cancel_calls == 0);
  pthread_mutex_unlock(&fixture.lock);

  set_flag(&fixture, &fixture.allow_close_return);
  assert(pthread_join(worker, NULL) == 0);
  assert(pthread_join(canceler, NULL) == 0);
  assert(fixture.apply_result == 0);
  assert(fixture.cancel_result == -ENOENT);
  assert(fixture.cancel_calls == 0);
  assert(fixture.close_calls == 1);
  fixture_destroy(&fixture);
}

int main(void)
{
  assert(bk7258_ota_manager_initialize() == 0);
  test_cancel_before_close();
  test_cancel_waits_for_close();
  puts("bk7258 ota manager host tests: PASS");
  return 0;
}
