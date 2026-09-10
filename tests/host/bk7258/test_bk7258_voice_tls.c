/* SPDX-License-Identifier: Apache-2.0 */

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#include "bk7258_voice_companion.h"
#include "bk7258_voice_tls.h"
#include "bk7258_voice_transport.h"
#include "bk7258_voice_wss.h"

struct probe_s
{
  struct bkvoice_tls_s tls;
  struct bkvoice_wss_s wss;
  struct bkvoice_transport_s transport;
  struct bkvoice_tls_config_s tls_config;
  struct bkvoice_wss_config_s wss_config;
  mbedtls_x509_crt ca;
  mbedtls_x509_crt certificate;
  mbedtls_pk_context key;
  int trusted;
  int server_auth_only;
  int tls_ready;
  int wss_ready;
  int transport_ready;
};

struct receive_s
{
  struct bkvoice_tls_s *tls;
  volatile int started;
  volatile int done;
  int result;
};

static uint64_t probe_now_ms(void *context)
{
  struct timespec now;

  (void)context;
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      return 0;
    }

  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static int probe_trusted_time(void *context)
{
  struct probe_s *probe = context;

  return probe->trusted ? 0 : -EAGAIN;
}

static void probe_cleanup(struct probe_s *probe)
{
  if (probe->transport_ready)
    {
      (void)bkvoice_transport_close(&probe->transport);
    }

  if (probe->wss_ready)
    {
      bkvoice_wss_uninitialize(&probe->wss);
    }

  if (probe->tls_ready)
    {
      bkvoice_tls_uninitialize(&probe->tls);
    }

  mbedtls_pk_free(&probe->key);
  mbedtls_x509_crt_free(&probe->certificate);
  mbedtls_x509_crt_free(&probe->ca);
}

static int probe_load_credentials(struct probe_s *probe, const char *ca_path,
                                  const char *cert_path, const char *key_path)
{
  int ret;

  mbedtls_x509_crt_init(&probe->ca);
  mbedtls_x509_crt_init(&probe->certificate);
  mbedtls_pk_init(&probe->key);

  ret = mbedtls_x509_crt_parse_file(&probe->ca, ca_path);
  if (ret < 0)
    {
      fprintf(stderr, "voice TLS CA load failed: %d\n", ret);
      return ret;
    }

  ret = mbedtls_x509_crt_parse_file(&probe->certificate, cert_path);
  if (ret < 0)
    {
      fprintf(stderr, "voice TLS client certificate load failed: %d\n", ret);
      return ret;
    }

  return mbedtls_pk_parse_keyfile(&probe->key, key_path, NULL, NULL, NULL);
}

static int probe_open(struct probe_s *probe, const char *host, int port)
{
  int ret;

  memset(&probe->tls, 0, sizeof(probe->tls));
  memset(&probe->wss, 0, sizeof(probe->wss));
  memset(&probe->transport, 0, sizeof(probe->transport));
  memset(&probe->tls_config, 0, sizeof(probe->tls_config));
  memset(&probe->wss_config, 0, sizeof(probe->wss_config));

  if (inet_pton(AF_INET, "127.0.0.1", &probe->tls_config.peer_address) != 1)
    {
      return -EINVAL;
    }

  probe->tls_config.server_ca = &probe->ca;
  probe->tls_config.client_certificate = &probe->certificate;
  probe->tls_config.client_key = &probe->key;
  if (probe->server_auth_only)
    {
      probe->tls_config.server_auth_only = true;
      probe->tls_config.client_certificate = NULL;
      probe->tls_config.client_key = NULL;
    }
  probe->tls_config.trusted_time = probe_trusted_time;
  probe->tls_config.now_ms = probe_now_ms;
  probe->tls_config.clock_context = probe;
  ret = bkvoice_tls_initialize(&probe->tls, &probe->tls_config);
  if (ret < 0)
    {
      fprintf(stderr, "voice TLS/WSS open failed: %d\n", ret);
      return ret;
    }

  probe->tls_ready = 1;
  probe->wss_config.host = host;
  probe->wss_config.port = (uint16_t)port;
  probe->wss_config.path = "/companion/v1";
  probe->wss_config.subprotocol = "companion-v1";
  ret = bkvoice_wss_initialize(&probe->wss, bkvoice_tls_ops(), &probe->tls,
                                &probe->wss_config);
  if (ret < 0)
    {
      fprintf(stderr, "voice WSS initialization failed: %d\n", ret);
      return ret;
    }

  probe->wss_ready = 1;
  ret = bkvoice_transport_initialize(&probe->transport,
                                     bkvoice_wss_transport_ops(), &probe->wss);
  if (ret < 0)
    {
      return ret;
    }

  probe->transport_ready = 1;
  return bkvoice_transport_open(&probe->transport, probe_now_ms(NULL) + 3000);
}

