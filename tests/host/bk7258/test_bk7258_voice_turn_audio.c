/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_turn_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <media_player.h>
#include <media_policy.h>
#include <media_recorder.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_voice_turn.h"
#include "bk7258_voice_turn_audio.h"

#ifdef CONFIG_BK7258_PREFERENCES
#include "bk7258_preferences.h"
#endif

#define TEST_FRAME_BYTES 640u
#define TEST_TRACE_MAX   64u

enum test_call_e
{
  TEST_REC_OPEN = 1,
  TEST_REC_PREPARE,
  TEST_REC_START,
  TEST_REC_READ,
  TEST_REC_STOP,
  TEST_REC_CLOSE,
  TEST_PLAYER_OPEN,
  TEST_PLAYER_PREPARE,
  TEST_PLAYER_START,
  TEST_PLAYER_WRITE,
  TEST_PLAYER_EOF,
  TEST_PLAYER_CLOSE,
#ifdef CONFIG_BK7258_PREFERENCES
  TEST_PREFS_READ,
#endif
  TEST_VOLUME_RANGE,
  TEST_VOLUME_SET,
  TEST_VOLUME_GET,
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  TEST_VOLUME_STORE_GET,
  TEST_VOLUME_STORE_SET,
#endif
};

struct test_media_s
{
  enum test_call_e trace[TEST_TRACE_MAX];
  size_t trace_count;
  enum test_call_e fail_call;
  unsigned int fail_count;
  bool recorder_open;
  bool recorder_prepared;
  bool recorder_started;
  bool player_open;
  bool player_prepared;
  bool player_started;
  bool defer_completion;
  bool overlap;
};

static struct test_media_s g_media;
static int g_recorder_handle;
static int g_player_handle;
static media_event_callback g_player_callback;
static void *g_player_cookie;

int media_player_set_event_callback(void *handle, void *cookie,
                                    media_event_callback callback)
{
  assert(handle == &g_player_handle);
  g_player_callback = callback;
  g_player_cookie = cookie;
  return 0;
}

static int test_call(enum test_call_e call)
{
  assert(g_media.trace_count < TEST_TRACE_MAX);
  g_media.trace[g_media.trace_count++] = call;
  if (g_media.fail_call == call && g_media.fail_count > 0)
    {
      g_media.fail_count--;
      return -EIO;
    }

  return 0;
}

static int g_volume_index;
static int g_range_max = 15;
static bool g_volume_mismatch;

#ifdef CONFIG_BK7258_PREFERENCES
static unsigned int g_percent = 50;

int bk7258_preferences_playback_volume(unsigned int *volume_percent)
{
  *volume_percent = g_percent;
  return test_call(TEST_PREFS_READ);
}
#endif

#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
static unsigned int g_store_percent;
static int g_store_get_result;
static int g_store_set_result;
static unsigned int g_store_set_calls;

int bkvoice_volume_store_get(unsigned int *volume_percent)
{
  int ret = test_call(TEST_VOLUME_STORE_GET);

  if (ret < 0)
    {
      return ret;
    }

  if (g_store_get_result < 0)
    {
      return g_store_get_result;
    }

  *volume_percent = g_store_percent;
  return 0;
}

int bkvoice_volume_store_set(unsigned int volume_percent)
{
  int ret = test_call(TEST_VOLUME_STORE_SET);

  g_store_set_calls++;
  if (ret < 0)
    {
      return ret;
    }

  if (g_store_set_result < 0)
    {
      return g_store_set_result;
    }

  g_store_percent = volume_percent;
  g_store_get_result = 0;
  return 0;
}

static void reset_volume_store(void)
{
  g_store_percent = 0;
  g_store_get_result = -ENOENT;
  g_store_set_result = 0;
  g_store_set_calls = 0;
}
#endif

int media_policy_get_range(const char *name, int *minimum, int *maximum)
{
  assert(strcmp(name, MEDIA_STREAM_MUSIC MEDIA_POLICY_VOLUME) == 0);
  *minimum = 0;
  *maximum = g_range_max;
  return test_call(TEST_VOLUME_RANGE);
}

int media_policy_set_stream_volume(const char *stream, int index)
{
  assert(strcmp(stream, MEDIA_STREAM_MUSIC) == 0);
  g_volume_index = index;
  return test_call(TEST_VOLUME_SET);
}

