/****************************************************************************
 * app/bk7258/bk7258_voice_feedback.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Keep LCD rendering outside the serialized MIC/DAC state transitions.  The
 * worker intentionally applies the newest state: transient states may be
 * coalesced when the display is slower than the voice state machine.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_VOICE_SERVICE) && \
    defined(CONFIG_BK7258_DISPLAY_SERVICE)

#include "bk7258_display_service.h"
#include "bk7258_voice_feedback.h"

#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>

struct bkvoice_feedback_s
{
  mutex_t init_lock;
  sem_t pending_sem;
  volatile bool initialized;
  volatile uint32_t requested_state;
};

static struct bkvoice_feedback_s g_bkvoice_feedback =
{
  .init_lock = NXMUTEX_INITIALIZER,
};

static int bkvoice_feedback_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static const char *bkvoice_feedback_expression(
  enum bkvoice_turn_state_e state)
{
  switch (state)
    {
      case BKVOICE_TURN_IDLE:
        return "neutral";
      case BKVOICE_TURN_CAPTURING:
        return "listening";
      case BKVOICE_TURN_WAITING_TTS:
        return "thinking";
      case BKVOICE_TURN_PLAYING:
      case BKVOICE_TURN_DRAINING:
        return "speaking";
      case BKVOICE_TURN_FAULTED:
        return "error";
      default:
        return "error";
    }
}

static int bkvoice_feedback_worker(int argc, char **argv)
{
  struct bkvoice_feedback_s *feedback = &g_bkvoice_feedback;
  uint32_t applied_state = UINT32_MAX;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      enum bkvoice_turn_state_e state;
      uint32_t requested;
      int ret;

      if (nxsem_wait_uninterruptible(&feedback->pending_sem) < 0)
        {
          continue;
        }

      requested = __atomic_load_n(&feedback->requested_state,
                                  __ATOMIC_ACQUIRE);
      if (requested == applied_state)
        {
          continue;
        }

      state = (enum bkvoice_turn_state_e)requested;
      ret = bk7258_display_set_expression(
        bkvoice_feedback_expression(state));
      if (ret < 0)
        {
          syslog(LOG_WARNING,
                 "BKVOICE EYES state=%s ret=%d best_effort=1\n",
                 bkvoice_turn_state_name(state), ret);
          continue;
        }

      applied_state = requested;
    }

  return 0;
}

int bk7258_voice_feedback_start(void)
{
  struct bkvoice_feedback_s *feedback = &g_bkvoice_feedback;
  pid_t pid;
  bool semaphore_initialized = false;
  int ret;

  ret = nxmutex_lock(&feedback->init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&feedback->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&feedback->init_lock);
      return 0;
    }

  ret = nxsem_init(&feedback->pending_sem, 0, 0);
  if (ret >= 0)
    {
      semaphore_initialized = true;
    }
#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
      ret = nxsem_set_protocol(&feedback->pending_sem, SEM_PRIO_NONE);
    }
#endif

  if (ret >= 0)
    {
      __atomic_store_n(&feedback->requested_state,
                       (uint32_t)BKVOICE_TURN_IDLE, __ATOMIC_RELEASE);
      pid = task_create("bkvoice-eyes",
                        CONFIG_BK7258_DISPLAY_SERVICE_PRIORITY,
                        CONFIG_BK7258_DISPLAY_SERVICE_STACKSIZE,
                        bkvoice_feedback_worker, NULL);
      if (pid < 0)
        {
          ret = bkvoice_feedback_errno();
        }
    }

  if (ret >= 0)
    {
      __atomic_store_n(&feedback->initialized, true, __ATOMIC_RELEASE);
    }
  else
    {
      if (semaphore_initialized)
        {
          (void)nxsem_destroy(&feedback->pending_sem);
        }
    }

  nxmutex_unlock(&feedback->init_lock);
  return ret;
}

void bk7258_voice_feedback_report(void *context,
                                  enum bkvoice_turn_state_e state)
{
  struct bkvoice_feedback_s *feedback = &g_bkvoice_feedback;

  (void)context;
  if (!__atomic_load_n(&feedback->initialized, __ATOMIC_ACQUIRE))
    {
      return;
    }

  __atomic_store_n(&feedback->requested_state, (uint32_t)state,
                   __ATOMIC_RELEASE);
  if (nxsem_post(&feedback->pending_sem) < 0)
    {
      syslog(LOG_WARNING,
             "BKVOICE EYES enqueue state=%s ret=%d best_effort=1\n",
             bkvoice_turn_state_name(state), bkvoice_feedback_errno());
    }
}

#endif /* CONFIG_BK7258_VOICE_SERVICE && CONFIG_BK7258_DISPLAY_SERVICE */
