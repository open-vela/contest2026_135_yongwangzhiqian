/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_playback.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#ifndef OUTSIDE_SPEEX
#  define OUTSIDE_SPEEX
#endif
#ifndef RANDOM_PREFIX
#  define RANDOM_PREFIX nuttx
#endif
#include <speex_resampler.h>

static void release_filter(struct bkcloud_playback_s *play)
{
  if (play->filter) speex_resampler_destroy(play->filter);
  play->filter = NULL;
  memset(play->frame, 0, sizeof(play->frame));
  play->frame_size = 0;
  play->low_byte = 0; play->partial_sample = false;
}
void bkcloud_playback_abort(struct bkcloud_playback_s *play, int reason)
{
  if (!play) return;
  if (play->turn && play->filter)
    {
      play->token.sequence = play->turn->last_control_sequence + 1;
      (void)bkvoice_turn_cancel(play->turn, &play->token,
                                reason < 0 ? reason : -ECANCELED);
    }
  release_filter(play);
  play->error = reason < 0 ? reason : -ECANCELED;
}
static int sequence(struct bkcloud_playback_s *play)
{
  if (play->turn->last_downlink_sequence >= UINT32_MAX - 1) return -EOVERFLOW;
  play->token.sequence = play->turn->last_downlink_sequence + 1;
  return 0;
}
static int emit_frame(struct bkcloud_playback_s *play)
{
  int ret = sequence(play);
  if (!ret) ret = bkvoice_turn_tts_audio(play->turn, &play->token, play->frame,
                    play->turn->limits.audio_frame_bytes,
                    play->now_ms(play->clock_context));
  if (!ret) { play->frame_size = 0; memset(play->frame, 0, sizeof(play->frame)); }
  return ret;
}
static int emit(struct bkcloud_playback_s *play, const spx_int16_t *pcm,
                size_t count)
{
  for (size_t i = 0; i < count; i++)
    {
      uint16_t sample = (uint16_t)pcm[i];
      play->frame[play->frame_size++] = sample;
      play->frame[play->frame_size++] = sample >> 8;
      play->output_samples++;
      if (play->frame_size == play->turn->limits.audio_frame_bytes)
        {
          int ret = emit_frame(play);
          if (ret) return ret;
        }
    }
  return 0;
}
int bkcloud_playback_begin(struct bkcloud_playback_s *play,
                           struct bkvoice_turn_s *turn,
                           uint64_t (*now_ms)(void *), void *context)
{
  int ret;
  if (!play || !turn || !now_ms || !turn->limits.audio_frame_bytes ||
      turn->limits.audio_frame_bytes > sizeof(play->frame) ||
      turn->limits.audio_frame_bytes % 2) return -EINVAL;
  if (turn->state != BKVOICE_TURN_WAITING_TTS) return -EBUSY;
  /* Caller supplies an inactive workspace; begin must not overwrite a live
   * resampler. All close/abort operations remain with the same turn owner.
   */
  memset(play, 0, sizeof(*play));
  play->filter = speex_resampler_init(1, 24000, 16000, 5, &ret);
  if (!play->filter) return -ENOMEM;
  ret = speex_resampler_skip_zeros(play->filter);
  if (ret) { release_filter(play); return -EIO; }
  play->turn = turn;
  play->token = turn->active;
  play->now_ms = now_ms;
  play->clock_context = context;
  ret = sequence(play);
  if (!ret) ret = bkvoice_turn_tts_start(turn, &play->token, now_ms(context));
  if (ret) { release_filter(play); play->error = ret; }
  return ret;
}
static int process(struct bkcloud_playback_s *play, const spx_int16_t *input,
                   spx_uint32_t count, uint64_t target)
{
  spx_int16_t output[320];
  while (count)
    {
      spx_uint32_t used = count, produced = 320;
      int ret = speex_resampler_process_int(play->filter, 0, input, &used,
                                             output, &produced);
      if (ret || (!used && !produced)) return -EIO;
      if (produced > target - play->output_samples)
        produced = target - play->output_samples;
      ret = emit(play, output, produced);
      if (ret) return ret;
      input += used; count -= used;
      if (play->output_samples == target) break;
    }
  memset(output, 0, sizeof(output));
  return 0;
}
int bkcloud_playback_feed(void *context, const void *pcm, size_t size)
{
  struct bkcloud_playback_s *play = context;
  const uint8_t *bytes = pcm;
  spx_int16_t input[480];
  if (!play || !play->filter) return -EINVAL;
  uint64_t remaining = (90u * 24000u - play->input_samples) * 2u;
  if ((!pcm && size) || size > remaining - (play->partial_sample ? 1u : 0u))
    { bkcloud_playback_abort(play, -EINVAL); return -EINVAL; }
  while (size)
    {
      size_t count = 0;
      if (play->partial_sample)
        {
          input[count++] = (spx_int16_t)((uint16_t)play->low_byte | ((uint16_t)*bytes++ << 8));
          size--; play->partial_sample = false; play->low_byte = 0;
        }
      while (size >= 2 && count < 480)
        {
          input[count++] = (spx_int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
          bytes += 2; size -= 2;
        }
      if (size == 1 && count < 480)
        {
          play->low_byte = *bytes++; play->partial_sample = true; size = 0;
        }
      play->input_samples += count;
      int ret = count ? process(play, input, count, UINT64_MAX) : 0;
      if (ret) { memset(input, 0, sizeof(input)); bkcloud_playback_abort(play, ret); return ret; }
    }
  memset(input, 0, sizeof(input));
  return 0;
}
int bkcloud_playback_end(struct bkcloud_playback_s *play)
{
  spx_int16_t zeros[256] = {0};
  if (!play || !play->filter) return -EINVAL;
  if (play->partial_sample)
    { bkcloud_playback_abort(play, -EBADMSG); return -EBADMSG; }
  uint64_t target = (play->input_samples * 2 + 2) / 3;
  unsigned int remaining = speex_resampler_get_input_latency(play->filter) + 3;
  int ret = target ? 0 : -ENODATA;
  while (!ret && play->output_samples < target && remaining)
    {
      unsigned int count = remaining > 256 ? 256 : remaining;
      ret = process(play, zeros, count, target);
      remaining -= count;
    }
  if (!ret && play->output_samples != target) ret = -EIO;
  if (!ret && play->frame_size)
    {
      memset(play->frame + play->frame_size, 0,
              play->turn->limits.audio_frame_bytes - play->frame_size);
      ret = emit_frame(play);
    }
  if (!ret) ret = sequence(play);
  if (!ret) ret = bkvoice_turn_tts_end(play->turn, &play->token);
  if (ret) bkcloud_playback_abort(play, ret);
  else release_filter(play);
  return ret;
}
