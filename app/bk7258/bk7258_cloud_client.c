/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_client.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <mbedtls/platform_util.h>
#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include <cJSON.h>
#endif

int bkcloud_recognize(struct bkcloud_client_s *client,
                     const struct bkcloud_config_s *config,
                     const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                     uint64_t deadline_ms, const uint8_t *pcm, size_t pcm_size,
                     char *text, size_t capacity)
{
  int ret;
  if (text == NULL || capacity == 0) return -EINVAL;
  memset(text, 0, capacity);
  if (client == NULL || config == NULL) return -EINVAL;
  memset(client, 0, sizeof(*client));
  /* These two configured dialects currently share Chat Completions audio
   * input. Other providers must add their actual request/response adapter.
   */
  if (config->dialect != 1 && config->dialect != 2) return -ENOTSUP;
  ret = bkcloud_asr_source_init(&client->source, config->asr_model, pcm,
                               pcm_size);
  if (ret == 0)
    ret = bkcloud_http_post(&client->http, config, "chat/completions", tls,
                            tls_context, deadline_ms, bkcloud_asr_body,
                            &client->source, client->source.length,
                            client->response, sizeof(client->response));
  if (ret == 0)
    ret = bkcloud_text_parse(client->response, client->http.received,
                             text, capacity);
  bkcloud_asr_source_clear(&client->source);
  mbedtls_platform_zeroize(client->response, sizeof(client->response));
  return ret;
}

static bool valid_text(const char *text)
{
  if (text == NULL) return false;
  size_t size = strnlen(text, BKCLOUD_TEXT_MAX + 1);
  return size > 0 && size <= BKCLOUD_TEXT_MAX;
}

void bkcloud_history_clear(struct bkcloud_history_s *history)
{
  if (history != NULL) mbedtls_platform_zeroize(history, sizeof(*history));
}

int bkcloud_history_commit(struct bkcloud_history_s *history,
                            const char *user, const char *assistant)
{
  if (history == NULL || history->count > BKCLOUD_HISTORY_TURNS ||
      !valid_text(user) || !valid_text(assistant)) return -EINVAL;
  /* Inputs must not alias history: the owner passes pending-turn storage. */
  if (history->count == BKCLOUD_HISTORY_TURNS)
    {
      memmove(history->turns, history->turns + 1,
              sizeof(history->turns[0]) * (BKCLOUD_HISTORY_TURNS - 1));
      history->count--;
    }
  size_t index = history->count++;
  mbedtls_platform_zeroize(&history->turns[index], sizeof(history->turns[index]));
  memcpy(history->turns[index].user, user, strlen(user));
  memcpy(history->turns[index].assistant, assistant, strlen(assistant));
  return 0;
}

static bool add_message(cJSON *messages, const char *role, const char *content)
{
  cJSON *message = cJSON_CreateObject();
  if (message == NULL) return false;
  if (!cJSON_AddStringToObject(message, "role", role) ||
      !cJSON_AddStringToObject(message, "content", content) ||
      !cJSON_AddItemToArray(messages, message))
    { cJSON_Delete(message); return false; }
  return true;
}

static bool add_image_message(cJSON *messages, const char *prompt,
                              const char *marker)
{
  cJSON *message = cJSON_CreateObject();
  cJSON *content = cJSON_CreateArray();
  cJSON *part = cJSON_CreateObject();
  cJSON *image_part = cJSON_CreateObject();
  cJSON *image = cJSON_CreateObject();
  char url[64];
  int length = snprintf(url, sizeof(url), "data:image/jpeg;base64,%s", marker);
  if (length < 0 || (size_t)length >= sizeof(url) || message == NULL ||
      content == NULL || part == NULL || image_part == NULL || image == NULL ||
      !cJSON_AddStringToObject(message, "role", "user") ||
      !cJSON_AddStringToObject(part, "type", "text") ||
      !cJSON_AddStringToObject(part, "text", prompt) ||
      !cJSON_AddStringToObject(image_part, "type", "image_url") ||
      !cJSON_AddStringToObject(image, "url", url) ||
      !cJSON_AddItemToObject(image_part, "image_url", image)) goto fail;
  image = NULL;
  if (!cJSON_AddItemToArray(content, part)) goto fail;
  part = NULL;
  if (!cJSON_AddItemToArray(content, image_part)) goto fail;
  image_part = NULL;
  if (!cJSON_AddItemToObject(message, "content", content)) goto fail;
  content = NULL;
  if (!cJSON_AddItemToArray(messages, message)) goto fail;
  return true;
fail:
  cJSON_Delete(message);
  cJSON_Delete(content);
  cJSON_Delete(part);
  cJSON_Delete(image_part);
  cJSON_Delete(image);
  return false;
}

