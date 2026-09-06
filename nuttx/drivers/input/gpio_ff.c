/****************************************************************************
 * drivers/input/gpio_ff.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * A bounded, GPIO-backed FF_RUMBLE lower half.  The output callback is kept
 * ISR-safe: watchdog expiry, explicit stop, and inhibit all force it low
 * before returning.  Power voting is deliberately confined to task context.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/bits.h>
#include <nuttx/clock.h>
#include <nuttx/input/ff.h>
#include <nuttx/input/gpio_ff.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/power/pm.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct gpio_ff_dev_s
{
  struct ff_lowerhalf_s lower;
  struct gpio_ff_config_s config;
  mutex_t power_lock;
  spinlock_t lock;
  sem_t stop_sem;
  struct work_s work;
  struct wdog_s watchdog;
  uint32_t generation;
  uint32_t duration_ms;
  clock_t stopped_at;
  bool stopped_once;
  bool uploaded;
  bool request;
  bool inflight;
  bool output_on;
  bool output_fault;
  bool inhibited;
  bool destroyed;
  bool pm_held;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void gpio_ff_worker(void *arg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void gpio_ff_drain_stop_sem(struct gpio_ff_dev_s *dev)
{
  while (nxsem_trywait(&dev->stop_sem) == OK)
    {
    }
}

/* The caller holds dev->lock with interrupts disabled. */

static int gpio_ff_stop_locked(struct gpio_ff_dev_s *dev, bool wake_worker)
{
  int ret = OK;

  dev->generation++;
  dev->request = false;
  (void)wd_cancel(&dev->watchdog);

  if (dev->output_on)
    {
      ret = dev->config.set_output(dev->config.arg, false);
      if (ret < 0)
        {
          dev->output_fault = true;
        }
      else
        {
          dev->output_on = false;
          dev->stopped_at = clock_systime_ticks();
          dev->stopped_once = true;
        }
    }

  if (wake_worker)
    {
      (void)nxsem_post(&dev->stop_sem);
    }

  return ret;
}

static void gpio_ff_timeout(wdparm_t arg)
{
  struct gpio_ff_dev_s *dev = (struct gpio_ff_dev_s *)(uintptr_t)arg;
  irqstate_t flags;

  flags = spin_lock_irqsave(&dev->lock);
  if (!dev->destroyed)
    {
      gpio_ff_stop_locked(dev, true);
    }

  spin_unlock_irqrestore(&dev->lock, flags);
}

static void gpio_ff_worker(void *arg)
{
  struct gpio_ff_dev_s *dev = arg;
  irqstate_t flags;
  uint32_t generation;
  uint32_t duration_ms;
#ifdef CONFIG_PM
  bool pm_held = false;
#endif
  int ret = OK;

  if (nxmutex_lock(&dev->power_lock) < 0)
    {
      flags = spin_lock_irqsave(&dev->lock);
      dev->request = false;
      dev->inflight = false;
      dev->generation++;
      (void)dev->config.set_output(dev->config.arg, false);
      spin_unlock_irqrestore(&dev->lock, flags);
      return;
    }

  flags = spin_lock_irqsave(&dev->lock);
  if (!dev->request || dev->inhibited || dev->destroyed)
    {
      dev->inflight = false;
      spin_unlock_irqrestore(&dev->lock, flags);
      nxmutex_unlock(&dev->power_lock);
      return;
    }

  dev->request = false;
  generation = dev->generation;
  duration_ms = dev->duration_ms;
  spin_unlock_irqrestore(&dev->lock, flags);

#ifdef CONFIG_PM
  pm_stay(PM_IDLE_DOMAIN, PM_NORMAL);
  pm_held = true;
  flags = spin_lock_irqsave(&dev->lock);
  dev->pm_held = true;
  spin_unlock_irqrestore(&dev->lock, flags);
#endif

  ret = dev->config.set_power(dev->config.arg, true);
  if (ret < 0)
    {
      flags = spin_lock_irqsave(&dev->lock);
      (void)dev->config.set_output(dev->config.arg, false);
      spin_unlock_irqrestore(&dev->lock, flags);
      goto power_off;
    }

  gpio_ff_drain_stop_sem(dev);
  flags = spin_lock_irqsave(&dev->lock);
  if (dev->generation != generation || dev->inhibited || dev->destroyed)
    {
      spin_unlock_irqrestore(&dev->lock, flags);
      goto power_off;
    }

  /* Treat an attempted enable as possibly active, even if the callback
   * fails part-way through.  The same checked stop path must prove it low.
   */

  dev->output_on = true;
  ret = dev->config.set_output(dev->config.arg, true);
  if (ret < 0)
    {
      (void)gpio_ff_stop_locked(dev, false);
      spin_unlock_irqrestore(&dev->lock, flags);
      goto power_off;
    }

  ret = wd_start(&dev->watchdog, MSEC2TICK(duration_ms), gpio_ff_timeout,
                 (wdparm_t)(uintptr_t)dev);
  if (ret < 0)
    {
      (void)gpio_ff_stop_locked(dev, false);
      spin_unlock_irqrestore(&dev->lock, flags);
      goto power_off;
    }

  spin_unlock_irqrestore(&dev->lock, flags);

  (void)nxsem_tickwait_uninterruptible(&dev->stop_sem,
                                       MSEC2TICK(duration_ms));

  flags = spin_lock_irqsave(&dev->lock);
  if (dev->generation == generation && dev->output_on)
    {
      (void)gpio_ff_stop_locked(dev, false);
    }

  spin_unlock_irqrestore(&dev->lock, flags);

power_off:
  /* Every route above has either not raised the output or has attempted
   * to lower it under the spinlock.  A failed shutdown latches output_fault
   * and blocks further pulses.  Always release this driver's power vote.
   */

  (void)dev->config.set_power(dev->config.arg, false);
#ifdef CONFIG_PM
  if (pm_held)
    {
      flags = spin_lock_irqsave(&dev->lock);
      dev->pm_held = false;
      spin_unlock_irqrestore(&dev->lock, flags);
      pm_relax(PM_IDLE_DOMAIN, PM_NORMAL);
    }
#endif

  flags = spin_lock_irqsave(&dev->lock);
  dev->inflight = false;
  spin_unlock_irqrestore(&dev->lock, flags);

  nxmutex_unlock(&dev->power_lock);
}

