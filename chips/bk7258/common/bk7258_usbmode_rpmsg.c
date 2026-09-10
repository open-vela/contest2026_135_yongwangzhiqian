/****************************************************************************
 * chips/bk7258/common/bk7258_usbmode_rpmsg.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned CP-to-AP control plane for the BK7258 USB mode manager.  RPMsg
 * callbacks only validate/copy fixed-size messages and wake task context;
 * the potentially blocking USB disconnect/re-enumeration runs on AP worker.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_USBMODE_RPMSG

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/rpmsg/rpmsg.h>
#ifdef CONFIG_SMP
#  include <nuttx/sched.h>
#endif
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>

#include <arch/chip/bk7258_usbmode.h>
#include <arch/chip/bk7258_usbmode_rpmsg.h>

#define BK7258_USBMODE_RPMSG_ENDPOINT       "bk7258-usbmode-v1"
#define BK7258_USBMODE_RPMSG_MAGIC          0x31535542u /* "BUS1" */
#define BK7258_USBMODE_RPMSG_VERSION        1u
#define BK7258_USBMODE_RPMSG_ENDPOINT_MS    5000u
#define BK7258_USBMODE_RPMSG_SEND_MS        1000u

enum bk7258_usbmode_rpmsg_command_e
{
  BK7258_USBMODE_RPMSG_GET = 1,
  BK7258_USBMODE_RPMSG_SET,
  BK7258_USBMODE_RPMSG_REPLY,
};

struct bk7258_usbmode_rpmsg_message_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t status;
  uint32_t mode;
};

struct bk7258_usbmode_rpmsg_dev_s
{
  struct rpmsg_endpoint endpoint;
  mutex_t init_lock;
  mutex_t endpoint_lock;
  volatile bool initialized;
  volatile bool endpoint_created;
  volatile int connection_error;
#ifdef CONFIG_BK7258_AP_CORE
  spinlock_t request_lock;
  sem_t request_sem;
  bool request_active;
  struct bk7258_usbmode_rpmsg_message_s request;
#else
  mutex_t request_lock;
  spinlock_t reply_lock;
  sem_t reply_sem;
  bool reply_valid;
  uint32_t session;
  uint32_t sequence;
  uint32_t waiting_sequence;
  struct bk7258_usbmode_rpmsg_message_s reply;
#endif
};

static_assert(sizeof(struct bk7258_usbmode_rpmsg_message_s) == 24u,
              "BK7258 USB mode RPMsg ABI changed");

static struct bk7258_usbmode_rpmsg_dev_s g_bk7258_usbmode_rpmsg =
{
  .init_lock = NXMUTEX_INITIALIZER,
  .endpoint_lock = NXMUTEX_INITIALIZER,
#ifdef CONFIG_BK7258_AP_CORE
  .request_lock = SP_UNLOCKED,
#else
  .request_lock = NXMUTEX_INITIALIZER,
  .reply_lock = SP_UNLOCKED,
#endif
};

#ifdef CONFIG_BK7258_AP_CORE
static bool bk7258_usbmode_rpmsg_mode_valid(uint32_t mode, bool set)
{
  if (!set)
    {
      return mode == BK7258_USBMODE_NONE;
    }

  return mode == BK7258_USBMODE_CDC || mode == BK7258_USBMODE_MSC;
}

static bool bk7258_usbmode_rpmsg_request_valid(
  const struct bk7258_usbmode_rpmsg_message_s *message)
{
  return message->magic == BK7258_USBMODE_RPMSG_MAGIC &&
         message->version == BK7258_USBMODE_RPMSG_VERSION &&
         message->session != 0u && message->sequence != 0u &&
         message->status == 0 &&
         ((message->command == BK7258_USBMODE_RPMSG_GET &&
           bk7258_usbmode_rpmsg_mode_valid(message->mode, false)) ||
         (message->command == BK7258_USBMODE_RPMSG_SET &&
           bk7258_usbmode_rpmsg_mode_valid(message->mode, true)));
}
#endif

static int bk7258_usbmode_rpmsg_send(
  struct bk7258_usbmode_rpmsg_dev_s *priv,
  const struct bk7258_usbmode_rpmsg_message_s *message)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_USBMODE_RPMSG_SEND_MS);
  int ret;

  do
    {
      ret = nxmutex_lock(&priv->endpoint_lock);
      if (ret < 0)
        {
          return ret;
        }

      if (!__atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) ||
          !is_rpmsg_ept_ready(&priv->endpoint))
        {
          ret = -ENOTCONN;
        }
      else
        {
          ret = rpmsg_trysend(&priv->endpoint, message, sizeof(*message));
        }

      nxmutex_unlock(&priv->endpoint_lock);
      if (ret >= 0)
        {
          return 0;
        }

      if (ret != -ENOMEM && ret != -EAGAIN)
        {
          return ret;
        }

      (void)nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  return -ETIMEDOUT;
}

