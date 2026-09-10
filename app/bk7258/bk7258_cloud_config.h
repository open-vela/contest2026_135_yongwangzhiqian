/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_CONFIG_H
#define __APP_BK7258_CLOUD_CONFIG_H
#include <stddef.h>
#include <stdint.h>

#define BKCLOUD_KEY_MAX 4096u
#define BKCLOUD_NAME_MAX 127u
#define BKCLOUD_CONFIG_MAX (24u + BKCLOUD_KEY_MAX + 5u * BKCLOUD_NAME_MAX)

/* CCF1 is a secret provisioning payload, not a readable status record.
 * Header (network byte order): magic[4], dialect[1], reserved[1], port[2],
 * six lengths[2 each] (host, base_path, key, ASR, chat, TTS), reserved[4].
 * Followed by the six strings without NUL. HTTPS is mandatory.
 * Dialect 1 = OpenAI Chat Completions audio; 2 = MiMo extensions.
 * Declaring a dialect does not imply its runtime adapter is installed.
 */
struct bkcloud_config_s
{
  uint8_t dialect;
  uint16_t port;
  char host[128];
  char base_path[128];
  char api_key[BKCLOUD_KEY_MAX + 1u];
  char asr_model[128];
  char chat_model[128];
  char tts_model[128];
};

/* Clear output on error; record must not alias output. */
int bkcloud_config_decode(struct bkcloud_config_s *config,
                          const void *record, size_t size);
void bkcloud_config_clear(struct bkcloud_config_s *config);
#endif