static int gpio_ff_upload(struct ff_lowerhalf_s *lower,
                          struct ff_effect *effect,
                          struct ff_effect *old)
{
  struct gpio_ff_dev_s *dev = (struct gpio_ff_dev_s *)lower;
  irqstate_t flags;

  (void)old;
  if (effect == NULL || effect->type != FF_RUMBLE || effect->direction != 0 ||
      effect->trigger.button != 0 || effect->trigger.interval != 0 ||
      effect->replay.delay != 0 || effect->replay.length == 0 ||
      effect->replay.length > dev->config.max_on_ms ||
      (effect->u.rumble.strong_magnitude == 0 &&
       effect->u.rumble.weak_magnitude == 0))
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&dev->lock);
  if (dev->destroyed)
    {
      spin_unlock_irqrestore(&dev->lock, flags);
      return -ENODEV;
    }

  if (dev->inflight || dev->output_on)
    {
      spin_unlock_irqrestore(&dev->lock, flags);
      return -EBUSY;
    }

  dev->duration_ms = effect->replay.length;
  dev->uploaded = true;
  spin_unlock_irqrestore(&dev->lock, flags);
  return OK;
}

static int gpio_ff_erase(struct ff_lowerhalf_s *lower, int effect_id)
{
  struct gpio_ff_dev_s *dev = (struct gpio_ff_dev_s *)lower;
  irqstate_t flags;
  int ret;

  if (effect_id != 0)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&dev->lock);
  ret = gpio_ff_stop_locked(dev, true);
  dev->uploaded = false;
  spin_unlock_irqrestore(&dev->lock, flags);
  return ret;
}

static int gpio_ff_playback(struct ff_lowerhalf_s *lower, int effect_id,
                            int value)
{
  struct gpio_ff_dev_s *dev = (struct gpio_ff_dev_s *)lower;
  irqstate_t flags;
  int ret = OK;

  if (effect_id != 0 || (value != 0 && value != 1))
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&dev->lock);
  if (value == 0)
    {
      ret = gpio_ff_stop_locked(dev, true);
    }
  else if (dev->destroyed)
    {
      ret = -ENODEV;
    }
  else if (dev->inhibited)
    {
      ret = -EPERM;
    }
  else if (dev->output_fault)
    {
      ret = -EIO;
    }
  else if (!dev->uploaded)
    {
      ret = -ENOENT;
    }
  else if (dev->inflight || dev->output_on ||
           (dev->stopped_once &&
            (clock_t)(clock_systime_ticks() - dev->stopped_at) <
            MSEC2TICK(dev->config.min_off_ms)))
    {
      ret = -EBUSY;
    }
  else
    {
      dev->request = true;
      dev->inflight = true;
      dev->generation++;
    }

  spin_unlock_irqrestore(&dev->lock, flags);
  if (ret == OK && value == 1 &&
      work_queue(LPWORK, &dev->work, gpio_ff_worker, dev, 0) < 0)
    {
      flags = spin_lock_irqsave(&dev->lock);
      dev->request = false;
      dev->inflight = false;
      dev->generation++;
      (void)dev->config.set_output(dev->config.arg, false);
      spin_unlock_irqrestore(&dev->lock, flags);
      ret = -EIO;
    }

  return ret;
}