#ifdef CONFIG_BK7258_AP_CORE

static void bk7258_usbmode_rpmsg_make_reply(
  struct bk7258_usbmode_rpmsg_message_s *reply,
  const struct bk7258_usbmode_rpmsg_message_s *request, int status)
{
  memcpy(reply, request, sizeof(*reply));
  reply->command = BK7258_USBMODE_RPMSG_REPLY;
  reply->status = status;
  reply->mode = bk7258_usbmode_get();
}

static int bk7258_usbmode_rpmsg_worker(int argc, char **argv)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = &g_bk7258_usbmode_rpmsg;

  (void)argc;
  (void)argv;

  for (; ; )
    {
      struct bk7258_usbmode_rpmsg_message_s request;
      struct bk7258_usbmode_rpmsg_message_s reply;
      irqstate_t flags;
      int ret;

      if (nxsem_wait_uninterruptible(&priv->request_sem) < 0)
        {
          continue;
        }

      flags = spin_lock_irqsave(&priv->request_lock);
      memcpy(&request, &priv->request, sizeof(request));
      spin_unlock_irqrestore(&priv->request_lock, flags);

      ret = 0;
      if (request.command == BK7258_USBMODE_RPMSG_SET)
        {
          ret = bk7258_usbmode_set(
                  (enum bk7258_usbmode_e)request.mode);
        }

      bk7258_usbmode_rpmsg_make_reply(&reply, &request, ret);

      flags = spin_lock_irqsave(&priv->request_lock);
      priv->request_active = false;
      spin_unlock_irqrestore(&priv->request_lock, flags);
      (void)bk7258_usbmode_rpmsg_send(priv, &reply);
    }

  return 0;
}

static int bk7258_usbmode_rpmsg_server_cb(
  struct rpmsg_endpoint *endpoint, void *data, size_t len, uint32_t src,
  void *arg)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = arg;
  const struct bk7258_usbmode_rpmsg_message_s *request = data;
  irqstate_t flags;
  int ret;

  (void)endpoint;
  (void)src;

  if (request == NULL || len != sizeof(*request) ||
      !bk7258_usbmode_rpmsg_request_valid(request))
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&priv->request_lock);
  if (priv->request_active)
    {
      spin_unlock_irqrestore(&priv->request_lock, flags);
      return -EBUSY;
    }

  memcpy(&priv->request, request, sizeof(*request));
  priv->request_active = true;
  spin_unlock_irqrestore(&priv->request_lock, flags);

  ret = nxsem_post(&priv->request_sem);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&priv->request_lock);
      priv->request_active = false;
      spin_unlock_irqrestore(&priv->request_lock, flags);
    }

  return ret;
}

static bool bk7258_usbmode_rpmsg_ns_match(
  struct rpmsg_device *rdev, void *arg, const char *name, uint32_t dest)
{
  const char *cpuname = rpmsg_get_cpuname(rdev);

  (void)arg;
  (void)dest;
  return cpuname != NULL && strcmp(cpuname, "cp") == 0 &&
         strcmp(name, BK7258_USBMODE_RPMSG_ENDPOINT) == 0;
}

static void bk7258_usbmode_rpmsg_ns_bind(
  struct rpmsg_device *rdev, void *arg, const char *name, uint32_t dest)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = arg;
  int ret;

  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret < 0)
    {
      return;
    }

  if (!__atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE))
    {
      priv->endpoint.priv = priv;
      ret = rpmsg_create_ept(&priv->endpoint, rdev, name,
                             RPMSG_ADDR_ANY, dest,
                             bk7258_usbmode_rpmsg_server_cb, NULL);
      __atomic_store_n(&priv->connection_error, ret, __ATOMIC_RELEASE);
      if (ret >= 0)
        {
          __atomic_store_n(&priv->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&priv->endpoint_lock);
}

#else

static void bk7258_usbmode_rpmsg_flush(sem_t *sem)
{
  while (nxsem_trywait(sem) == 0)
    {
    }
}

static bool bk7258_usbmode_rpmsg_endpoint_ready(
  struct bk7258_usbmode_rpmsg_dev_s *priv)
{
  bool ready = false;

  if (nxmutex_lock(&priv->endpoint_lock) >= 0)
    {
      ready = __atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE) &&
              is_rpmsg_ept_ready(&priv->endpoint);
      nxmutex_unlock(&priv->endpoint_lock);
    }

  return ready;
}

