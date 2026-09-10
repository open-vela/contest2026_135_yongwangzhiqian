/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_client.h"
#include "bk7258_voice_tls.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <mbedtls/platform_util.h>
#include <time.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct peer
{
  const char *reply;
  size_t offset;
  char request[8192];
  size_t sent;
  unsigned int opens;
  unsigned int closes;
  int failure;
};
static int open_peer(void *arg, const char *host, uint16_t port, uint64_t due)
{
  struct peer *p = arg;
  assert(strcmp(host, "cloud.example") == 0 && port == 443 && due == 123456);
  p->opens++;
  return 0;
}
static ssize_t send_peer(void *arg, const uint8_t *data, size_t size, uint64_t due)
{
  struct peer *p = arg;
  assert(due == 123456);
  if (p->failure) return p->failure;
  if (size > 7) size = 7; /* Exercise partial headers and request bodies. */
  assert(p->sent + size < sizeof(p->request));
  memcpy(p->request + p->sent, data, size);
  p->sent += size;
  return size;
}
static ssize_t recv_peer(void *arg, uint8_t *data, size_t size, uint64_t due)
{
  struct peer *p = arg;
  assert(due == 123456);
  size_t left = strlen(p->reply) - p->offset;
  if (size > left) size = left;
  if (size > 3) size = 3; /* Split every HTTP construct across reads. */
  memcpy(data, p->reply + p->offset, size);
  p->offset += size;
  return size;
}
static int close_peer(void *arg)
{ ((struct peer *)arg)->closes++; return 0; }
static const struct bkvoice_wss_tls_ops_s ops =
{.open_verified=open_peer, .send=send_peer, .recv=recv_peer, .close=close_peer};
static int body(void *buffer, size_t *size, const void **data,
                size_t requested, void *context)
{
  (void)context;
  assert(*size >= 2 && requested == 2);
  memcpy(buffer, "{}", 2); *data = buffer; *size = 2;
  return 0;
}
static void run(const char *reply, int error, int failure, size_t capacity)
{
  struct bkcloud_http_s *http = calloc(1, sizeof(*http));
  struct bkcloud_config_s config = {.port=443};
  strcpy(config.host, "cloud.example"); strcpy(config.base_path, "/v1");
  strcpy(config.api_key, "test-only-key");
  struct peer p = {.reply=reply, .failure=failure};
  char output[128]; memset(output, 'x', sizeof(output));
  assert(bkcloud_http_post(http, &config, "chat/completions", &ops, &p,
    123456, body, NULL, 2, output, capacity) == error);
  assert(p.opens == 1 && p.closes == 1);
  assert(!http->connected && http->config == NULL && http->response == NULL);
  for (size_t i=0; i<sizeof(http->authorization); i++) assert(!http->authorization[i]);
  for (size_t i=0; i<sizeof(http->buffer); i++) assert(!http->buffer[i]);
  if (error == 0)
    {
      assert(strcmp(output, "{}") == 0 && http->received == 2);
      assert(strstr(p.request, "POST /v1/chat/completions HTTP/1.1\r\n"));
      assert(strstr(p.request, "Authorization: Bearer test-only-key\r\n"));
      assert(strstr(p.request, "\r\n\r\n{}"));
    }
  else
    for (size_t i=0; i<capacity; i++) assert(output[i] == 0);
  free(http);
}
static void recognize(void)
{
  const char *json = "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\","
    "\"message\":{\"content\":\"hello\"}}]}";
  char reply[512];
  snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",
            strlen(json), json);
  struct bkcloud_client_s *client = calloc(1, sizeof(*client));
  struct bkcloud_config_s config = {.port=443, .dialect=1};
  strcpy(config.host, "cloud.example"); strcpy(config.base_path, "/v1");
  strcpy(config.api_key, "test-only-key"); strcpy(config.asr_model, "vendor/asr");
  struct peer p = {.reply=reply};
  uint8_t pcm[4] = {0, 1, 2, 3};
  char text[64];
  assert(bkcloud_recognize(client, &config, &ops, &p, 123456,
                           pcm, sizeof(pcm), text, sizeof(text)) == 0);
  assert(strcmp(text, "hello") == 0 && p.opens == 1 && p.closes == 1);
  assert(strstr(p.request, "data:audio/wav;base64,UklGR"));
  for (size_t i=0; i<sizeof(client->response); i++) assert(!client->response[i]);
  config.dialect = 99;
  assert(bkcloud_recognize(client, &config, &ops, &p, 123456,
                           pcm, sizeof(pcm), text, sizeof(text)) == -ENOTSUP);
  assert(text[0] == 0 && p.opens == 1);
  free(client);
}
static void understand(void)
{
  const char *json = "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\","
    "\"message\":{\"content\":\"image reply\"}}]}";
  char reply[512];
  snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",
           strlen(json), json);
  struct bkcloud_client_s *client = calloc(1, sizeof(*client));
  struct bkcloud_config_s config = {.port=443, .dialect=2};
  struct bkcloud_history_s history = {.count=1};
  struct peer p = {.reply=reply};
  uint8_t jpeg[] = {0xff, 0xd8, 1, 2, 3, 0xff, 0xd9};
  char text[64];
  assert(client != NULL);
  strcpy(config.host, "cloud.example"); strcpy(config.base_path, "/v1");
  strcpy(config.api_key, "test-only-key"); strcpy(config.chat_model, "mimo-v2.5");
  strcpy(history.turns[0].user, "__BKCLOUD_JPEG_BASE64__");
  strcpy(history.turns[0].assistant, "{\"url\":\"data:image/jpeg;base64,");
  assert(bkcloud_understand_jpeg(client, &config, &ops, &p, 123456,
         "persona __BKCLOUD_JPEG_BASE64__", &history,
         "describe {\"url\":\"data:image/jpeg;base64, __BKCLOUD_JPEG_BASE64__",
         jpeg, sizeof(jpeg), text,
         sizeof(text)) == 0);
  assert(!strcmp(text, "image reply") && p.opens == 1 && p.closes == 1);
  assert(strstr(p.request, "\"thinking\":{\"type\":\"disabled\"}"));
  assert(strstr(p.request, "__BKCLOUD_JPEG_BASE64__") &&
         strstr(p.request, "data:image/jpeg;base64,/9gBAgP/2Q=="));
  config.dialect = 1;
  p = (struct peer){.reply=reply};
  assert(bkcloud_understand_jpeg(client, &config, &ops, &p, 123456,
         "persona", &history, "describe image", jpeg, sizeof(jpeg), text,
         sizeof(text)) == 0 && !strcmp(text, "image reply"));
  p = (struct peer){.reply="HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\n{}"};
  memset(text, 'x', sizeof(text));
  assert(bkcloud_understand_jpeg(client, &config, &ops, &p, 123456,
         "persona", &history, "describe image", jpeg, sizeof(jpeg), text,
         sizeof(text)) == -EACCES);
  assert(text[0] == 0 && p.opens == 1 && p.closes == 1);
  p = (struct peer){.reply="", .failure=-ECANCELED};
  memset(text, 'x', sizeof(text));
  assert(bkcloud_understand_jpeg(client, &config, &ops, &p, 123456,
         "persona", &history, "describe image", jpeg, sizeof(jpeg), text,
         sizeof(text)) == -ECANCELED);
  assert(text[0] == 0 && p.opens == 1 && p.closes == 1);
  memset(text, 'x', sizeof(text));
  jpeg[0] = 0;
  assert(bkcloud_understand_jpeg(client, &config, &ops, &p, 123456,
         "persona", &history, "describe image", jpeg, sizeof(jpeg), text,
         sizeof(text)) == -EINVAL);
  assert(text[0] == 0 && p.opens == 1);
  for (size_t i = 0; i < sizeof(client->request); i++) assert(!client->request[i]);
  free(client);
}
static int pcm_output(void *context, const void *data, size_t size)
{
  size_t *total = context;
  const uint8_t expected[] = {0, 1, 2, 3};
  assert(size == sizeof(expected) && memcmp(data, expected, size) == 0);
  *total += size;
  return 0;
}
static int pcm_cancel(void *context, const void *data, size_t size)
{ (void)context; (void)data; (void)size; return -ECANCELED; }
static int pcm_count(void *context, const void *data, size_t size)
{ (void)data; *(size_t *)context += size; return 0; }
static int pcm_fragment(void *context, const void *data, size_t size)
{
 size_t *total = context;
 for (size_t i = 0; i < size; i++) assert(((const uint8_t *)data)[i] == *total + i);
 *total += size; return 0;
}
static void pcm_responses(void)
{
 struct bkcloud_http_s *http = calloc(1, sizeof(*http));
 struct bkcloud_config_s config = {.port=443, .dialect=1};
 strcpy(config.host, "cloud.example"); strcpy(config.base_path, "/v1");
 strcpy(config.api_key, "test-only-key");
 const char *responses[] = {
   "HTTP/1.1 200 OK\r\nContent-Type: audio/pcm\r\nContent-Length: 4\r\n\r\nABCD",
   "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nA\r\n3\r\nBCD\r\n0\r\n\r\n",
   "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
   "HTTP/1.1 200 OK\r\nContent-Type: audio/pcm\r\nContent-Length: 3\r\n\r\nABC",
   "HTTP/1.1 200 OK\r\nContent-Type: audio/pcm\r\nContent-Length: 0\r\n\r\n",
   "HTTP/1.1 401 Unauthorized\r\nContent-Type: audio/pcm\r\nContent-Length: 2\r\n\r\nAB",
   "HTTP/1.1 200 OK\r\nContent-Type: audio/pcm\r\nContent-Length: 8\r\n\r\nABCD",
   "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 4\r\n\r\nABCD"
 };
 for (unsigned i = 0; i < sizeof(responses) / sizeof(responses[0]); i++)
   {
     struct peer p = {.reply=responses[i]}; size_t total = 0;
     int ret = bkcloud_http_pcm(http, &config, &ops, &p, 123456, "{}", 2, pcm_count, &total, 100);
     if (i < 2) assert(ret == 0 && total == 4);
     else assert(ret < 0);
     if (i == 2 || i == 4 || i == 5 || i == 7) assert(total == 0);
     assert(p.closes == 1 && strstr(p.request, "POST /v1/audio/speech "));
   }
 struct peer p = {.reply=responses[0]}; size_t total = 0;
 assert(bkcloud_http_pcm(http, &config, &ops, &p, 123456, "{}", 2, pcm_cancel, &total, 100) == -ECANCELED);
 p = (struct peer){.reply=responses[0]};
 assert(bkcloud_http_pcm(http, &config, &ops, &p, 123456, "{}", 2, pcm_count, &total, 2) == -E2BIG);
 free(http);
}
static void stream_errors(void)
{
  struct bkcloud_http_s *http = calloc(1, sizeof(*http));
  struct bkcloud_config_s config = {.port=443, .dialect=2};
  strcpy(config.host, "cloud.example"); strcpy(config.base_path, "/v1");
  strcpy(config.api_key, "test-only-key");
  size_t delivered = 0;
  struct peer p = {.reply="HTTP/1.1 401 Unauthorized\r\nContent-Type: text/event-stream\r\nContent-Length: 2\r\n\r\n{}"};
  assert(bkcloud_http_events(http, &config, &ops, &p, 123456, "{}", 2,
                              pcm_output, &delivered, 100) == -EACCES);
  assert(delivered == 0 && p.closes == 1);
  p = (struct peer){.reply="HTTP/1.1 403 Forbidden\r\nContent-Type: text/event-stream\r\nContent-Length: 4\r\n\r\nFAIL"};
  assert(bkcloud_http_events(http, &config, &ops, &p, 123456, "{}", 2,
                              pcm_output, &delivered, 1) == -EACCES);
  assert(delivered == 0 && http->received == 0 && p.closes == 1);
  p = (struct peer){.reply="HTTP/1.1 401 Unauthorized\r\nContent-Type: audio/pcm\r\nContent-Length: 4\r\n\r\nFAIL"};
  assert(bkcloud_http_pcm(http, &config, &ops, &p, 123456, "{}", 2,
                          pcm_output, &delivered, 1) == -EACCES);
  assert(delivered == 0 && http->received == 0 && p.closes == 1);
  memset(&p, 0, sizeof(p));
  p.reply="HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}";
  assert(bkcloud_http_events(http, &config, &ops, &p, 123456, "{}", 2,
                              pcm_output, &delivered, 100) == -EPROTO);
  assert(delivered == 0 && p.closes == 1);
  free(http);
}
static void tts_decoder(void)
{
  const char *events = "data: {\"choices\":[{\"index\":0,\"delta\":{\"audio\":{"
    "\"data\":\"AAECAw==\"}},\"finish_reason\":null}]}\r\n\r\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
    "data: [DONE]\n\n";
  struct bkcloud_tts_s *tts = calloc(1, sizeof(*tts));
  for (size_t chunk=1; chunk<strlen(events); chunk++)
    {
      size_t total = 0;
      bkcloud_tts_init(tts, pcm_output, &total);
      for (size_t i=0; i<strlen(events); i+=chunk)
        {
          size_t count = strlen(events)-i;
          if (count>chunk) count=chunk;
          assert(bkcloud_tts_feed(tts, events+i, count) == 0);
        }
      assert(bkcloud_tts_finish(tts) == 0 && total == 4);
      assert(bkcloud_tts_feed(tts, "data: [DONE]\n\n", 14) == -EPROTO);
    }
  size_t total = 0;
  bkcloud_tts_init(tts, pcm_output, &total);
  assert(bkcloud_tts_feed(tts, events, strlen(events)-1) == 0);
  assert(bkcloud_tts_finish(tts) == -EBADMSG);
  bkcloud_tts_init(tts, pcm_cancel, NULL);
  assert(bkcloud_tts_feed(tts, events, strlen(events)) == -ECANCELED);
  assert(bkcloud_tts_finish(tts) == -ECANCELED);
  bkcloud_tts_clear(tts);
  for (size_t i=0; i<sizeof(*tts); i++) assert(((uint8_t *)tts)[i] == 0);
  free(tts);
}
static uint64_t now_ms(void *context)
{
  struct timespec now; (void)context;
  assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int trusted_time(void *context) { (void)context; return 0; }
static int real_https(const char *ca_file, const char *port)
{
  mbedtls_x509_crt ca;
  mbedtls_x509_crt_init(&ca);
  assert(mbedtls_x509_crt_parse_file(&ca, ca_file) == 0);
  struct bkvoice_tls_s tls;
  struct bkvoice_tls_config_s transport = {
    .server_ca=&ca, .trusted_time=trusted_time, .now_ms=now_ms,
    .server_auth_only=true};
  assert(inet_pton(AF_INET, "127.0.0.1", &transport.peer_address) == 1);
  assert(bkvoice_tls_initialize(&tls, &transport) == 0);
  struct bkcloud_config_s config = {.port=atoi(port), .dialect=1};
  strcpy(config.host, "localhost"); strcpy(config.base_path, "/v1");
  strcpy(config.api_key, "test-only-key"); strcpy(config.asr_model, "vendor/asr");
  struct bkcloud_client_s *client = calloc(1, sizeof(*client));
  uint8_t pcm[4] = {0, 1, 2, 3};
  char text[64];
  int ret = bkcloud_recognize(client, &config, bkvoice_tls_ops(), &tls,
                             now_ms(NULL) + 5000, pcm, sizeof(pcm), text, sizeof(text));
  assert(ret == 0 && strcmp(text, "hello") == 0);
  struct bkcloud_history_s *history = calloc(1, sizeof(*history));
  strcpy(config.chat_model, "vendor/chat");
  assert(bkcloud_chat(client, &config, bkvoice_tls_ops(), &tls,
                      now_ms(NULL) + 5000, "persona \"line\"\n", history, "question 1",
                      text, sizeof(text)) == 0);
  assert(history->count == 0 && strcmp(text, "hello") == 0);
  assert(bkcloud_history_commit(history, "question 1", text) == 0);
  config.dialect = 2;
  assert(bkcloud_chat(client, &config, bkvoice_tls_ops(), &tls,
                      now_ms(NULL) + 5000, "persona \"line\"\n", history, "question 2",
                      text, sizeof(text)) == 0);
  assert(history->count == 1);
  assert(bkcloud_history_commit(history, "question 2", text) == 0);
  assert(bkcloud_history_commit(history, "question 3", text) == 0);
  assert(bkcloud_history_commit(history, "question 4", text) == 0);
  assert(history->count == 3 && strcmp(history->turns[0].user, "question 2") == 0);
  assert(bkcloud_history_commit(history, "", text) == -EINVAL);
  assert(history->count == 3);
  bkcloud_history_clear(history);
  for (size_t i=0; i<sizeof(*history); i++) assert(((uint8_t *)history)[i] == 0);
  free(history);
  strcpy(config.tts_model, "mimo-v2.5-tts");
  struct bkcloud_tts_s *decoder = calloc(1, sizeof(*decoder));
  size_t pcm_bytes = 0;
  assert(bkcloud_synthesize(client, decoder, &config, bkvoice_tls_ops(), &tls,
                            now_ms(NULL) + 5000, "hello", pcm_output,
                            &pcm_bytes) == 0);
  assert(pcm_bytes == 4);
  config.dialect = 1; strcpy(config.tts_model, "vendor/tts"); pcm_bytes = 0;
  assert(bkcloud_synthesize(client, decoder, &config, bkvoice_tls_ops(), &tls,
                            now_ms(NULL) + 5000, "hello", pcm_fragment, &pcm_bytes) == 0);
  assert(pcm_bytes == 4);
  free(decoder);
  assert(bkvoice_tls_uninitialize(&tls) == 0);
  mbedtls_x509_crt_free(&ca);
  free(client);
  return 0;
}
/* Explicit opt-in integration mode: CCF1 arrives on stdin, never argv or logs.
 * Reuses the target client, webclient and TLS implementation with host sockets.
 */
struct live_pcm_s {size_t bytes;unsigned int peak;FILE *output;uint8_t low;};
static int live_pcm(void *context,const void *data,size_t size)
{
  struct live_pcm_s *stats=context;
  const uint8_t *p=data;
  if(stats->output && fwrite(data,1,size,stats->output)!=size)return -EIO;
  for(size_t i=0;i<size;i++)
    {
      if ((stats->bytes+i)%2 == 0) { stats->low=p[i]; continue; }
      int sample=(int16_t)((uint16_t)stats->low | ((uint16_t)p[i]<<8));
      unsigned int amplitude=(unsigned int)(sample<0 ? -sample : sample);
      if(amplitude>stats->peak)stats->peak=amplitude;
      stats->low=0;
    }
  stats->bytes+=size;
  return 0;
}
static int live_https(const char *ca_file,const char *pcm_input,const char *pcm_output_file)
{
  uint8_t record[BKCLOUD_CONFIG_MAX];
  size_t size=fread(record,1,sizeof(record),stdin);
  struct bkcloud_config_s config;
  int ret=bkcloud_config_decode(&config,record,size);
  mbedtls_platform_zeroize(record,sizeof(record));
  if(ret)return 1;
  struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_STREAM};
  struct addrinfo *address=NULL;
  mbedtls_x509_crt ca;mbedtls_x509_crt_init(&ca);
  struct bkvoice_tls_s tls={0};
  struct bkcloud_client_s *client=calloc(1,sizeof(*client));
  struct bkcloud_tts_s *decoder=calloc(1,sizeof(*decoder));
  char text[BKCLOUD_TEXT_MAX+1]={0};
  if(!client || !decoder) {ret=-ENOMEM;goto done;}
  ret=getaddrinfo(config.host,NULL,&hints,&address);
  if(ret || !address) {ret=-EHOSTUNREACH;goto done;}
  ret=mbedtls_x509_crt_parse_file(&ca,ca_file);
  if(ret<0)goto done;
  struct bkvoice_tls_config_s transport={.server_ca=&ca,
    .trusted_time=trusted_time,.now_ms=now_ms,.server_auth_only=true,
    .peer_address=((struct sockaddr_in *)address->ai_addr)->sin_addr};
  ret=bkvoice_tls_initialize(&tls,&transport);
  if(ret)goto done;
  if(pcm_input)
    {
      uint8_t *pcm=malloc(BKCLOUD_PCM_MAX+1);
      FILE *input=fopen(pcm_input,"rb");
      if(!pcm || !input)
        {free(pcm);if(input)fclose(input);ret=-EIO;goto done;}
      size_t bytes=fread(pcm,1,BKCLOUD_PCM_MAX+1,input);
      ret=ferror(input) ? -EIO : 0;fclose(input);
      uint64_t begin=now_ms(NULL);
      if(!ret)ret=bkcloud_recognize(client,&config,bkvoice_tls_ops(),&tls,
          begin+60000,pcm,bytes,text,sizeof(text));
      bool matches=!ret && strstr(text,"语音") && strstr(text,"测试");
      printf("LIVE asr result=%d elapsed_ms=%llu pcm_bytes=%zu rate=16000 text_bytes=%zu expected_terms=%u\n",
          ret,(unsigned long long)(now_ms(NULL)-begin),bytes,strlen(text),matches ? 1u : 0u);
      mbedtls_platform_zeroize(pcm,BKCLOUD_PCM_MAX+1);free(pcm);
      if(!ret && !matches)ret=-EBADMSG;
      goto done;
    }
  struct bkcloud_history_s history={0};
  uint64_t start=now_ms(NULL);
  ret=bkcloud_chat(client,&config,bkvoice_tls_ops(),&tls,start+60000,
      "Reply briefly in Chinese.",&history,"请回复你好。",text,sizeof(text));
  printf("LIVE chat result=%d elapsed_ms=%llu text_bytes=%zu\n",ret,
         (unsigned long long)(now_ms(NULL)-start),strlen(text));
  if(!ret)
    {
      struct live_pcm_s stats={0};start=now_ms(NULL);
      if(pcm_output_file)
        {
          stats.output=fopen(pcm_output_file,"wb");
          if(!stats.output){ret=-EIO;goto done;}
        }
      ret=bkcloud_synthesize(client,decoder,&config,bkvoice_tls_ops(),&tls,
          start+60000,"你好，语音测试。",live_pcm,&stats);
      if(stats.output && fclose(stats.output) && !ret)ret=-EIO;
      if(!ret && stats.bytes%2)ret=-EBADMSG;
      printf("LIVE tts result=%d elapsed_ms=%llu pcm_bytes=%zu peak=%u rate=24000\n",
             ret,(unsigned long long)(now_ms(NULL)-start),stats.bytes,stats.peak);
      if(!ret && (!stats.bytes || !stats.peak))ret=-ENODATA;
    }
done:
  printf("LIVE final result=%d\n",ret);
  if(tls.initialized)bkvoice_tls_uninitialize(&tls);
  if(address)freeaddrinfo(address);
  mbedtls_x509_crt_free(&ca);
  bkcloud_config_clear(&config);
  mbedtls_platform_zeroize(text,sizeof(text));
  if(client){mbedtls_platform_zeroize(client,sizeof(*client));free(client);}
  if(decoder){bkcloud_tts_clear(decoder);free(decoder);}
  return ret ? 1 : 0;
}
int main(int argc, char **argv)
{
  if ((argc == 3 || argc == 4) && !strcmp(argv[1],"--live"))
    return live_https(argv[2],NULL,argc == 4 ? argv[3] : NULL);
  if (argc == 4 && !strcmp(argv[1],"--live-asr"))
    return live_https(argv[2],argv[3],NULL);
  if (argc == 3) return real_https(argv[1], argv[2]);
  assert(argc == 1);
  const uint8_t samples[] = {1, 0, 0, 128};
  for(size_t chunk=1;chunk<=4;chunk++) {
    struct live_pcm_s stats={0};
    for(size_t offset=0;offset<sizeof(samples);offset+=chunk) {
      size_t n=sizeof(samples)-offset; if(n>chunk)n=chunk;
      assert(live_pcm(&stats,samples+offset,n)==0);
    }
    assert(stats.bytes==4 && stats.peak==32768 && stats.low==0);
  }
  run("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", 0, 0, 128);
  run("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\n{\r\n1\r\n}\r\n0\r\n\r\n", 0, 0, 128);
  run("HTTP/1.1 302 Found\r\nLocation: http://other.example/\r\nContent-Length: 0\r\n\r\n", -EPERM, 0, 128);
  run("HTTP/1.1 401 Unauthorized\r\nContent-Length: 2\r\n\r\n{}", -EACCES, 0, 128);
  /* Error bodies must not hide the HTTP failure behind the success-body cap. */
  run("HTTP/1.1 401 Unauthorized\r\nContent-Length: 4\r\n\r\nFAIL", -EACCES, 0, 2);
  run("HTTP/1.1 403 Forbidden\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nFAIL\r\n0\r\n\r\n", -EACCES, 0, 2);
  run("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", -E2BIG, 0, 2);
  run("", -ECANCELED, -ECANCELED, 128);
  run("", -ETIMEDOUT, -ETIMEDOUT, 128);
  recognize();
  understand();
  tts_decoder();
  pcm_responses();
  stream_errors();
  puts("Real webclient: partial I/O, chunked, redirect, auth, bounds, cancellation PASS");
  return 0;
}
