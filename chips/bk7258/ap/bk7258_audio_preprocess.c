/****************************************************************************
 * chips/bk7258/ap/bk7258_audio_preprocess.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * AP-side adapter for the pinned v3.1.1.9 AEC v3 and AGC libraries.  The SDK
 * profile excludes legacy libaec.a so the raw aec_* ABI has one deterministic
 * owner: libaec_v3.a, the backend used by the maintained SDK voice service.
 ****************************************************************************/

#include <nuttx/config.h>

#if defined(CONFIG_BK7258_AUDIO_PREPROCESS) && \
    defined(CONFIG_BK7258_AP_CORE)

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>

#include <arch/chip/bk7258_audio_preprocess.h>

#include <common/bk_err.h>
#include <components/system.h>
#include <modules/aec_v3.h>
#include <os/mem.h>
#include <os/os.h>

#define BK7258_AGC_MIN_LEVEL          0
#define BK7258_AGC_MAX_LEVEL          255
#define BK7258_AGC_TARGET_DBFS        3
#define BK7258_AGC_COMPRESSION_DB     9
#define BK7258_AGC_LIMITER_ENABLE     1u
#define BK7258_AUDIO_WORK_BUFFERS     4u
#define BK7258_AEC_DELAY_CAPACITY_SAMPLES 1000u
#define BK7258_SDK_FRAME_SAMPLES_10MS 160u

/* Hardware isolation proved that the separate legacy NS wrapper faults on its
 * second consecutive 160-sample call and that the VAD wrapper also faults.
 * Keep those obsolete singleton wrappers disabled.  The maintained AEC v3
 * integrated NS is independently gated because its current board throughput
 * must be proven against the 20-ms capture deadline.
 */

#define BK7258_SDK_NS_ENABLED          0
#define BK7258_SDK_VAD_ENABLED         0

#define BK7258_AEC_FLAGS_REQUIRED ((uint32_t)AEC_EC_FLAG_MSK)

#ifdef CONFIG_BK7258_AUDIO_PREPROCESS_POSTFILTERS
#  define BK7258_AEC_FLAGS_POSTFILTERS \
  ((uint32_t)(AEC_BPF_FLAG_MSK | AEC_DRC_FLAG_MSK | AEC_CNI_FLAG_MSK))
#  define BK7258_AEC_POSTFILTERS_ENABLED 1u
#else
#  define BK7258_AEC_FLAGS_POSTFILTERS 0u
#  define BK7258_AEC_POSTFILTERS_ENABLED 0u
#endif

#ifdef CONFIG_BK7258_AUDIO_PREPROCESS_INTEGRATED_NS
#  define BK7258_AEC_FLAGS_NS ((uint32_t)AEC_NS_FLAG_MSK)
#  define BK7258_AEC_INTEGRATED_NS_ENABLED 1u
#else
#  define BK7258_AEC_FLAGS_NS 0u
#  define BK7258_AEC_INTEGRATED_NS_ENABLED 0u
#endif

#define BK7258_AEC_FLAGS \
  (BK7258_AEC_FLAGS_REQUIRED | BK7258_AEC_FLAGS_POSTFILTERS | \
   BK7258_AEC_FLAGS_NS)

_Static_assert(BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES ==
               2u * BK7258_SDK_FRAME_SAMPLES_10MS,
               "16-kHz preprocessing must contain two 10-ms SDK frames");

/* libaec/libaud_ns/libaud_vad do not call the public OS adapter symbols
 * directly.  They first fetch this pinned v3.1.1.9 function table from
 * libaudio_osi.a.  The official media_service initializes the table before
 * constructing its pipeline; this NuttX lower-half uses the algorithms
 * directly, so it must establish the same process-lifetime root itself.
 */

struct bk7258_audio_osi_funcs_s
{
  FAR void *(*psram_malloc)(size_t size);
  FAR void *(*psram_realloc)(FAR void *old_mem, size_t size);
  FAR void *(*malloc)(size_t size);
  FAR void *(*zalloc)(size_t num, size_t size);
  FAR void *(*realloc)(FAR void *old_mem, size_t size);
  void (*free)(FAR void *ptr);
  FAR void *(*memcpy)(FAR void *out, FAR const void *in, uint32_t n);
  void (*memcpy_word)(FAR void *out, FAR const void *in, uint32_t n);
  FAR void *(*memset)(FAR void *buffer, int value, uint32_t n);
  FAR void *(*memmove)(FAR void *out, FAR const void *in, uint32_t n);
  void (*memset_word)(FAR void *buffer, int32_t value, uint32_t n);
  void (*log_write)(int level, FAR char *tag, FAR const char *format, ...);
  void (*osi_assert)(uint8_t expression, FAR char *expression_text,
                     FAR const char *function);
  uint32_t (*get_time)(void);
};

