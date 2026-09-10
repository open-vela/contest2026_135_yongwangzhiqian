/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_ota_cancel.c
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

#include "bk7258_voice_ota_admission.h"
#include "bk7258_voice_ota_cancel.h"

struct admission_fixture_s
{
  volatile uint8_t state;
  volatile bool cancel;
  volatile bool go;
  volatile int result;
  volatile unsigned int rejection_writers;
  volatile unsigned int image_reads;
  uint32_t published_target;
  uint32_t durable_target;
};

static void *publish_target(void *context)
{
  struct admission_fixture_s *fixture = context;

  fixture->published_target = 450;
  bkvoice_ota_target_admission_publish(&fixture->state);
  while (bkvoice_ota_target_admission_load(&fixture->state) ==
         BKVOICE_OTA_TARGET_READY ||
         bkvoice_ota_target_admission_load(&fixture->state) ==
         BKVOICE_OTA_TARGET_COMMITTING)
    {
      sched_yield();
    }

  assert(bkvoice_ota_target_admission_load(&fixture->state) ==
         BKVOICE_OTA_TARGET_APPROVED);
  assert(fixture->durable_target == fixture->published_target);
  return NULL;
}

static void finish_canceled_admission(struct admission_fixture_s *fixture)
{
  __atomic_fetch_add(&fixture->rejection_writers, 1u, __ATOMIC_RELAXED);
  __atomic_store_n(&fixture->result, -ECANCELED, __ATOMIC_RELAXED);
  bkvoice_ota_target_admission_finish(&fixture->state, false);
}

static void *publish_canceled_target(void *context)
{
  struct admission_fixture_s *fixture = context;

  while (!__atomic_load_n(&fixture->go, __ATOMIC_ACQUIRE))
    {
      sched_yield();
    }

  fixture->published_target = 450;
  bkvoice_ota_target_admission_publish(&fixture->state);
  if (__atomic_load_n(&fixture->cancel, __ATOMIC_ACQUIRE) &&
      bkvoice_ota_target_admission_claim_rejection(&fixture->state))
    {
      finish_canceled_admission(fixture);
    }

  if (bkvoice_ota_target_admission_load(&fixture->state) ==
      BKVOICE_OTA_TARGET_APPROVED)
    {
      __atomic_fetch_add(&fixture->image_reads, 1u, __ATOMIC_RELAXED);
    }

  return NULL;
}

static void *cancel_target_publication(void *context)
{
  struct admission_fixture_s *fixture = context;

  while (!__atomic_load_n(&fixture->go, __ATOMIC_ACQUIRE))
    {
      sched_yield();
    }

  __atomic_store_n(&fixture->cancel, true, __ATOMIC_RELEASE);
  if (bkvoice_ota_target_admission_claim_rejection(&fixture->state))
    {
      finish_canceled_admission(fixture);
    }

  return NULL;
}

static bool intent_after(bool intent_present,
                         enum bkvoice_ota_cancel_disposition_e disposition)
{
  return disposition == BKVOICE_OTA_CANCEL_CLEAR ? false : intent_present;
}

static void test_queued_and_applying_cancel(void)
{
  enum bkvoice_ota_cancel_disposition_e disposition;

  disposition = bkvoice_ota_cancel_disposition(
                  BKVOICE_OTA_JOB_QUEUED, 0, -EINPROGRESS, false, false);
  assert(disposition == BKVOICE_OTA_CANCEL_CLEAR);
  assert(!intent_after(true, disposition));

  disposition = bkvoice_ota_cancel_disposition(
                  BKVOICE_OTA_JOB_APPLYING, 0, -EINPROGRESS, true, false);
  assert(disposition == BKVOICE_OTA_CANCEL_WAIT);
  assert(intent_after(true, disposition));
  assert(bkvoice_ota_cancel_requires_join(BKVOICE_OTA_JOB_APPLYING, true));

  disposition = bkvoice_ota_cancel_disposition(
                  BKVOICE_OTA_JOB_DONE, 0, -ECANCELED, true, false);
  assert(disposition == BKVOICE_OTA_CANCEL_CLEAR);
  assert(!intent_after(true, disposition));
}

static void test_completed_apply_races(void)
{
  enum bkvoice_ota_cancel_disposition_e disposition;

  disposition = bkvoice_ota_cancel_disposition(
                  BKVOICE_OTA_JOB_APPLYING, -EALREADY,
                  -EINPROGRESS, true, false);
  assert(disposition == BKVOICE_OTA_CANCEL_PRESERVE);
  assert(intent_after(true, disposition));

  disposition = bkvoice_ota_cancel_disposition(
                  BKVOICE_OTA_JOB_DONE, -ENOENT, 0, true, false);
  assert(disposition == BKVOICE_OTA_CANCEL_PRESERVE);
  assert(intent_after(true, disposition));
  assert(bkvoice_ota_cancel_requires_join(BKVOICE_OTA_JOB_DONE, true));
  assert(!bkvoice_ota_cancel_requires_join(BKVOICE_OTA_JOB_DONE, false));

  disposition = bkvoice_ota_cancel_disposition(
                  BKVOICE_OTA_JOB_DONE, -ENOENT, -EALREADY, true, false);
  assert(disposition == BKVOICE_OTA_CANCEL_PRESERVE);
  assert(intent_after(true, disposition));
}