static int bk7258_usbmode_rpmsg_wait_endpoint(
  struct bk7258_usbmode_rpmsg_dev_s *priv)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(BK7258_USBMODE_RPMSG_ENDPOINT_MS);
  int ret;

  do
    {
      if (bk7258_usbmode_rpmsg_endpoint_ready(priv))
        {
          return 0;
        }

      (void)nxsig_usleep(1000);
    }
  while ((clock_t)(clock_systime_ticks() - start) < limit);

  ret = __atomic_load_n(&priv->connection_error, __ATOMIC_ACQUIRE);
  return ret < 0 ? ret : -ETIMEDOUT;
}

static int bk7258_usbmode_rpmsg_client_cb(
  struct rpmsg_endpoint *endpoint, void *data, size_t len, uint32_t src,
  void *arg)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = arg;
  const struct bk7258_usbmode_rpmsg_message_s *reply = data;
  irqstate_t flags;
  bool matched;

  (void)endpoint;
  (void)src;

  if (reply == NULL || len != sizeof(*reply) ||
      reply->magic != BK7258_USBMODE_RPMSG_MAGIC ||
      reply->version != BK7258_USBMODE_RPMSG_VERSION ||
      reply->command != BK7258_USBMODE_RPMSG_REPLY ||
      reply->session != priv->session || reply->sequence == 0u ||
      reply->mode > BK7258_USBMODE_MSC)
    {
      return -ENOMSG;
    }

  flags = spin_lock_irqsave(&priv->reply_lock);
  matched = priv->waiting_sequence != 0u &&
            reply->sequence == priv->waiting_sequence;
  if (matched)
    {
      memcpy(&priv->reply, reply, sizeof(priv->reply));
      priv->reply_valid = true;
    }

  spin_unlock_irqrestore(&priv->reply_lock, flags);
  return matched ? nxsem_post(&priv->reply_sem) : -ENOMSG;
}

static void bk7258_usbmode_rpmsg_device_created(
  struct rpmsg_device *rdev, void *arg)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = arg;
  const char *cpuname = rpmsg_get_cpuname(rdev);
  int ret;

  if (cpuname == NULL || strcmp(cpuname, "ap") != 0)
    {
      return;
    }

  ret = nxmutex_lock(&priv->endpoint_lock);
  if (ret < 0)
    {
      __atomic_store_n(&priv->connection_error, ret, __ATOMIC_RELEASE);
      return;
    }

  if (!__atomic_load_n(&priv->endpoint_created, __ATOMIC_ACQUIRE))
    {
      priv->endpoint.priv = priv;
      ret = rpmsg_create_ept(&priv->endpoint, rdev,
                             BK7258_USBMODE_RPMSG_ENDPOINT,
                             RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                             bk7258_usbmode_rpmsg_client_cb, NULL);
      __atomic_store_n(&priv->connection_error, ret, __ATOMIC_RELEASE);
      if (ret >= 0)
        {
          __atomic_store_n(&priv->endpoint_created, true,
                           __ATOMIC_RELEASE);
        }
    }

  nxmutex_unlock(&priv->endpoint_lock);
}

static int bk7258_usbmode_rpmsg_wait_reply(
  struct bk7258_usbmode_rpmsg_dev_s *priv, uint32_t sequence,
  struct bk7258_usbmode_rpmsg_message_s *reply, uint32_t timeout_ms)
{
  clock_t start = clock_systime_ticks();
  clock_t limit = MSEC2TICK(timeout_ms);

  for (; ; )
    {
      irqstate_t flags;
      clock_t elapsed;
      bool valid;
      int ret;

      flags = spin_lock_irqsave(&priv->reply_lock);
      valid = priv->reply_valid && priv->reply.sequence == sequence;
      if (valid)
        {
          memcpy(reply, &priv->reply, sizeof(*reply));
          priv->reply_valid = false;
        }

      spin_unlock_irqrestore(&priv->reply_lock, flags);
      if (valid)
        {
          return 0;
        }

      ret = __atomic_load_n(&priv->connection_error, __ATOMIC_ACQUIRE);
      if (ret < 0)
        {
          return ret;
        }

      elapsed = clock_systime_ticks() - start;
      if (elapsed >= limit)
        {
          return -ETIMEDOUT;
        }

      ret = nxsem_tickwait_uninterruptible(&priv->reply_sem,
                                           limit - elapsed);
      if (ret < 0)
        {
          return ret;
        }
    }
}