_Static_assert(sizeof(struct bk7258_audio_osi_funcs_s) ==
               14u * sizeof(void *),
               "v3.1.1.9 audio OSI table ABI changed");

struct bk7258_agc_config_sdk_s
{
  int16_t target_level_dbfs;
  int16_t compression_gain_db;
  uint8_t limiter_enable;
};

struct bk7258_audio_preprocess_s
{
  mutex_t lock;
  FAR AECContext *aec;
  FAR void *agc;
  FAR int16_t *work;
  FAR int16_t *near;
  FAR int16_t *reference;
  FAR int16_t *aec_output;
  FAR int16_t *agc_output;
  bool ns_ready;
  bool vad_ready;
  bool ready;
  struct bk7258_audio_preprocess_diag_s diag;
};

extern int bk_aud_agc_create(FAR void **instance);
extern int bk_aud_agc_free(FAR void *instance);
extern int bk_aud_agc_init(FAR void *instance, int32_t min_level,
                           int32_t max_level, uint32_t sample_rate);
extern int bk_aud_agc_set_config(
  FAR void *instance, struct bk7258_agc_config_sdk_s config);
extern int bk_aud_agc_process(FAR void *instance, FAR const int16_t *input,
                              int16_t samples, FAR int16_t *output);
#if BK7258_SDK_NS_ENABLED
extern int bk_aud_ns_init(int frame_size_20ms, int sample_rate);
extern int bk_aud_ns_deinit(void);
extern int bk_aud_ns_process(FAR int16_t *input);
#endif
#if BK7258_SDK_VAD_ENABLED
extern int bk_aud_vad_init(int frame_size_20ms, int sample_rate);
extern int bk_aud_vad_deinit(void);
extern int bk_aud_vad_process(FAR int16_t *input);
#endif

extern bk_err_t audio_osi_funcs_init(FAR void *config);
extern FAR void *bk_get_audio_osi_funcs(void);

_Static_assert(sizeof(struct bk7258_agc_config_sdk_s) == 6,
               "v3.1.1.9 AGC config ABI changed");

static FAR void *bk7258_audio_osi_psram_malloc(size_t size)
{
  return psram_malloc(size);
}

static FAR void *bk7258_audio_osi_psram_realloc(FAR void *old_mem,
                                                size_t size)
{
  return bk_psram_realloc(old_mem, size);
}

static FAR void *bk7258_audio_osi_malloc(size_t size)
{
  return os_malloc(size);
}

static FAR void *bk7258_audio_osi_zalloc(size_t num, size_t size)
{
  if (size != 0 && num > SIZE_MAX / size)
    {
      return NULL;
    }

  return os_zalloc(num * size);
}

static FAR void *bk7258_audio_osi_realloc(FAR void *old_mem, size_t size)
{
  return os_realloc(old_mem, size);
}

static void bk7258_audio_osi_free(FAR void *ptr)
{
  os_free(ptr);
}

static FAR void *bk7258_audio_osi_memcpy(FAR void *out,
                                        FAR const void *in, uint32_t n)
{
  return os_memcpy(out, in, n);
}

static void bk7258_audio_osi_memcpy_word(FAR void *out,
                                        FAR const void *in, uint32_t n)
{
  os_memcpy_word(out, in, n);
}

static FAR void *bk7258_audio_osi_memset(FAR void *buffer, int value,
                                        uint32_t n)
{
  return os_memset(buffer, value, n);
}

static FAR void *bk7258_audio_osi_memmove(FAR void *out,
                                         FAR const void *in, uint32_t n)
{
  return os_memmove(out, in, n);
}

static void bk7258_audio_osi_memset_word(FAR void *buffer, int32_t value,
                                        uint32_t n)
{
  os_memset_word(buffer, value, n);
}

static void bk7258_audio_osi_assert(uint8_t expression,
                                   FAR char *expression_text,
                                   FAR const char *function)
{
  (void)expression_text;
  (void)function;

  if (expression == 0)
    {
      PANIC();
    }
}