int media_policy_get_stream_volume(const char *stream, int *index)
{
  assert(strcmp(stream, MEDIA_STREAM_MUSIC) == 0);
  *index = g_volume_index + (g_volume_mismatch ? 1 : 0);
  return test_call(TEST_VOLUME_GET);
}

void *media_recorder_open(const char *params)
{
  int ret = test_call(TEST_REC_OPEN);

  assert(params != NULL && strcmp(params, MEDIA_SOURCE_MIC) == 0);
  if (g_media.player_open)
    {
      g_media.overlap = true;
    }

  if (ret < 0)
    {
      errno = EIO;
      return NULL;
    }

  assert(!g_media.recorder_open);
  g_media.recorder_open = true;
  return &g_recorder_handle;
}

int media_recorder_prepare(void *handle, const char *url,
                           const char *options)
{
  int ret = test_call(TEST_REC_PREPARE);

  assert(handle == &g_recorder_handle && url == NULL);
  assert(options != NULL &&
         strcmp(options,
                "format=s16le:sample_rate=16000:ch_layout=mono") == 0);
  if (ret >= 0)
    {
      g_media.recorder_prepared = true;
    }

  return ret;
}

int media_recorder_start(void *handle)
{
  int ret = test_call(TEST_REC_START);

  assert(handle == &g_recorder_handle && g_media.recorder_prepared);
  if (ret >= 0)
    {
      g_media.recorder_started = true;
    }

  return ret;
}

ssize_t media_recorder_read_data(void *handle, void *data, size_t len)
{
  int ret = test_call(TEST_REC_READ);

  assert(handle == &g_recorder_handle && data != NULL && len > 0);
  assert(g_media.recorder_started);
  if (ret < 0)
    {
      return ret;
    }

  memset(data, 0x5a, len);
  return (ssize_t)len;
}

int media_recorder_stop(void *handle)
{
  int ret = test_call(TEST_REC_STOP);

  assert(handle == &g_recorder_handle && g_media.recorder_open);
  if (ret >= 0)
    {
      g_media.recorder_started = false;
    }

  return ret;
}

int media_recorder_close(void *handle)
{
  int ret = test_call(TEST_REC_CLOSE);

  assert(handle == &g_recorder_handle && g_media.recorder_open);
  if (ret >= 0)
    {
      g_media.recorder_open = false;
      g_media.recorder_prepared = false;
      g_media.recorder_started = false;
    }

  return ret;
}

void *media_player_open(const char *stream)
{
  int ret = test_call(TEST_PLAYER_OPEN);

  assert(stream != NULL && strcmp(stream, MEDIA_STREAM_MUSIC) == 0);
  if (g_media.recorder_open)
    {
      g_media.overlap = true;
    }

  if (ret < 0)
    {
      errno = EIO;
      return NULL;
    }

  assert(!g_media.player_open);
  g_media.player_open = true;
  return &g_player_handle;
}

int media_player_prepare(void *handle, const char *url,
                         const char *options)
{
  int ret = test_call(TEST_PLAYER_PREPARE);

  assert(handle == &g_player_handle && url == NULL);
  assert(options != NULL &&
         strcmp(options,
                "format=s16le:sample_rate=16000:ch_layout=mono") == 0);
  if (ret >= 0)
    {
      g_media.player_prepared = true;
    }

  return ret;
}

int media_player_start(void *handle)
{
  int ret = test_call(TEST_PLAYER_START);

  assert(handle == &g_player_handle && g_media.player_prepared);
  if (ret >= 0)
    {
      g_media.player_started = true;
    }

  return ret;
}

ssize_t media_player_write_data(void *handle, const void *data, size_t len)
{
  int ret = test_call(TEST_PLAYER_WRITE);

  assert(handle == &g_player_handle && data != NULL && len > 0);
  assert(g_media.player_started);
  return ret < 0 ? ret : (ssize_t)len;
}

void media_player_close_socket(void *handle)
{
  int ret = test_call(TEST_PLAYER_EOF);

  assert(handle == &g_player_handle && g_media.player_open);
  if (ret >= 0)
    {
      g_media.player_prepared = false;
      g_media.player_started = false;
    }

  if (!g_media.defer_completion)
    {
      g_player_callback(g_player_cookie, MEDIA_EVENT_COMPLETED, ret, NULL);
    }
}

