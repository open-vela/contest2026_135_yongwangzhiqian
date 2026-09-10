/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_cloud_history.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>

int bkcloud_history_encode(const struct bkcloud_history_s *history,
                           unsigned persona, void *bytes, size_t capacity,
                           size_t *used)
{
  uint8_t *out = bytes;
  size_t sizes[BKCLOUD_HISTORY_TURNS][2], need = 8;
  if (used) *used = 0;
  if (!history || !bytes || !used || persona > 4 ||
      history->count > BKCLOUD_HISTORY_TURNS) return -EINVAL;
  for (size_t i = 0; i < history->count; i++)
    {
      sizes[i][0] = strnlen(history->turns[i].user, BKCLOUD_TEXT_MAX + 1);
      sizes[i][1] = strnlen(history->turns[i].assistant, BKCLOUD_TEXT_MAX + 1);
      for (unsigned j = 0; j < 2; j++)
        {
          if (!sizes[i][j] || sizes[i][j] > BKCLOUD_TEXT_MAX) return -EINVAL;
          need += 2 + sizes[i][j];
        }
    }
  if (capacity < need) return -ENOSPC;
  memcpy(out, "SMH1", 4);
  out[4] = (uint8_t)history->count; out[5] = persona; out[6] = 0; out[7] = 0;
  size_t pos = 8;
  for (size_t i = 0; i < history->count; i++)
    for (unsigned j = 0; j < 2; j++)
      {
        size_t n = sizes[i][j];
        out[pos++] = n >> 8; out[pos++] = n;
        memcpy(out + pos, j ? history->turns[i].assistant : history->turns[i].user, n);
        pos += n;
      }
  *used = pos;
  return 0;
}

int bkcloud_history_decode(struct bkcloud_history_s *history,
                           unsigned persona, const void *bytes, size_t size)
{
  const uint8_t *input = bytes;
  size_t pos = 8;
  if (!history) return -EINVAL;
  mbedtls_platform_zeroize(history, sizeof(*history));
  if (!input || size < 8 || size > BKCLOUD_HISTORY_BYTES || persona > 4 ||
      memcmp(input, "SMH1", 4) || input[4] > BKCLOUD_HISTORY_TURNS ||
      input[5] != persona || input[6] || input[7]) return -EBADMSG;
  for (size_t i = 0; i < input[4]; i++)
    for (unsigned j = 0; j < 2; j++)
      {
        if (size - pos < 2) goto bad;
        size_t n = (size_t)input[pos] << 8 | input[pos + 1]; pos += 2;
        if (!n || n > BKCLOUD_TEXT_MAX || n > size - pos ||
            memchr(input + pos, 0, n)) goto bad;
        memcpy(j ? history->turns[i].assistant : history->turns[i].user, input + pos, n);
        pos += n;
      }
  if (pos != size) goto bad;
  history->count = input[4];
  return 0;
bad:
  mbedtls_platform_zeroize(history, sizeof(*history));
  return -EBADMSG;
}