int bkcloud_chat(struct bkcloud_client_s *client,
                 const struct bkcloud_config_s *config,
                 const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                 uint64_t deadline_ms, const char *persona,
                 const struct bkcloud_history_s *history, const char *input,
                 char *text, size_t capacity)
{
  cJSON *root = NULL, *messages;
  int ret = -ENOMEM;
  if (text == NULL || capacity == 0) return -EINVAL;
  memset(text, 0, capacity);
  if (client == NULL || config == NULL || !valid_text(persona) ||
      !valid_text(input) || history == NULL ||
      history->count > BKCLOUD_HISTORY_TURNS) return -EINVAL;
  if (config->dialect != 1 && config->dialect != 2) return -ENOTSUP;
  for (size_t i = 0; i < history->count; i++)
    if (!valid_text(history->turns[i].user) ||
        !valid_text(history->turns[i].assistant)) return -EINVAL;
  memset(client, 0, sizeof(*client));
  root = cJSON_CreateObject();
  if (root == NULL) goto out;
  messages = cJSON_AddArrayToObject(root, "messages");
  if (messages == NULL ||
      !cJSON_AddStringToObject(root, "model", config->chat_model) ||
      !cJSON_AddBoolToObject(root, "stream", false) ||
      !cJSON_AddNumberToObject(root, config->dialect == 2 ?
        "max_completion_tokens" : "max_tokens", 1024) ||
      !add_message(messages, "system", persona)) goto out;
  if (config->dialect == 2)
    {
      cJSON *thinking = cJSON_AddObjectToObject(root, "thinking");
      if (thinking == NULL ||
          !cJSON_AddStringToObject(thinking, "type", "disabled")) goto out;
    }
  for (size_t i = 0; i < history->count; i++)
    if (!add_message(messages, "user", history->turns[i].user) ||
        !add_message(messages, "assistant", history->turns[i].assistant)) goto out;
  if (!add_message(messages, "user", input)) goto out;
  if (!cJSON_PrintPreallocated(root, client->request, sizeof(client->request), false))
    { ret = -E2BIG; goto out; }
  cJSON_Delete(root); root = NULL;
  struct webclient_context body;
  webclient_set_defaults(&body);
  webclient_set_static_body(&body, client->request, strlen(client->request));
  ret = bkcloud_http_post(&client->http, config, "chat/completions", tls,
                          tls_context, deadline_ms, body.body_callback,
                          body.body_callback_arg, body.bodylen,
                          client->response, sizeof(client->response));
  if (ret == 0)
    ret = bkcloud_text_parse(client->response, client->http.received, text, capacity);
out:
  cJSON_Delete(root);
  mbedtls_platform_zeroize(client->request, sizeof(client->request));
  mbedtls_platform_zeroize(client->response, sizeof(client->response));
  return ret;
}