int media_player_close(void *handle, int pending_stop)
{
  int ret = test_call(TEST_PLAYER_CLOSE);

  assert(handle == &g_player_handle && pending_stop == 0);
  assert(g_media.player_open);
  if (ret >= 0)
    {
      if (g_media.defer_completion)
        {
          g_player_callback(g_player_cookie, MEDIA_EVENT_COMPLETED, 0, NULL);
          g_media.defer_completion = false;
        }
      g_media.player_open = false;
      g_media.player_prepared = false;
      g_media.player_started = false;
    }

  return ret;
}

static const struct bkvoice_turn_limits_s g_limits =
{
  .capture_timeout_ms = 1000,
  .waiting_tts_timeout_ms = 2000,
  .playback_timeout_ms = 3000,
  .audio_frame_bytes = TEST_FRAME_BYTES,
};

static struct bkvoice_turn_token_s test_event(
  const struct bkvoice_turn_token_s *token, uint32_t sequence)
{
  struct bkvoice_turn_token_s event = *token;

  event.sequence = sequence;
  return event;
}

static void test_initialize(struct bkvoice_turn_s *turn,
                            struct bkvoice_turn_audio_s *audio)
{
  memset(&g_media, 0, sizeof(g_media));
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  reset_volume_store();
#endif
  assert(bkvoice_turn_audio_initialize(audio) == 0);
  assert(bkvoice_turn_audio_released(audio));
  assert(bkvoice_turn_initialize(turn, bkvoice_turn_audio_ops(), audio,
                                 &g_limits, 11) == 0);
  assert(bkvoice_turn_session_open(turn, 3) == 0);
}

static void test_normal_turn(void)
{
  static const enum test_call_e expected[] =
  {
    TEST_REC_OPEN, TEST_REC_PREPARE, TEST_REC_START, TEST_REC_READ,
    TEST_REC_STOP, TEST_REC_CLOSE,
    TEST_PLAYER_OPEN, TEST_PLAYER_PREPARE,
#ifdef CONFIG_BK7258_PREFERENCES
    TEST_PREFS_READ, TEST_VOLUME_RANGE, TEST_VOLUME_SET, TEST_VOLUME_GET,
#elif defined(CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE)
    TEST_VOLUME_STORE_GET,
#endif
    TEST_PLAYER_START,
    TEST_PLAYER_WRITE, TEST_PLAYER_EOF, TEST_PLAYER_CLOSE,
  };
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  uint8_t pcm[TEST_FRAME_BYTES];

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 100, &token) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == -EALREADY);
  assert(bkvoice_turn_audio_read(&audio, pcm, sizeof(pcm)) ==
         (ssize_t)sizeof(pcm));
  assert(pcm[0] == 0x5a && pcm[sizeof(pcm) - 1] == 0x5a);
  assert(bkvoice_turn_audio_reader_detach(&audio) == 0);

  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 200) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 300) == 0);
  event.sequence = 2;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm),
                                400) == 0);
  event.sequence = 3;
  assert(bkvoice_turn_tts_end(&turn, &event) == 0);
  assert(turn.state == BKVOICE_TURN_DRAINING);
  assert(bkvoice_turn_poll(&turn) == 0);
  assert(bkvoice_turn_audio_released(&audio));
  assert(!g_media.recorder_open && !g_media.player_open &&
         !g_media.overlap);
  assert(g_media.trace_count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(g_media.trace, expected, sizeof(expected)) == 0);
}