static int bk7258_usbmode_rpmsg_request(
  enum bk7258_usbmode_rpmsg_command_e command,
  enum bk7258_usbmode_e requested, enum bk7258_usbmode_e *actual,
  uint32_t timeout_ms)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = &g_bk7258_usbmode_rpmsg;
  struct bk7258_usbmode_rpmsg_message_s request;
  struct bk7258_usbmode_rpmsg_message_s reply = {0};
  irqstate_t flags;
  int ret = -EIO;

  if (actual == NULL || timeout_ms == 0u ||
      (command == BK7258_USBMODE_RPMSG_SET &&
       requested != BK7258_USBMODE_CDC &&
       requested != BK7258_USBMODE_MSC))
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->request_lock);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_usbmode_rpmsg_wait_endpoint(priv);
  if (ret < 0)
    {
      goto out;
    }

  bk7258_usbmode_rpmsg_flush(&priv->reply_sem);
  memset(&request, 0, sizeof(request));
  request.magic = BK7258_USBMODE_RPMSG_MAGIC;
  request.version = BK7258_USBMODE_RPMSG_VERSION;
  request.command = command;
  request.session = priv->session;
  priv->sequence++;
  if (priv->sequence == 0u)
    {
      priv->sequence++;
    }

  request.sequence = priv->sequence;
  request.mode = requested;

  flags = spin_lock_irqsave(&priv->reply_lock);
  priv->waiting_sequence = request.sequence;
  priv->reply_valid = false;
  spin_unlock_irqrestore(&priv->reply_lock, flags);

  ret = bk7258_usbmode_rpmsg_send(priv, &request);
  if (ret >= 0)
    {
      ret = bk7258_usbmode_rpmsg_wait_reply(priv, request.sequence,
                                            &reply, timeout_ms);
    }

  flags = spin_lock_irqsave(&priv->reply_lock);
  priv->waiting_sequence = 0u;
  spin_unlock_irqrestore(&priv->reply_lock, flags);
  if (ret >= 0)
    {
      *actual = (enum bk7258_usbmode_e)reply.mode;
      ret = reply.status;
    }

out:
  nxmutex_unlock(&priv->request_lock);
  return ret;
}

int bk7258_usbmode_rpmsg_get(enum bk7258_usbmode_e *mode,
                             uint32_t timeout_ms)
{
  return bk7258_usbmode_rpmsg_request(BK7258_USBMODE_RPMSG_GET,
                                      BK7258_USBMODE_NONE, mode,
                                      timeout_ms);
}

int bk7258_usbmode_rpmsg_set(enum bk7258_usbmode_e mode,
                             enum bk7258_usbmode_e *actual,
                             uint32_t timeout_ms)
{
  return bk7258_usbmode_rpmsg_request(BK7258_USBMODE_RPMSG_SET, mode,
                                      actual, timeout_ms);
}

#endif

static void bk7258_usbmode_rpmsg_device_destroy(
  struct rpmsg_device *rdev, void *arg)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = arg;
  const char *cpuname = rpmsg_get_cpuname(rdev);

#ifdef CONFIG_BK7258_AP_CORE
  const char *remote = "cp";
#else
  const char *remote = "ap";
  irqstate_t flags;
#endif

  if (cpuname == NULL || strcmp(cpuname, remote) != 0)
    {
      return;
    }

  __atomic_store_n(&priv->endpoint_created, false, __ATOMIC_RELEASE);
  __atomic_store_n(&priv->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
  if (nxmutex_lock(&priv->endpoint_lock) >= 0)
    {
      if (priv->endpoint.rdev != NULL)
        {
          rpmsg_destroy_ept(&priv->endpoint);
        }

      memset(&priv->endpoint, 0, sizeof(priv->endpoint));
      nxmutex_unlock(&priv->endpoint_lock);
    }

#ifndef CONFIG_BK7258_AP_CORE
  flags = spin_lock_irqsave(&priv->reply_lock);
  priv->reply_valid = false;
  spin_unlock_irqrestore(&priv->reply_lock, flags);
  (void)nxsem_post(&priv->reply_sem);
#endif
}