int bkcloud_understand_jpeg(struct bkcloud_client_s *client,
                            const struct bkcloud_config_s *config,
                            const struct bkvoice_wss_tls_ops_s *tls,
                            void *tls_context, uint64_t deadline_ms,
                            const char *persona,
                            const struct bkcloud_history_s *history,
                            const char *prompt, const uint8_t *jpeg,
                            size_t jpeg_size, char *text, size_t capacity)
{
  static const char image_prefix[] = "\"url\":\"data:image/jpeg;base64,";
  struct bkcloud_image_source_s source;
  cJSON *root = NULL;
  cJSON *messages;
  char *location;
  int ret = -ENOMEM;
  if (text == NULL || capacity == 0) return -EINVAL;
  memset(text, 0, capacity);
  if (client == NULL) return -EINVAL;
  memset(client, 0, sizeof(*client));
  if (config == NULL || !valid_text(persona) ||
      !valid_text(prompt) || history == NULL || history->count > BKCLOUD_HISTORY_TURNS)
    return -EINVAL;
  if (config->dialect != 1 && config->dialect != 2) return -ENOTSUP;
  for (size_t i = 0; i < history->count; i++)
    if (!valid_text(history->turns[i].user) || !valid_text(history->turns[i].assistant))
      return -EINVAL;
  root = cJSON_CreateObject();
  if (root == NULL) goto out;
  messages = cJSON_AddArrayToObject(root, "messages");
  if (messages == NULL || !cJSON_AddStringToObject(root, "model", config->chat_model) ||
      !cJSON_AddBoolToObject(root, "stream", false) ||
      !cJSON_AddNumberToObject(root, config->dialect == 2 ?
        "max_completion_tokens" : "max_tokens", 1024) ||
      !add_message(messages, "system", persona)) goto out;
  if (config->dialect == 2)
    {
      cJSON *thinking = cJSON_AddObjectToObject(root, "thinking");
      if (thinking == NULL || !cJSON_AddStringToObject(thinking, "type", "disabled")) goto out;
    }
  for (size_t i = 0; i < history->count; i++)
    if (!add_message(messages, "user", history->turns[i].user) ||
        !add_message(messages, "assistant", history->turns[i].assistant)) goto out;
  if (!add_image_message(messages, prompt, "") ||
      !cJSON_PrintPreallocated(root, client->request, sizeof(client->request), false))
    { ret = -E2BIG; goto out; }
  location = strstr(client->request, image_prefix);
  if (location == NULL ||
      strstr(location + sizeof(image_prefix) - 1, image_prefix) != NULL)
    { ret = -EBADMSG; goto out; }
  ret = bkcloud_image_source_init(&source, client->request,
                                  (size_t)(location - client->request) +
                                  sizeof(image_prefix) - 1,
                                  location + sizeof(image_prefix) - 1,
                                  strlen(location + sizeof(image_prefix) - 1),
                                  jpeg, jpeg_size);
  if (ret == 0)
    ret = bkcloud_http_post(&client->http, config, "chat/completions", tls,
                            tls_context, deadline_ms, bkcloud_image_body, &source,
                            source.length, client->response, sizeof(client->response));
  if (ret == 0)
    ret = bkcloud_text_parse(client->response, client->http.received, text, capacity);
  bkcloud_image_source_clear(&source);
out:
  cJSON_Delete(root);
  if (ret != 0) memset(text, 0, capacity);
  mbedtls_platform_zeroize(client->request, sizeof(client->request));
  mbedtls_platform_zeroize(client->response, sizeof(client->response));
  return ret;
}

static int synthesize_pcm(struct bkcloud_client_s *client,
                          const struct bkcloud_config_s *config,
                          const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                          uint64_t deadline_ms, const char *text,
                          bkcloud_write_t pcm, void *context)
{
  int ret = -ENOMEM;
  memset(client, 0, sizeof(*client));
  cJSON *root = cJSON_CreateObject();
  if (!root) return ret;
  /* CCF1 has no voice selector. Use the standard built-in voice explicitly;
   * custom-provider voices require a future versioned configuration field.
   */
  if (!cJSON_AddStringToObject(root, "model", config->tts_model) ||
      !cJSON_AddStringToObject(root, "input", text) ||
      !cJSON_AddStringToObject(root, "voice", "alloy") ||
      !cJSON_AddStringToObject(root, "response_format", "pcm")) goto done;
  if (!cJSON_PrintPreallocated(root, client->request, sizeof(client->request), false))
    { ret = -E2BIG; goto done; }
  ret = bkcloud_http_pcm(&client->http, config, tls, tls_context, deadline_ms,
      client->request, strlen(client->request), pcm, context, 8u * 1024u * 1024u);
done:
  cJSON_Delete(root);
  mbedtls_platform_zeroize(client->request, sizeof(client->request));
  return ret;
}