static void test_committed_states_keep_intent(void)
{
  const uint8_t states[] =
  {
    BKVOICE_OTA_JOB_STAGED,
    BKVOICE_OTA_JOB_REBOOTING,
    BKVOICE_OTA_JOB_TRIAL,
  };
  size_t i;

  for (i = 0; i < sizeof(states) / sizeof(states[0]); i++)
    {
      enum bkvoice_ota_cancel_disposition_e disposition =
        bkvoice_ota_cancel_disposition(states[i], -ENOENT, 0, true, false);
      assert(disposition == BKVOICE_OTA_CANCEL_PRESERVE);
      assert(intent_after(true, disposition));
    }
}

static void test_failed_apply_can_be_cleared(void)
{
  enum bkvoice_ota_cancel_disposition_e disposition;

  disposition =
    bkvoice_ota_cancel_disposition(BKVOICE_OTA_JOB_DONE, -ENOENT,
                                   -EIO, true, false);
  assert(disposition == BKVOICE_OTA_CANCEL_CLEAR);
  assert(!intent_after(true, disposition));

  disposition = bkvoice_ota_cancel_disposition(BKVOICE_OTA_JOB_DONE,
                                                -ENOENT, 0, false, false);
  assert(disposition == BKVOICE_OTA_CANCEL_CLEAR);
  assert(!intent_after(true, disposition));
}

static void test_target_persistence_boundary(void)
{
  assert(!bkvoice_ota_intent_is_commit_sensitive(
           true, true, 0, false));
  assert(bkvoice_ota_intent_is_commit_sensitive(
           true, true, 450, false));
  assert(bkvoice_ota_intent_is_commit_sensitive(
           true, true, 0, true));
  assert(bkvoice_ota_intent_is_commit_sensitive(
           true, false, 450, false));

  /* Once target publication starts, timeout and successful apply races keep
   * the durable intent even if the in-memory target flag is unavailable. */

  assert(bkvoice_ota_cancel_disposition(BKVOICE_OTA_JOB_DONE,
           -ENOENT, -ETIMEDOUT, false, true) ==
           BKVOICE_OTA_CANCEL_PRESERVE);
  assert(bkvoice_ota_cancel_disposition(BKVOICE_OTA_JOB_DONE,
           -ENOENT, 0, false, true) == BKVOICE_OTA_CANCEL_PRESERVE);
}

static void test_target_admission_publication(void)
{
  struct admission_fixture_s fixture = {0};
  pthread_t worker;

  assert(pthread_create(&worker, NULL, publish_target, &fixture) == 0);
  while (bkvoice_ota_target_admission_load(&fixture.state) !=
         BKVOICE_OTA_TARGET_READY)
    {
      sched_yield();
    }

  assert(bkvoice_ota_target_admission_claim(&fixture.state));
  assert(fixture.published_target == 450);
  fixture.durable_target = fixture.published_target;
  bkvoice_ota_target_admission_finish(&fixture.state, true);
  assert(pthread_join(worker, NULL) == 0);

  fixture.state = BKVOICE_OTA_TARGET_IDLE;
  bkvoice_ota_target_admission_publish(&fixture.state);
  assert(bkvoice_ota_target_admission_claim_rejection(&fixture.state));
  fixture.result = -ECANCELED;
  bkvoice_ota_target_admission_finish(&fixture.state, false);
  assert(!bkvoice_ota_target_admission_claim(&fixture.state));
}

static void test_cancel_publish_race(void)
{
  unsigned int iteration;

  for (iteration = 0; iteration < 256u; iteration++)
    {
      struct admission_fixture_s fixture = {0};
      pthread_t publisher;
      pthread_t canceler;

      __atomic_store_n(&fixture.result, -EINPROGRESS, __ATOMIC_RELAXED);
      assert(pthread_create(&publisher, NULL, publish_canceled_target,
                            &fixture) == 0);
      assert(pthread_create(&canceler, NULL, cancel_target_publication,
                            &fixture) == 0);
      __atomic_store_n(&fixture.go, true, __ATOMIC_RELEASE);
      assert(pthread_join(publisher, NULL) == 0);
      assert(pthread_join(canceler, NULL) == 0);
      assert(bkvoice_ota_target_admission_load(&fixture.state) ==
             BKVOICE_OTA_TARGET_REJECTED);
      assert(__atomic_load_n(&fixture.result, __ATOMIC_RELAXED) ==
             -ECANCELED);
      assert(__atomic_load_n(&fixture.rejection_writers,
                             __ATOMIC_RELAXED) == 1u);
      assert(__atomic_load_n(&fixture.image_reads, __ATOMIC_RELAXED) == 0u);
    }
}

static void test_restored_staged_intent(void)
{
  /* A reboot leaves JOB_EMPTY while the durable staged/reboot record lives. */
  assert(bkvoice_ota_cancel_disposition(BKVOICE_OTA_JOB_EMPTY,
           -ENOENT, 0, false, true) == BKVOICE_OTA_CANCEL_PRESERVE);
  assert(bkvoice_ota_cancel_disposition(BKVOICE_OTA_JOB_QUEUED,
           -ENOENT, -EINPROGRESS, false, true) ==
           BKVOICE_OTA_CANCEL_PRESERVE);
}

int main(void)
{
  test_restored_staged_intent();
  test_queued_and_applying_cancel();
  test_completed_apply_races();
  test_committed_states_keep_intent();
  test_failed_apply_can_be_cleared();
  test_target_persistence_boundary();
  test_target_admission_publication();
  test_cancel_publish_race();
  puts("bk7258 voice OTA cancel policy tests passed");
  return 0;
}