static struct bk7258_audio_osi_funcs_s g_bk7258_audio_osi_funcs =
{
  .psram_malloc = bk7258_audio_osi_psram_malloc,
  .psram_realloc = bk7258_audio_osi_psram_realloc,
  .malloc = bk7258_audio_osi_malloc,
  .zalloc = bk7258_audio_osi_zalloc,
  .realloc = bk7258_audio_osi_realloc,
  .free = bk7258_audio_osi_free,
  .memcpy = bk7258_audio_osi_memcpy,
  .memcpy_word = bk7258_audio_osi_memcpy_word,
  .memset = bk7258_audio_osi_memset,
  .memmove = bk7258_audio_osi_memmove,
  .memset_word = bk7258_audio_osi_memset_word,
  .log_write = bk_printf_ext,
  .osi_assert = bk7258_audio_osi_assert,
  .get_time = rtos_get_time,
};

static bool g_bk7258_audio_osi_ready;

static struct bk7258_audio_preprocess_s g_bk7258_audio_preprocess =
{
  .lock = NXMUTEX_INITIALIZER,
};

static int bk7258_audio_preprocess_sdk_result(int result)
{
  return result == 0 ? OK : (result < 0 ? result : -EIO);
}

static int bk7258_audio_osi_initialize(void)
{
  bk_err_t result;

  if (g_bk7258_audio_osi_ready)
    {
      return OK;
    }

  result = audio_osi_funcs_init(&g_bk7258_audio_osi_funcs);
  if (result != BK_OK)
    {
      return bk7258_audio_preprocess_sdk_result(result);
    }

  if (bk_get_audio_osi_funcs() != &g_bk7258_audio_osi_funcs)
    {
      return -EPROTO;
    }

  g_bk7258_audio_osi_ready = true;
  return OK;
}

static void bk7258_audio_preprocess_reset_diag(
  FAR struct bk7258_audio_preprocess_s *priv)
{
  memset(&priv->diag, 0, sizeof(priv->diag));
  priv->diag.magic = BK7258_AUDIO_PREPROCESS_DIAG_MAGIC;
  priv->diag.version = BK7258_AUDIO_PREPROCESS_DIAG_VERSION;
  priv->diag.size = sizeof(priv->diag);
}

static void bk7258_audio_preprocess_release(
  FAR struct bk7258_audio_preprocess_s *priv)
{
  if (priv->agc != NULL)
    {
      (void)bk_aud_agc_free(priv->agc);
      priv->agc = NULL;
    }

#if BK7258_SDK_VAD_ENABLED
  if (priv->vad_ready)
    {
      (void)bk_aud_vad_deinit();
      priv->vad_ready = false;
    }
#else
  priv->vad_ready = false;
#endif

#if BK7258_SDK_NS_ENABLED
  if (priv->ns_ready)
    {
      (void)bk_aud_ns_deinit();
      priv->ns_ready = false;
    }
#else
  priv->ns_ready = false;
#endif

  if (priv->aec != NULL)
    {
      explicit_bzero(priv->aec, priv->diag.aec_context_bytes);
      kmm_free(priv->aec);
      priv->aec = NULL;
    }

  if (priv->work != NULL)
    {
      explicit_bzero(priv->work,
                     BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES *
                     sizeof(int16_t) * BK7258_AUDIO_WORK_BUFFERS);
      kmm_free(priv->work);
      priv->work = NULL;
    }

  priv->near = NULL;
  priv->reference = NULL;
  priv->aec_output = NULL;
  priv->agc_output = NULL;
  priv->ready = false;
  priv->diag.ready = 0;
}