static void gpio_ff_destroy(struct ff_lowerhalf_s *lower)
{
  struct gpio_ff_dev_s *dev = (struct gpio_ff_dev_s *)lower;
  irqstate_t flags;
#ifdef CONFIG_PM
  bool pm_held;
#endif

  flags = spin_lock_irqsave(&dev->lock);
  dev->destroyed = true;
  dev->inhibited = true;
  gpio_ff_stop_locked(dev, true);
  spin_unlock_irqrestore(&dev->lock, flags);

  (void)work_cancel_sync(LPWORK, &dev->work);
  (void)wd_cancel(&dev->watchdog);
  (void)nxmutex_lock(&dev->power_lock);
  (void)dev->config.set_output(dev->config.arg, false);
  (void)dev->config.set_power(dev->config.arg, false);

#ifdef CONFIG_PM
  flags = spin_lock_irqsave(&dev->lock);
  pm_held = dev->pm_held;
  dev->pm_held = false;
  spin_unlock_irqrestore(&dev->lock, flags);
  if (pm_held)
    {
      pm_relax(PM_IDLE_DOMAIN, PM_NORMAL);
    }
#endif

  nxmutex_unlock(&dev->power_lock);
  nxsem_destroy(&dev->stop_sem);
  nxmutex_destroy(&dev->power_lock);
  kmm_free(dev);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int gpio_ff_register(const char *path, const struct gpio_ff_config_s *config,
                     struct ff_lowerhalf_s **lower_out)
{
  struct gpio_ff_dev_s *dev;
  int ret;

  if (lower_out != NULL)
    {
      *lower_out = NULL;
    }

  if (path == NULL || config == NULL || lower_out == NULL ||
      config->set_output == NULL || config->set_power == NULL ||
      config->max_on_ms == 0 || config->max_on_ms > 32767u ||
      config->min_off_ms > 32767u)
    {
      return -EINVAL;
    }

  dev = kmm_zalloc(sizeof(*dev));
  if (dev == NULL)
    {
      return -ENOMEM;
    }

  dev->config = *config;
  ret = nxmutex_init(&dev->power_lock);
  if (ret < 0)
    {
      kmm_free(dev);
      return ret;
    }

  ret = nxsem_init(&dev->stop_sem, 0, 0);
  if (ret < 0)
    {
      nxmutex_destroy(&dev->power_lock);
      kmm_free(dev);
      return ret;
    }

  wd_init(&dev->watchdog);
  dev->lower.upload = gpio_ff_upload;
  dev->lower.erase = gpio_ff_erase;
  dev->lower.playback = gpio_ff_playback;
  dev->lower.destroy = gpio_ff_destroy;
  set_bit(FF_RUMBLE, dev->lower.ffbit);

  ret = dev->config.set_output(dev->config.arg, false);
  if (ret >= 0)
    {
      ret = ff_register(&dev->lower, path, 1);
    }

  if (ret < 0)
    {
      (void)dev->config.set_output(dev->config.arg, false);
      nxsem_destroy(&dev->stop_sem);
      nxmutex_destroy(&dev->power_lock);
      kmm_free(dev);
      return ret;
    }

  *lower_out = &dev->lower;
  return OK;
}

int gpio_ff_inhibit(struct ff_lowerhalf_s *lower, bool inhibited)
{
  struct gpio_ff_dev_s *dev = (struct gpio_ff_dev_s *)lower;
  irqstate_t flags;
  int stop_ret = OK;
  int ret;

  if (dev == NULL)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&dev->lock);
  if (dev->destroyed)
    {
      spin_unlock_irqrestore(&dev->lock, flags);
      return -ENODEV;
    }

  dev->inhibited = inhibited;
  if (inhibited)
    {
      stop_ret = gpio_ff_stop_locked(dev, true);
    }

  spin_unlock_irqrestore(&dev->lock, flags);
  if (!inhibited)
    {
      return stop_ret;
    }

  /* This is a task-context API.  Waiting for the LPWORK owner closes the
   * power vote before reporting a successful inhibit.
   */

  ret = nxmutex_lock(&dev->power_lock);
  if (ret < 0)
    {
      return ret;
    }

  nxmutex_unlock(&dev->power_lock);
  return stop_ret;
}