int bkcloud_synthesize(struct bkcloud_client_s *client,
                       struct bkcloud_tts_s *decoder,
                       const struct bkcloud_config_s *config,
                       const struct bkvoice_wss_tls_ops_s *tls, void *tls_context,
                       uint64_t deadline_ms, const char *text,
                       bkcloud_write_t pcm, void *context)
{
  cJSON *root = NULL, *messages, *audio;
  int ret = -ENOMEM;
  if (!client || !decoder || !config || !pcm || !valid_text(text)) return -EINVAL;
  if (config->dialect == 1)
    return synthesize_pcm(client, config, tls, tls_context, deadline_ms, text, pcm, context);
  if (config->dialect != 2) return -ENOTSUP;
  memset(client, 0, sizeof(*client));
  bkcloud_tts_init(decoder, pcm, context);
  root = cJSON_CreateObject();
  if (!root) goto out;
  messages = cJSON_AddArrayToObject(root, "messages");
  audio = cJSON_AddObjectToObject(root, "audio");
  if (!messages || !audio ||
      !cJSON_AddStringToObject(root, "model", config->tts_model) ||
      !cJSON_AddBoolToObject(root, "stream", true) ||
      !cJSON_AddStringToObject(audio, "format", "pcm16") ||
      !cJSON_AddStringToObject(audio, "voice", "mimo_default") ||
      !add_message(messages, "assistant", text)) goto out;
  if (!cJSON_PrintPreallocated(root, client->request, sizeof(client->request), false))
    { ret = -E2BIG; goto out; }
  cJSON_Delete(root); root = NULL;
  ret = bkcloud_http_events(&client->http, config, tls, tls_context, deadline_ms,
                            client->request, strlen(client->request),
                            bkcloud_tts_feed, decoder, 8u * 1024u * 1024u);
  if (ret == 0) ret = bkcloud_tts_finish(decoder);
out:
  cJSON_Delete(root);
  bkcloud_tts_clear(decoder);
  mbedtls_platform_zeroize(client->request, sizeof(client->request));
  return ret;
}

struct playback_sink_s
{
  struct bkcloud_playback_s *play;
  struct bkvoice_turn_s *turn;
  uint64_t (*now_ms)(void *);
  void *clock_context;
  bool started;
};
static int playback_sink(void *context, const void *pcm, size_t size)
{
  struct playback_sink_s *sink = context;
  if (!sink->started)
    {
      int ret = bkcloud_playback_begin(sink->play, sink->turn, sink->now_ms,
                                       sink->clock_context);
      if (ret) return ret;
      sink->started = true;
    }
  return bkcloud_playback_feed(sink->play, pcm, size);
}
int bkcloud_synthesize_turn(struct bkcloud_client_s *client,
                            struct bkcloud_tts_s *decoder,
                            struct bkcloud_playback_s *play,
                            struct bkvoice_turn_s *turn,
                            const struct bkcloud_config_s *config,
                            const struct bkvoice_wss_tls_ops_s *tls,
                            void *tls_context, uint64_t deadline_ms,
                            uint64_t (*now_ms)(void *), void *clock_context,
                            const char *text)
{
  if (!play || !turn || !now_ms || turn->state != BKVOICE_TURN_WAITING_TTS)
    return -EINVAL;
  struct playback_sink_s sink = {play, turn, now_ms, clock_context, false};
  int ret = bkcloud_synthesize(client, decoder, config, tls, tls_context,
                                deadline_ms, text, playback_sink, &sink);
  if (ret == 0 && sink.started) ret = bkcloud_playback_end(play);
  else if (ret == 0) ret = -ENODATA;
  if (ret && sink.started) bkcloud_playback_abort(play, ret);
  else if (ret)
    {
      struct bkvoice_turn_token_s token = turn->active;
      token.sequence = turn->last_control_sequence + 1;
      (void)bkvoice_turn_cancel(turn, &token, ret);
    }
  return ret;
}