int bk7258_audio_preprocess_initialize(
  FAR const struct bk7258_audio_preprocess_config_s *config)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;
  struct bk7258_agc_config_sdk_s agc_config;
  size_t work_bytes;
  uint32_t aec_bytes;
  uint32_t aec_frame_samples = 0;
  int ret;

  if (config == NULL ||
      config->sample_rate != BK7258_AUDIO_PREPROCESS_RATE ||
      config->frame_samples != BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES ||
      config->aec_delay_samples > BK7258_AEC_DELAY_CAPACITY_SAMPLES)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (priv->ready)
    {
      nxmutex_unlock(&priv->lock);
      return -EALREADY;
    }

  bk7258_audio_preprocess_reset_diag(priv);

  ret = bk7258_audio_osi_initialize();
  if (ret < 0)
    {
      goto errout;
    }

  /* aec_size() takes the maximum delay-buffer capacity, not the current
   * physical delay.  The pinned SDK's official AEC pipeline allocates for a
   * fixed maximum, then applies the measured delay separately with aec_ctrl.
   * Allocating only the board's 16-sample delay can leave the opaque context
   * too small for aec_proc(), matching the observed first-frame precise data
   * BusFault.
   */

  aec_bytes = aec_size(BK7258_AEC_DELAY_CAPACITY_SAMPLES);
  if (aec_bytes < 4096u || aec_bytes > 131072u)
    {
      ret = -EPROTO;
      goto errout;
    }

  priv->diag.aec_context_bytes = aec_bytes;
  priv->aec = (FAR AECContext *)kmm_zalloc(aec_bytes);
  work_bytes = BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES *
               sizeof(int16_t) * BK7258_AUDIO_WORK_BUFFERS;
  priv->work = kmm_zalloc(work_bytes);
  if (priv->aec == NULL || priv->work == NULL)
    {
      ret = -ENOMEM;
      goto errout;
    }

  priv->near = priv->work;
  priv->reference = priv->near + BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES;
  priv->aec_output = priv->reference +
                     BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES;
  priv->agc_output = priv->aec_output +
                     BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES;

  aec_init(priv->aec, BK7258_AUDIO_PREPROCESS_RATE);
  aec_ctrl(priv->aec, AEC_CTRL_CMD_SET_FLAGS, BK7258_AEC_FLAGS);
  aec_ctrl(priv->aec, AEC_CTRL_CMD_SET_MAX_DELAY,
           BK7258_AEC_DELAY_CAPACITY_SAMPLES);
  aec_ctrl(priv->aec, AEC_CTRL_CMD_SET_DELAY_BUFF,
           (uint32_t)(uintptr_t)priv->aec->refbuff);
  aec_ctrl(priv->aec, AEC_CTRL_CMD_SET_MIC_DELAY,
           config->aec_delay_samples);
  aec_ctrl(priv->aec, AEC_CTRL_CMD_GET_FRAME_SAMPLE,
           (uint32_t)(uintptr_t)&aec_frame_samples);
  if (aec_frame_samples != BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES)
    {
      ret = -EPROTO;
      goto errout;
    }

  priv->diag.backend_version = aec_ver();
#if BK7258_SDK_NS_ENABLED
  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_ns_init(BK7258_SDK_FRAME_SAMPLES_10MS,
                   BK7258_AUDIO_PREPROCESS_RATE));
  if (ret < 0)
    {
      goto errout;
    }

  priv->ns_ready = true;
#else
  priv->ns_ready = false;
#endif

  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_agc_create(&priv->agc));
  if (ret < 0 || priv->agc == NULL)
    {
      ret = ret < 0 ? ret : -ENOMEM;
      goto errout;
    }

  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_agc_init(priv->agc, BK7258_AGC_MIN_LEVEL,
                    BK7258_AGC_MAX_LEVEL,
                    BK7258_AUDIO_PREPROCESS_RATE));
  if (ret < 0)
    {
      goto errout;
    }

  memset(&agc_config, 0, sizeof(agc_config));
  agc_config.target_level_dbfs = BK7258_AGC_TARGET_DBFS;
  agc_config.compression_gain_db = BK7258_AGC_COMPRESSION_DB;
  agc_config.limiter_enable = BK7258_AGC_LIMITER_ENABLE;
  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_agc_set_config(priv->agc, agc_config));
  if (ret < 0)
    {
      goto errout;
    }

#if BK7258_SDK_VAD_ENABLED
  ret = bk7258_audio_preprocess_sdk_result(
    bk_aud_vad_init(BK7258_SDK_FRAME_SAMPLES_10MS,
                    BK7258_AUDIO_PREPROCESS_RATE));
  if (ret < 0)
    {
      goto errout;
    }

  priv->vad_ready = true;
#else
  priv->vad_ready = false;
#endif
  priv->ready = true;
  priv->diag.ready = 1;
  nxmutex_unlock(&priv->lock);
  return OK;

errout:
  priv->diag.last_error = ret;
  bk7258_audio_preprocess_release(priv);
  nxmutex_unlock(&priv->lock);
  return ret;
}

