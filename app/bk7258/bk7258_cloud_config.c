/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_config.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>

void bkcloud_config_clear(struct bkcloud_config_s *config)
{
  if (config != NULL)
    {
      volatile uint8_t *p = (volatile uint8_t *)config;
      for (size_t i = 0; i < sizeof(*config); i++) p[i] = 0;
    }
}

static bool alnum(uint8_t c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}

static bool host_valid(const uint8_t *p, size_t size)
{
  size_t label = 0;
  if (!alnum(p[0]) || !alnum(p[size - 1])) return false;
  for (size_t i = 0; i < size; i++)
    {
      if (p[i] == '.')
        {
          if (label == 0 || !alnum(p[i - 1]) ||
              i + 1 == size || !alnum(p[i + 1])) return false;
          label = 0;
        }
      else if ((!alnum(p[i]) && p[i] != '-') || ++label > 63)
        return false;
    }
  return true;
}

int bkcloud_config_decode(struct bkcloud_config_s *config,
                          const void *record, size_t size)
{
  const uint8_t *p = record;
  size_t lengths[6], total = 24, offset;
  if (config == NULL) return -EINVAL;
  bkcloud_config_clear(config);
  if (p == NULL || size < 24 || size > BKCLOUD_CONFIG_MAX ||
      memcmp(p, "CCF1", 4) || (p[4] != 1 && p[4] != 2) || p[5] ||
      (p[6] == 0 && p[7] == 0)) return -EBADMSG;
  for (size_t i = 20; i < 24; i++) if (p[i]) return -EBADMSG;
  for (size_t i = 0; i < 6; i++)
    {
      lengths[i] = ((size_t)p[8 + 2 * i] << 8) | p[9 + 2 * i];
      if (lengths[i] == 0 || lengths[i] >
          (i == 2 ? BKCLOUD_KEY_MAX : BKCLOUD_NAME_MAX)) return -EBADMSG;
      total += lengths[i];
    }
  if (total != size || !host_valid(p + 24, lengths[0])) return -EBADMSG;
  offset = 24 + lengths[0];
  if (p[offset] != '/') return -EBADMSG;
  for (size_t i = 1; i < 6; i++)
    {
      for (size_t j = 0; j < lengths[i]; j++)
        {
          uint8_t c = p[offset + j];
          if (i == 2)
            { if (c < 33 || c > 126) return -EBADMSG; }
          else if (!alnum(c) && c != '-' && c != '_' && c != '/' &&
                   c != '.' && (i == 1 || c != ':')) return -EBADMSG;
          if (i == 1 && j > 0 && c == '.' && p[offset + j - 1] == '.')
            return -EBADMSG;
        }
      offset += lengths[i];
    }
  char *fields[] = {config->host, config->base_path, config->api_key,
                    config->asr_model, config->chat_model, config->tts_model};
  offset = 24;
  for (size_t i = 0; i < 6; i++)
    { memcpy(fields[i], p + offset, lengths[i]); offset += lengths[i]; }
  config->dialect = p[4];
  config->port = ((uint16_t)p[6] << 8) | p[7];
  return 0;
}
