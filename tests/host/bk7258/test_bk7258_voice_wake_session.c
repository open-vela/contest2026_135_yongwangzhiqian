/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_voice_wake_session.h"
#include "bk7258_voice_kws_model.h"
#include "bk7258_voice_wake_owner.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_model_open_calls;
static int g_listener_init_calls;
static int g_listener_uninit_calls;
static int g_owner_step_calls;
static int g_owner_suspend_calls;
static int g_owner_close_calls;
static int g_owner_close_result;
static struct bkvoice_kws_model_s *const g_model = (void *)1;

int bkvoice_kws_model_open(const struct bkvoice_kws_model_spec_s *spec,
                           void *arena, size_t arena_bytes,
                           struct bkvoice_kws_model_s **model)
{
  g_model_open_calls++;
  assert(spec != NULL && spec->bytes == strlen("model-approved"));
  assert(!memcmp(spec->data, "model-approved", spec->bytes));
  assert(!strcmp(spec->frontend, BKVOICE_KWS_FRONTEND_ID));
  assert(!strcmp(spec->labels[0], "silence"));
  assert(!strcmp(spec->labels[1], "unknown"));
  assert(!strcmp(spec->labels[2], BKVOICE_KWS_LABEL));
  assert(arena != NULL && (uintptr_t)arena % 16u == 0);
  assert(arena_bytes == 16384u);
  *model = g_model;
  return 0;
}

int bkvoice_kws_model_infer(void *context, const float *features,
                            float scores[BKVOICE_KWS_CLASSES])
{ (void)context; (void)features; (void)scores; return 0; }
void bkvoice_kws_model_close(struct bkvoice_kws_model_s *model)
{ if (model != NULL) assert(model == g_model); }

int bkvoice_kws_initialize(struct bkvoice_kws_s *kws,
  const struct bkvoice_kws_policy_s *policy, bkvoice_kws_infer_t infer,
  void *context)
{ (void)kws; assert(policy && infer && context == g_model); return 0; }
void bkvoice_kws_uninitialize(struct bkvoice_kws_s *kws) { (void)kws; }

int bkvoice_wake_window_initialize(struct bkvoice_wake_window_s *window,
  int16_t *pre_roll, size_t samples,
  const struct bkvoice_wake_window_policy_s *policy)
{ (void)window; assert(pre_roll && samples == BKVOICE_WAKE_PRE_ROLL_SAMPLES && policy); return 0; }
void bkvoice_wake_window_uninitialize(struct bkvoice_wake_window_s *window)
{ (void)window; }

int bkvoice_wake_listener_initialize(struct bkvoice_wake_listener_s *listener,
  const struct bkvoice_turn_audio_ops_s *audio_ops, void *audio_context,
  const struct bkvoice_capture_source_ops_s *source_ops, void *source_context,
  struct bkvoice_kws_s *kws, struct bkvoice_wake_window_s *window,
  sem_t *wake, size_t stack_size, uint32_t join_timeout_ms)
{
  (void)listener; (void)audio_ops; (void)source_ops; (void)kws; (void)window;
  g_listener_init_calls++;
  assert(audio_context == (void *)2 && source_context == (void *)3);
  assert(wake != NULL && stack_size == 4096u && join_timeout_ms == 100u);
  return 0;
}
int bkvoice_wake_listener_uninitialize(struct bkvoice_wake_listener_s *listener)
{ (void)listener; g_listener_uninit_calls++; return 0; }

int bkvoice_wake_owner_initialize(struct bkvoice_wake_owner_s *owner,
  struct bkvoice_wake_listener_s *listener,
  struct bkvoice_wake_window_s *window, struct bkcloud_runtime_s *cloud,
  sem_t *wake)
{ (void)owner; assert(listener && window && cloud == (void *)4 && wake); return 0; }
int bkvoice_wake_owner_step(struct bkvoice_wake_owner_s *owner,
                            bool allowed, uint64_t now_ms)
{ (void)owner; assert(allowed && now_ms != 0); g_owner_step_calls++; return 0; }
int bkvoice_wake_owner_suspend(struct bkvoice_wake_owner_s *owner)
{ (void)owner; g_owner_suspend_calls++; return 0; }
int bkvoice_wake_owner_close(struct bkvoice_wake_owner_s *owner)
{ (void)owner; g_owner_close_calls++; return g_owner_close_result; }