int bk7258_audio_preprocess_process(FAR const int16_t *interleaved,
                                    uint32_t frames,
                                    FAR int16_t *output)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;
  uint32_t index;
  uint32_t offset;
#if BK7258_SDK_VAD_ENABLED
  int chunk_vad;
  int vad;
#endif
  int ret;

  if (interleaved == NULL || output == NULL ||
      frames != BK7258_AUDIO_PREPROCESS_FRAME_SAMPLES)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  if (!priv->ready)
    {
      nxmutex_unlock(&priv->lock);
      return -ENODEV;
    }

  for (index = 0; index < frames; index++)
    {
      priv->near[index] = interleaved[index * 2u];
      priv->reference[index] = interleaved[index * 2u + 1u];
    }

  aec_proc(priv->aec, priv->reference, priv->near, priv->aec_output);

#if BK7258_SDK_NS_ENABLED
  /* Despite the legacy header naming its argument frame_size_20ms, the
   * pinned NS and VAD demos initialize with 320 bytes / 160 samples and call
   * each library once per such block.  Process the 20-ms AEC output as two
   * consecutive 10-ms blocks, matching that executable SDK contract.
   */

  for (offset = 0; offset < frames;
       offset += BK7258_SDK_FRAME_SAMPLES_10MS)
    {
      ret = bk_aud_ns_process(priv->aec_output + offset);
      if (ret < 0)
        {
          memcpy(output, priv->near, frames * sizeof(*output));
          priv->diag.last_error = ret;
          priv->diag.process_failures++;
          nxmutex_unlock(&priv->lock);
          return ret;
        }

    }
#else
#endif

  /* The public AGC header permits 10- or 20-ms calls, but the pinned SDK's
   * maintained audio pipeline always feeds 16-kHz audio as two 160-sample
   * calls.  Follow that executable contract so its internal WebRTC state is
   * advanced exactly as in the vendor integration.
   */

  for (offset = 0; offset < frames;
       offset += BK7258_SDK_FRAME_SAMPLES_10MS)
    {
      ret = bk7258_audio_preprocess_sdk_result(
        bk_aud_agc_process(priv->agc, priv->aec_output + offset,
                           (int16_t)BK7258_SDK_FRAME_SAMPLES_10MS,
                           priv->agc_output + offset));
      if (ret < 0)
        {
          memcpy(output, priv->near, frames * sizeof(*output));
          priv->diag.last_error = ret;
          priv->diag.process_failures++;
          nxmutex_unlock(&priv->lock);
          return ret;
        }

    }

#if BK7258_SDK_VAD_ENABLED
  vad = 0;
  for (offset = 0; offset < frames;
       offset += BK7258_SDK_FRAME_SAMPLES_10MS)
    {
      chunk_vad = bk_aud_vad_process(priv->agc_output + offset);
      if (chunk_vad < 0)
        {
          memcpy(output, priv->near, frames * sizeof(*output));
          priv->diag.last_error = chunk_vad;
          priv->diag.process_failures++;
          nxmutex_unlock(&priv->lock);
          return chunk_vad;
        }

      if (chunk_vad > 0)
        {
          vad = 1;
        }

    }
#else
#endif

  memcpy(output, priv->agc_output, frames * sizeof(*output));
  priv->diag.frames++;
#if BK7258_SDK_VAD_ENABLED
  priv->diag.last_vad = vad > 0 ? 1u : 0u;
  if (vad > 0)
    {
      priv->diag.speech_frames++;
    }
  else
    {
      priv->diag.silence_frames++;
    }
#else
  priv->diag.last_vad = 0;
  priv->diag.silence_frames++;
#endif

  nxmutex_unlock(&priv->lock);
  return OK;
}

void bk7258_audio_preprocess_deinitialize(void)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;

  if (nxmutex_lock(&priv->lock) >= 0)
    {
      bk7258_audio_preprocess_release(priv);
      nxmutex_unlock(&priv->lock);
    }
}

int bk7258_audio_preprocess_get_diag(
  FAR struct bk7258_audio_preprocess_diag_s *diag)
{
  FAR struct bk7258_audio_preprocess_s *priv =
    &g_bk7258_audio_preprocess;
  int ret;

  if (diag == NULL)
    {
      return -EINVAL;
    }

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(diag, &priv->diag, sizeof(*diag));
  nxmutex_unlock(&priv->lock);
  return OK;
}

#endif