#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
static void test_persistent_volume_policy(void)
{
  const struct bkvoice_turn_audio_ops_s *ops = bkvoice_turn_audio_ops();
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_s turn;
  unsigned int observed = UINT32_MAX;
  size_t before;

  /* The first query after boot restores the durable value and reports the
   * media-policy quantized result without writing flash again.
   */

  test_initialize(&turn, &audio);
  g_store_get_result = 0;
  g_store_percent = 73;
  g_volume_index = 8;
  g_range_max = 15;
  assert(ops->volume(&audio, false, 0, &observed) == 0);
  assert(observed == 73 && g_volume_index == 11);
  assert(audio.volume_override && audio.volume_percent == 73);
  assert(g_store_set_calls == 0);
  g_store_percent = 24; /* Later settings change uses the same store. */
  assert(ops->dac_acquire(&audio) == 0);
  assert(ops->dac_prepare(&audio) == 0);
  assert(g_volume_index == 4);
  assert(ops->dac_release(&audio) == 0);

  /* A failed durable publication leaves the current media policy untouched
   * and cannot produce a successful VOLUME_REPORT.
   */

  test_initialize(&turn, &audio);
  g_volume_index = 8;
  g_store_set_result = -ENOSPC;
  before = g_media.trace_count;
  assert(ops->volume(&audio, true, 65, &observed) == -ENOSPC);
  assert(g_volume_index == 8 && !audio.volume_override);
  assert(g_media.trace_count == before + 1);
  assert(g_media.trace[before] == TEST_VOLUME_STORE_SET);

  /* If media application fails after publication, the next query reconciles
   * the saved value instead of claiming that the failed call applied it.
   */

  test_initialize(&turn, &audio);
  g_volume_index = 8;
  g_media.fail_call = TEST_VOLUME_SET;
  g_media.fail_count = 1;
  assert(ops->volume(&audio, true, 65, &observed) == -EIO);
  assert(g_store_percent == 65 && g_store_get_result == 0);
  assert(!audio.volume_override);
  assert(ops->volume(&audio, false, 0, &observed) == 0);
  assert(observed == 67 && audio.volume_override);

  /* Corrupt/unavailable preference data never takes voice playback offline. */

  test_initialize(&turn, &audio);
  g_store_get_result = -EBADMSG;
  assert(ops->dac_acquire(&audio) == 0);
  assert(ops->dac_prepare(&audio) == 0);
  assert(ops->dac_release(&audio) == 0);
  assert(bkvoice_turn_audio_released(&audio));
}
#endif