static void reset_calls(void)
{
  g_model_open_calls = g_listener_init_calls = g_listener_uninit_calls = 0;
  g_owner_step_calls = g_owner_suspend_calls = g_owner_close_calls = 0;
  g_owner_close_result = 0;
}

int main(void)
{
  static const char good_hash[] =
    "df20d2705ebcc6f9508a76cf61ffbb2ebe5c34dc57b6b3edf878e5aac11ee7f1";
  static const char bad_hash[] =
    "0f20d2705ebcc6f9508a76cf61ffbb2ebe5c34dc57b6b3edf878e5aac11ee7f1";
  struct bkvoice_wake_session_config_s config =
    {.model_sha256_hex = good_hash, .arena_bytes = 16384u,
     .listener_stack_size = 4096u, .listener_join_timeout_ms = 100u};
  struct bkvoice_wake_session_s *session = NULL;
  struct bkvoice_ptt_s ptt;
  sem_t wake;
  char path[] = "/tmp/bkvoice-wake-model-XXXXXX";
  char fifo[] = "/tmp/bkvoice-wake-fifo-XXXXXX";
  int fd;

  memset(&ptt, 0, sizeof(ptt));
  ptt.initialized = true;
  ptt.turn.audio_context = (void *)2;
  ptt.source_context = (void *)3;
  assert(sem_init(&wake, 0, 0) == 0);
  reset_calls();

  config.model_path = "/tmp/bkvoice-wake-model-missing";
  assert(bkvoice_wake_session_open(&session, &config, &ptt, (void *)4,
                                   &wake) == -ENOENT);
  assert(session == NULL && g_model_open_calls == 0 &&
         g_listener_init_calls == 0);

  fd = mkstemp(fifo);
  assert(fd >= 0 && close(fd) == 0 && unlink(fifo) == 0);
  assert(mkfifo(fifo, 0600) == 0);
  config.model_path = fifo;
  assert(bkvoice_wake_session_open(&session, &config, &ptt, (void *)4,
                                   &wake) == -EFBIG);
  assert(session == NULL && g_model_open_calls == 0 &&
         g_listener_init_calls == 0);
  assert(unlink(fifo) == 0);

  fd = mkstemp(path);
  assert(fd >= 0);
  assert(write(fd, "model-approved", strlen("model-approved")) ==
         (ssize_t)strlen("model-approved"));
  assert(close(fd) == 0);
  config.model_path = path;
  config.model_sha256_hex = bad_hash;
  assert(bkvoice_wake_session_open(&session, &config, &ptt, (void *)4,
                                   &wake) == -EKEYREJECTED);
  assert(session == NULL && g_model_open_calls == 0 &&
         g_listener_init_calls == 0);

  config.model_sha256_hex = good_hash;
  assert(bkvoice_wake_session_open(&session, &config, &ptt, (void *)4,
                                   &wake) == 0);
  assert(session != NULL && g_model_open_calls == 1 &&
         g_listener_init_calls == 1);
  assert(bkvoice_wake_session_open(&session, &config, &ptt, (void *)4,
                                   &wake) == -EINVAL);
  assert(bkvoice_wake_session_step(session, true, 1u) == 0);
  assert(bkvoice_wake_session_step(session, true, 2u) == 0);
  assert(g_owner_step_calls == 2);
  assert(bkvoice_wake_session_suspend(session) == 0 &&
         g_owner_suspend_calls == 1);

  g_owner_close_result = -EAGAIN;
  assert(bkvoice_wake_session_close(&session) == -EAGAIN);
  assert(session != NULL && g_listener_uninit_calls == 0);
  g_owner_close_result = 0;
  assert(bkvoice_wake_session_close(&session) == 0);
  assert(session == NULL && g_owner_close_calls == 2 &&
         g_listener_uninit_calls == 1);
  assert(unlink(path) == 0);
  assert(sem_destroy(&wake) == 0);
  puts("BKVOICE_WAKE_SESSION_HOST_PASS");
  return 0;
}
