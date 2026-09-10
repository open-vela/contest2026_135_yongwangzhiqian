/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_NUTTX_AUDIO_AUDIO_H
#define __MOCK_NUTTX_AUDIO_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define AUDIOIOC_GETCAPS       1
#define AUDIOIOC_RESERVE       2
#define AUDIOIOC_RELEASE       3
#define AUDIOIOC_CONFIGURE     4
#define AUDIOIOC_SHUTDOWN      5
#define AUDIOIOC_START         6
#define AUDIOIOC_STOP          7
#define AUDIOIOC_GETBUFFERINFO 8
#define AUDIOIOC_ALLOCBUFFER   9
#define AUDIOIOC_FREEBUFFER    10
#define AUDIOIOC_ENQUEUEBUFFER 11
#define AUDIOIOC_REGISTERMQ    12
#define AUDIOIOC_UNREGISTERMQ  13

#define AUDIO_TYPE_QUERY 1
#define AUDIO_TYPE_INPUT 2
#define AUDIO_FMT_PCM    3

#define AUDIO_APB_FINAL (1u << 0)

#define AUDIO_MSG_DEQUEUE 1
#define AUDIO_MSG_COMPLETE 2
#define AUDIO_MSG_IOERR 3
#define AUDIO_MSG_STOP 4

struct ap_buffer_s
{
  uint8_t *samp;
  size_t nmaxbytes;
  size_t nbytes;
  size_t curbyte;
  size_t nsamples;
  uint16_t flags;
};

struct audio_buf_desc_s
{
  size_t numbytes;
  union
  {
    struct ap_buffer_s *buffer;
    struct ap_buffer_s **pbuffer;
  } u;
};

struct audio_caps_s
{
  uint8_t ac_len;
  uint8_t ac_type;
  uint8_t ac_subtype;
  uint8_t ac_channels;
  union
  {
    uint8_t b[2];
    uint16_t hw;
  } ac_format;
  union
  {
    uint8_t b[8];
    uint16_t hw[4];
  } ac_controls;
};

struct audio_caps_desc_s
{
  struct audio_caps_s caps;
};

struct ap_buffer_info_s
{
  size_t buffer_size;
  size_t nbuffers;
};

struct audio_msg_s
{
  uint16_t msg_id;
  union
  {
    void *ptr;
  } u;
};

#endif /* __MOCK_NUTTX_AUDIO_AUDIO_H */