int bk7258_usbmode_rpmsg_initialize(void)
{
  struct bk7258_usbmode_rpmsg_dev_s *priv = &g_bk7258_usbmode_rpmsg;
  bool semaphore_initialized = false;
  bool callback_registered = false;
  int ret;

  ret = nxmutex_lock(&priv->init_lock);
  if (ret < 0)
    {
      return ret;
    }

  if (__atomic_load_n(&priv->initialized, __ATOMIC_ACQUIRE))
    {
      nxmutex_unlock(&priv->init_lock);
      return 0;
    }

  __atomic_store_n(&priv->connection_error, -ENOTCONN, __ATOMIC_RELEASE);
#ifdef CONFIG_BK7258_AP_CORE
  ret = nxsem_init(&priv->request_sem, 0, 0);
#else
  ret = nxsem_init(&priv->reply_sem, 0, 0);
  priv->session = (uint32_t)clock_systime_ticks() ^
                  (uint32_t)(uintptr_t)priv;
  if (priv->session == 0u)
    {
      priv->session = 1u;
    }
#endif
  semaphore_initialized = ret >= 0;

#ifdef CONFIG_PRIORITY_INHERITANCE
  if (ret >= 0)
    {
#ifdef CONFIG_BK7258_AP_CORE
      ret = nxsem_set_protocol(&priv->request_sem, SEM_PRIO_NONE);
#else
      ret = nxsem_set_protocol(&priv->reply_sem, SEM_PRIO_NONE);
#endif
    }
#endif

  if (ret >= 0)
    {
      ret = rpmsg_register_callback(priv,
#ifdef CONFIG_BK7258_AP_CORE
                                    NULL,
#else
                                    bk7258_usbmode_rpmsg_device_created,
#endif
                                    bk7258_usbmode_rpmsg_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
                                    bk7258_usbmode_rpmsg_ns_match,
                                    bk7258_usbmode_rpmsg_ns_bind);
#else
                                    NULL, NULL);
#endif
      callback_registered = ret >= 0;
    }

#ifdef CONFIG_BK7258_AP_CORE
  if (ret >= 0)
    {
      pid_t pid;

      pid = kthread_create("bk7258-usbmode",
                           CONFIG_BK7258_USBMODE_RPMSG_PRIORITY,
                           CONFIG_BK7258_USBMODE_RPMSG_STACKSIZE,
                           bk7258_usbmode_rpmsg_worker, NULL);
      ret = (int)pid;
      if (pid >= 0)
        {
#ifdef CONFIG_SMP
          cpu_set_t cpuset;

          /* CherryUSB controller lifecycle and the SDK IRQ-routing bridge are
           * initialized from AP logical CPU0.  Keep later disconnect and
           * re-enumeration on that same owner: allowing this worker to migrate
           * to CPU1 can make a controller teardown cross a core-local IRQ
           * lifecycle while the caller is itself on the mirrored core.
           */

          CPU_ZERO(&cpuset);
          CPU_SET(0, &cpuset);
          ret = sched_setaffinity(pid, sizeof(cpuset), &cpuset);
          if (ret < 0)
            {
              kthread_delete(pid);
            }
#else
          ret = 0;
#endif
        }
    }
#endif

  if (ret >= 0)
    {
      __atomic_store_n(&priv->initialized, true, __ATOMIC_RELEASE);
#ifdef CONFIG_BK7258_AP_CORE
      syslog(LOG_INFO, "BK7258 USBMODE RPC READY endpoint=%s\n",
             BK7258_USBMODE_RPMSG_ENDPOINT);
#endif
    }
  else
    {
      if (callback_registered)
        {
          rpmsg_unregister_callback(priv,
#ifdef CONFIG_BK7258_AP_CORE
                                    NULL,
#else
                                    bk7258_usbmode_rpmsg_device_created,
#endif
                                    bk7258_usbmode_rpmsg_device_destroy,
#ifdef CONFIG_BK7258_AP_CORE
                                    bk7258_usbmode_rpmsg_ns_match,
                                    bk7258_usbmode_rpmsg_ns_bind);
#else
                                    NULL, NULL);
#endif
        }

      if (semaphore_initialized)
        {
#ifdef CONFIG_BK7258_AP_CORE
          (void)nxsem_destroy(&priv->request_sem);
#else
          (void)nxsem_destroy(&priv->reply_sem);
#endif
        }
    }

  nxmutex_unlock(&priv->init_lock);
  return ret;
}

#endif /* CONFIG_BK7258_USBMODE_RPMSG */