static int probe_send_frame(struct probe_s *probe, uint8_t type, uint16_t flags,
                            uint32_t turn, uint32_t sequence,
                            const uint8_t *payload, uint32_t payload_len)
{
  struct bkvoice_companion_header_s header;
  uint8_t wire[BKVOICE_COMPANION_HEADER_BYTES + BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  size_t wire_len;
  int ret;

  memset(&header, 0, sizeof(header));
  header.magic = BKVOICE_COMPANION_MAGIC;
  header.version = BKVOICE_COMPANION_VERSION;
  header.type = type;
  header.flags = flags;
  header.header_len = BKVOICE_COMPANION_HEADER_BYTES;
  header.payload_len = payload_len;
  header.boot_generation = 7;
  header.session_id = 3;
  header.turn_id = turn;
  header.sequence = sequence;
  header.timestamp_ms = probe_now_ms(NULL);
  ret = bkvoice_companion_encode(&header, payload, wire, sizeof(wire), &wire_len);
  return ret < 0 ? ret : bkvoice_transport_send_all(&probe->transport, wire,
                                                      wire_len, probe_now_ms(NULL) + 1000);
}

static int probe_recv_frame(struct probe_s *probe,
                            struct bkvoice_companion_header_s *header,
                            uint8_t *payload, size_t payload_size)
{
  uint8_t wire[BKVOICE_COMPANION_HEADER_BYTES + BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  const uint8_t *decoded;
  uint32_t bytes;
  int ret;

  ret = bkvoice_transport_recv_exact(&probe->transport, wire,
                                     BKVOICE_COMPANION_HEADER_BYTES,
                                     probe_now_ms(NULL) + 1500);
  if (ret < 0)
    return ret;
  bytes = ((uint32_t)wire[12] << 24) | ((uint32_t)wire[13] << 16) |
          ((uint32_t)wire[14] << 8) | wire[15];
  if (bytes > BKVOICE_COMPANION_AUDIO_FRAME_BYTES || bytes > payload_size)
    return -EMSGSIZE;
  ret = bkvoice_transport_recv_exact(&probe->transport,
                                     wire + BKVOICE_COMPANION_HEADER_BYTES,
                                     bytes, probe_now_ms(NULL) + 1500);
  if (ret < 0)
    return ret;
  ret = bkvoice_companion_decode(wire, BKVOICE_COMPANION_HEADER_BYTES + bytes,
                                 header, &decoded);
  if (ret == 0 && bytes != 0)
    memcpy(payload, decoded, bytes);
  return ret;
}

static int probe_hello(struct probe_s *probe)
{
  struct bkvoice_companion_header_s request;
  struct bkvoice_companion_header_s response;
  const uint8_t *decoded;
  uint8_t wire[BKVOICE_COMPANION_HEADER_BYTES];
  uint8_t audio[BKVOICE_COMPANION_AUDIO_FRAME_BYTES] = {0};
  uint8_t payload[BKVOICE_COMPANION_AUDIO_FRAME_BYTES];
  size_t wire_len;
  int ret;

  memset(&request, 0, sizeof(request));
  request.magic = BKVOICE_COMPANION_MAGIC;
  request.version = BKVOICE_COMPANION_VERSION;
  request.type = BKVOICE_COMPANION_HELLO;
  request.header_len = BKVOICE_COMPANION_HEADER_BYTES;
  request.boot_generation = 7;
  request.session_id = 3;
  request.sequence = 1;
  request.timestamp_ms = probe_now_ms(NULL);
  ret = bkvoice_companion_encode(&request, NULL, wire, sizeof(wire),
                                 &wire_len);
  if (ret < 0)
    {
      fprintf(stderr, "HELLO encode: %d\n", ret);
      return ret;
    }

  ret = bkvoice_transport_send_all(&probe->transport, wire, wire_len,
                                   probe_now_ms(NULL) + 1000);
  if (ret < 0)
    {
      fprintf(stderr, "HELLO send: %d\n", ret);
      return ret;
    }

  ret = bkvoice_transport_recv_exact(&probe->transport, wire, sizeof(wire),
                                     probe_now_ms(NULL) + 1000);
  if (ret < 0)
    {
      fprintf(stderr, "WELCOME receive: %d\n", ret);
      return ret;
    }

  ret = bkvoice_companion_decode(wire, sizeof(wire), &response, &decoded);
  if (ret < 0 || response.type != BKVOICE_COMPANION_WELCOME ||
      response.sequence != request.sequence)
    {
      fprintf(stderr, "WELCOME decode: %d type=%u seq=%lu\n", ret,
              response.type, (unsigned long)response.sequence);
      return -EPROTO;
    }

  ret = probe_recv_frame(probe, &response, payload, sizeof(payload));
  if (ret < 0 || response.type != BKVOICE_COMPANION_WINDOW_UPDATE ||
      response.sequence != 2)
    return -EPROTO;
  ret = probe_send_frame(probe, BKVOICE_COMPANION_WINDOW_UPDATE, 0, 0, 2,
                         (const uint8_t[]){0, 0, 2, 0x80}, 4);
  if (ret < 0)
    return ret;
  ret = probe_send_frame(probe, BKVOICE_COMPANION_TURN_START, 0, 1, 3,
                         NULL, 0);
  if (ret < 0)
    return ret;
  ret = probe_send_frame(probe, BKVOICE_COMPANION_AUDIO_UP,
                         BKVOICE_COMPANION_FLAG_END_OF_STREAM, 1, 4,
                         audio, sizeof(audio));
  if (ret < 0)
    return ret;
  ret = probe_recv_frame(probe, &response, payload, sizeof(payload));
  if (ret < 0 || response.type != BKVOICE_COMPANION_WINDOW_UPDATE ||
      response.sequence != 3)
    return -EPROTO;
  ret = probe_send_frame(probe, BKVOICE_COMPANION_TURN_END, 0, 1, 5,
                         NULL, 0);
  if (ret < 0)
    return ret;
  ret = probe_recv_frame(probe, &response, payload, sizeof(payload));
  if (ret < 0 || response.sequence != 4 || response.turn_id != 1 ||
      response.type != BKVOICE_COMPANION_TTS_START ||
      response.flags != BKVOICE_COMPANION_FLAG_SYNTHETIC)
    return -EPROTO;

  for (uint32_t index = 0; index < 6; index++)
    {
      ret = probe_recv_frame(probe, &response, payload, sizeof(payload));
      if (ret < 0 || response.sequence != index + 5 ||
          response.turn_id != 1 ||
          response.type != BKVOICE_COMPANION_AUDIO_DOWN ||
          response.payload_len != BKVOICE_COMPANION_AUDIO_FRAME_BYTES ||
          response.flags != (BKVOICE_COMPANION_FLAG_SYNTHETIC |
                             (index == 5 ?
                              BKVOICE_COMPANION_FLAG_END_OF_STREAM : 0)))
        return -EPROTO;

      ret = probe_send_frame(
        probe, BKVOICE_COMPANION_WINDOW_UPDATE, 0, 1, index + 6,
        (const uint8_t[]){0, 0, 2, 0x80}, 4);
      if (ret < 0)
        return ret;
    }

  ret = probe_recv_frame(probe, &response, payload, sizeof(payload));
  if (ret < 0 || response.sequence != 11 || response.turn_id != 1 ||
      response.type != BKVOICE_COMPANION_TTS_END ||
      response.flags != BKVOICE_COMPANION_FLAG_SYNTHETIC)
    return -EPROTO;

  return 0;
}

static void *probe_receive(void *argument)
{
  struct receive_s *receive = argument;
  uint8_t byte;

  receive->started = 1;
  receive->result = bkvoice_tls_ops()->recv(receive->tls, &byte, sizeof(byte),
                                            probe_now_ms(NULL) + 1000);
  receive->done = 1;
  return NULL;
}

static int probe_interrupt(struct probe_s *probe, int with_transmit)
{
  struct receive_s receive;
  pthread_t thread;
  uint8_t ping[] = {0x89, 0x80, 0x11, 0x22, 0x33, 0x44};
  int i;
  int ret;

  memset(&receive, 0, sizeof(receive));
  receive.tls = &probe->tls;
  if (pthread_create(&thread, NULL, probe_receive, &receive) != 0)
    {
      return -errno;
    }

  for (i = 0; i < 20 && !receive.started; i++)
    {
      (void)poll(NULL, 0, 5);
    }

  if (!receive.started || receive.done)
    {
      (void)bkvoice_tls_ops()->interrupt(&probe->tls);
      (void)pthread_join(thread, NULL);
      return -EIO;
    }

  if (with_transmit)
    {
      ret = bkvoice_tls_ops()->send(&probe->tls, ping, sizeof(ping),
                                    probe_now_ms(NULL) + 500);
      if (ret != (int)sizeof(ping))
        {
          (void)bkvoice_tls_ops()->interrupt(&probe->tls);
          (void)pthread_join(thread, NULL);
          return ret < 0 ? ret : -EIO;
        }
    }

  (void)bkvoice_tls_ops()->interrupt(&probe->tls);
  (void)pthread_join(thread, NULL);
  if (!with_transmit && receive.result != -ECANCELED)
    {
      return -EIO;
    }

  if (with_transmit && receive.result < 0 && receive.result != -ECANCELED)
    {
      return receive.result;
    }

  return 0;
}

static int probe_reopen(struct probe_s *probe, const char *host, int port)
{
  int ret;

  (void)bkvoice_transport_close(&probe->transport);
  bkvoice_wss_uninitialize(&probe->wss);
  probe->wss_ready = 0;
  bkvoice_tls_uninitialize(&probe->tls);
  probe->tls_ready = 0;
  probe->transport_ready = 0;
  ret = probe_open(probe, host, port);
  return ret < 0 ? ret : probe_hello(probe);
}

static int probe_run(const char *mode, struct probe_s *probe,
                     const char *host, int port)
{
  int ret = probe_open(probe, host, port);

  if (strcmp(mode, "reject-time") == 0)
    {
      return ret == -EAGAIN ? 0 : -EIO;
    }

  if (strcmp(mode, "reject-verify") == 0)
    {
      if (ret >= 0)
        {
          fprintf(stderr, "verification unexpectedly accepted\n");
        }
      return ret < 0 ? 0 : -EIO;
    }

  if (ret < 0)
    {
      fprintf(stderr, "voice TLS/WSS open failed: %d\n", ret);
      return ret;
    }

  ret = probe_hello(probe);
  if (ret < 0)
    {
      fprintf(stderr, "voice companion HELLO failed: %d\n", ret);
      return ret;
    }

  if (strcmp(mode, "deadline") == 0)
    {
      uint8_t byte;
      ret = bkvoice_tls_ops()->recv(&probe->tls, &byte, sizeof(byte),
                                    probe_now_ms(NULL) + 60);
      if (ret != -ETIMEDOUT)
        {
          return -EIO;
        }

      ret = bkvoice_tls_ops()->send(&probe->tls, &byte, sizeof(byte),
                                    probe_now_ms(NULL) + 100);
      return ret == -ENOTCONN ? probe_reopen(probe, host, port) : -EIO;
    }

  if (strcmp(mode, "interrupt") == 0)
    {
      ret = probe_interrupt(probe, 0);
      return ret < 0 ? ret : probe_reopen(probe, host, port);
    }

  if (strcmp(mode, "concurrent") == 0)
    {
      ret = probe_interrupt(probe, 1);
      return ret < 0 ? ret : probe_reopen(probe, host, port);
    }

  if (strcmp(mode, "reconnect") == 0)
    {
      return probe_reopen(probe, host, port);
    }

  return strcmp(mode, "success") == 0 ? 0 : -EINVAL;
}

int main(int argc, char **argv)
{
  struct probe_s probe;
  int ret;
  int port;

  if (argc != 8)
    {
      return EXIT_FAILURE;
    }

  memset(&probe, 0, sizeof(probe));
  probe.trusted = atoi(argv[7]) != 0;
  const char *mode = argv[1];
  if (strncmp(mode, "cloud-", 6) == 0)
    {
      probe.server_auth_only = 1;
      mode += 6;
    }
  port = atoi(argv[3]);
  if (port <= 0 || port > 65535)
    {
      return EXIT_FAILURE;
    }

  ret = probe_load_credentials(&probe, argv[4], argv[5], argv[6]);
  if (ret >= 0)
    {
      ret = probe_run(mode, &probe, argv[2], port);
    }

  probe_cleanup(&probe);
  if (ret < 0)
    {
      fprintf(stderr, "voice TLS probe failed: %d\n", ret);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