static void test_recorder_close_recovery(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  g_media.fail_call = TEST_REC_CLOSE;
  g_media.fail_count = 1;
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -EIO);
  assert(turn.state == BKVOICE_TURN_FAULTED);
  assert(audio.mic_handle != NULL && g_media.recorder_open);
  assert(bkvoice_turn_recover(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_player_stop_retry(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  uint8_t pcm[TEST_FRAME_BYTES] = {0};
  size_t before;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 2) == 0);
  event.sequence = 2;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm), 3) == 0);

  g_media.fail_call = TEST_PLAYER_EOF;
  g_media.fail_count = 1;
  before = g_media.trace_count;
  event.sequence = 3;
  assert(bkvoice_turn_tts_end(&turn, &event) == 0);
  assert(bkvoice_turn_poll(&turn) == -EIO);
  assert(g_media.trace_count == before + 2);
  assert(g_media.trace[before] == TEST_PLAYER_EOF);
  assert(g_media.trace[before + 1] == TEST_PLAYER_CLOSE);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_cancel_does_not_drain_and_can_retry_close(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;
  uint8_t pcm[TEST_FRAME_BYTES] = {0};
  size_t before;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 2) == 0);
  event.sequence = 2;
  assert(bkvoice_turn_tts_audio(&turn, &event, pcm, sizeof(pcm), 3) == 0);
  g_media.defer_completion = true;
  event.sequence = 3;
  assert(bkvoice_turn_tts_end(&turn, &event) == 0);
  assert(turn.state == BKVOICE_TURN_DRAINING);
  assert(bkvoice_turn_poll(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_DRAINING);
  before = g_media.trace_count;
  g_media.fail_call = TEST_PLAYER_CLOSE;
  g_media.fail_count = 2;
  event = test_event(&token, 3);
  assert(bkvoice_turn_cancel(&turn, &event, -ECANCELED) == -EIO);
  assert(turn.state == BKVOICE_TURN_FAULTED);
  assert(audio.dac_handle != NULL && g_media.player_open);
  assert(g_media.trace_count == before + 2);
  assert(g_media.trace[before] == TEST_PLAYER_CLOSE);
  assert(g_media.trace[before + 1] == TEST_PLAYER_CLOSE);
  assert(bkvoice_turn_recover(&turn) == 0);
  assert(g_media.trace_count == before + 3);
  assert(g_media.trace[before + 2] == TEST_PLAYER_CLOSE);
  assert(bkvoice_turn_audio_released(&audio));
  assert(bkvoice_turn_poll(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);

  /* The callback racing with the previous close must not finish a new turn. */

  assert(bkvoice_turn_ptt_down(&turn, 3, 4, 10, &token) == 0);
  event = test_event(&token, 5);
  assert(bkvoice_turn_ptt_up(&turn, &event, 11) == 0);
  event = test_event(&token, turn.last_downlink_sequence + 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 12) == 0);
  g_media.defer_completion = true;
  event.sequence++;
  assert(bkvoice_turn_tts_end(&turn, &event) == 0);
  assert(bkvoice_turn_poll(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_DRAINING);
  g_media.defer_completion = false;
  g_player_callback(g_player_cookie, MEDIA_EVENT_COMPLETED, 0, NULL);
  assert(bkvoice_turn_poll(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);
}

static void test_player_close_recovery(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  event = test_event(&token, 1);
  assert(bkvoice_turn_tts_start(&turn, &event, 2) == 0);

  g_media.fail_call = TEST_PLAYER_CLOSE;
  g_media.fail_count = 2;
  event.sequence = 2;
  assert(bkvoice_turn_tts_end(&turn, &event) == 0);
  assert(bkvoice_turn_poll(&turn) == -EIO);
  assert(turn.state == BKVOICE_TURN_FAULTED);
  assert(audio.dac_handle != NULL && g_media.player_open);
  assert(bkvoice_turn_recover(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_guards_and_open_failure(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_s turn;
  uint8_t pcm[TEST_FRAME_BYTES];

  assert(bkvoice_turn_audio_initialize(NULL) == -EINVAL);
  assert(bkvoice_turn_audio_reader_attach(NULL) == -EINVAL);
  assert(bkvoice_turn_audio_reader_stop(NULL) == -EINVAL);
  assert(!bkvoice_turn_audio_released(NULL));
  assert(bkvoice_turn_audio_initialize(&audio) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == -EPERM);
  assert(bkvoice_turn_audio_reader_stop(&audio) == -EPERM);
  assert(bkvoice_turn_audio_reader_detach(&audio) == -EALREADY);
  assert(bkvoice_turn_audio_read(&audio, pcm, sizeof(pcm)) == -EPERM);

  test_initialize(&turn, &audio);
  g_media.fail_call = TEST_REC_OPEN;
  g_media.fail_count = 1;
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == -EIO);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_reader_stop_join_sequence(void)
{
  static const enum test_call_e expected[] =
  {
    TEST_REC_OPEN, TEST_REC_PREPARE, TEST_REC_START,
    TEST_REC_STOP, TEST_REC_CLOSE,
  };
  const struct bkvoice_capture_source_ops_s *source_ops;
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  test_initialize(&turn, &audio);
  source_ops = bkvoice_turn_audio_capture_source_ops();
  assert(source_ops != NULL);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  assert(source_ops->attach(&audio) == 0);

  /* PTT owner interrupts the recorder, joins this reader task, then the
   * worker detaches.  Only after that may the arbiter release the MIC.
   */

  assert(source_ops->interrupt(&audio) == 0);
  assert(!audio.mic_started && audio.mic_reader_active);
  assert(source_ops->detach(&audio) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
  assert(turn.state == BKVOICE_TURN_WAITING_TTS);
  assert(bkvoice_turn_audio_released(&audio));
  assert(g_media.trace_count == sizeof(expected) / sizeof(expected[0]));
  assert(memcmp(g_media.trace, expected, sizeof(expected)) == 0);
}

static void test_reader_pins_recorder(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  test_initialize(&turn, &audio);
  assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
  assert(bkvoice_turn_audio_reader_attach(&audio) == 0);
  event = test_event(&token, 2);
  assert(bkvoice_turn_ptt_up(&turn, &event, 1) == -EBUSY);
  assert(turn.state == BKVOICE_TURN_FAULTED);
  assert(audio.mic_handle != NULL && g_media.recorder_open);
  assert(bkvoice_turn_recover(&turn) == -EBUSY);
  assert(audio.mic_handle != NULL && g_media.recorder_open);
  assert(bkvoice_turn_audio_reader_detach(&audio) == 0);
  assert(bkvoice_turn_recover(&turn) == 0);
  assert(turn.state == BKVOICE_TURN_IDLE);
  assert(bkvoice_turn_audio_released(&audio));
}

static void test_runtime_volume_policy(void)
{
  const struct bkvoice_turn_audio_ops_s *ops = bkvoice_turn_audio_ops();
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_s turn;
  unsigned int observed = UINT32_MAX;

  test_initialize(&turn, &audio);
  g_volume_index = 8;
  g_range_max = 15;
  g_volume_mismatch = false;
  assert(ops->volume != NULL);
  assert(ops->volume(&audio, false, 0, &observed) == 0 && observed == 53);
  assert(ops->volume(&audio, true, 65, &observed) == 0);
  assert(observed == 67 && audio.volume_override);
  assert(audio.volume_percent == 65);
  assert(ops->volume(&audio, true, 101, &observed) == -EINVAL);
  g_volume_mismatch = true;
  assert(ops->volume(&audio, true, 40, &observed) == -EIO);
  assert(audio.volume_percent == 65);
  g_volume_mismatch = false;

  assert(ops->dac_acquire(&audio) == 0);
  assert(ops->dac_prepare(&audio) == 0);
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  /* 40 was committed even though immediate policy application failed. */
  assert(g_volume_index == 6);
#else
  assert(g_volume_index == 10); /* RAM-only override survives next turn. */
#endif
  assert(ops->dac_release(&audio) == 0);
  assert(bkvoice_turn_audio_released(&audio));
}

#ifdef CONFIG_BK7258_PREFERENCES
static void test_stored_volume_policy(void)
{
  struct bkvoice_turn_audio_s audio;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_token_s event;
  struct bkvoice_turn_s turn;

  for (unsigned int i = 0; i < 8; i++)
    {
      test_initialize(&turn, &audio);
      assert(bkvoice_turn_ptt_down(&turn, 3, 1, 0, &token) == 0);
      event = test_event(&token, 2);
      assert(bkvoice_turn_ptt_up(&turn, &event, 1) == 0);
      event = test_event(&token, 1);
      g_percent = i == 6 ? 101 : 73;
      g_range_max = i == 5 ? 0 : 15;
      g_volume_mismatch = i == 4;
      if (i < 4)
        {
          g_media.fail_call = TEST_PREFS_READ + i;
          g_media.fail_count = 1;
        }
      int ret = bkvoice_turn_tts_start(&turn, &event, 2);
      if (i < 7)
        {
          assert(ret == (i == 5 || i == 6 ? -ERANGE : -EIO));
          assert(!g_media.player_started);
          assert(bkvoice_turn_audio_released(&audio));
        }
      else
        {
          assert(ret == 0 && g_volume_index == 11);
          event.sequence = 2;
          assert(bkvoice_turn_tts_end(&turn, &event) == 0);
          assert(turn.state == BKVOICE_TURN_DRAINING);
          assert(bkvoice_turn_poll(&turn) == 0);
          assert(bkvoice_turn_audio_released(&audio));
        }
    }
  static const unsigned int percentages[] = {0, 1, 50, 99, 100, 50};
  static const int maxima[] = {15, 15, 15, 15, 15, 31};
  static const int expected[] = {0, 0, 8, 15, 15, 16};
  g_volume_mismatch = false;
  for (unsigned int i = 0; i < sizeof(percentages) / sizeof(percentages[0]); i++)
    {
      const struct bkvoice_turn_audio_ops_s *ops = bkvoice_turn_audio_ops();
      test_initialize(&turn, &audio);
      g_percent = percentages[i];
      g_range_max = maxima[i];
      assert(ops->dac_acquire(&audio) == 0);
      assert(ops->dac_prepare(&audio) == 0);
      assert(g_volume_index == expected[i]);
      assert(ops->dac_release(&audio) == 0);
      assert(bkvoice_turn_audio_released(&audio));
    }
  g_percent = 50;
  g_range_max = 15;
}
#endif

int main(void)
{
  test_normal_turn();
  test_recorder_close_recovery();
  test_player_stop_retry();
  test_player_close_recovery();
  test_cancel_does_not_drain_and_can_retry_close();
  test_guards_and_open_failure();
  test_reader_stop_join_sequence();
  test_reader_pins_recorder();
  test_runtime_volume_policy();
  puts("BKVOICE_TURN_VOLUME_HOST_PASS");
#ifdef CONFIG_BK7258_PREFERENCES
  test_stored_volume_policy();
#endif
#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  test_persistent_volume_policy();
#endif
  puts("BKVOICE_TURN_AUDIO_HOST_PASS");
  return 0;
}
