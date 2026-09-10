/****************************************************************************
 * app/bk7258/bk7258_voice_wss.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "bk7258_voice_wss.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BKVOICE_WSS_GUID \
  "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define BKVOICE_WSS_NONCE_BYTES       16u
#define BKVOICE_WSS_SHA1_BYTES        20u
#define BKVOICE_WSS_CLIENT_KEY_BYTES  25u
#define BKVOICE_WSS_ACCEPT_KEY_BYTES  29u
#define BKVOICE_WSS_REQUEST_BYTES     640u
#define BKVOICE_WSS_RESPONSE_BYTES    1024u
#define BKVOICE_WSS_CONTROL_BYTES     125u
#define BKVOICE_WSS_MASK_CHUNK_BYTES  128u

#define BKVOICE_WSS_OPCODE_CONTINUATION 0x0u
#define BKVOICE_WSS_OPCODE_TEXT         0x1u
#define BKVOICE_WSS_OPCODE_BINARY       0x2u
#define BKVOICE_WSS_OPCODE_CLOSE        0x8u
#define BKVOICE_WSS_OPCODE_PING         0x9u
#define BKVOICE_WSS_OPCODE_PONG         0xau

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char g_bkvoice_wss_base64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool bkvoice_wss_tls_ops_valid(
  const struct bkvoice_wss_tls_ops_s *ops)
{
  return ops != NULL && ops->open_verified != NULL && ops->send != NULL &&
         ops->recv != NULL && ops->interrupt != NULL && ops->close != NULL &&
         ops->random != NULL && ops->sha1 != NULL;
}

static bool bkvoice_wss_host_valid(const char *host)
{
  size_t i;
  size_t length;

  if (host == NULL)
    {
      return false;
    }

  length = strlen(host);
  if (length == 0 || length > BKVOICE_WSS_HOST_MAX ||
      host[0] == '.' || host[length - 1] == '.')
    {
      return false;
    }

  for (i = 0; i < length; i++)
    {
      unsigned char ch = (unsigned char)host[i];

      if (!isalnum(ch) && ch != '.' && ch != '-')
        {
          return false;
        }
    }

  return true;
}

static bool bkvoice_wss_path_valid(const char *path)
{
  size_t i;
  size_t length;

  if (path == NULL)
    {
      return false;
    }

  length = strlen(path);
  if (length == 0 || length > BKVOICE_WSS_PATH_MAX || path[0] != '/')
    {
      return false;
    }

  for (i = 0; i < length; i++)
    {
      unsigned char ch = (unsigned char)path[i];

      if (ch <= 0x20u || ch >= 0x7fu || ch == '#')
        {
          return false;
        }
    }

  return true;
}

static bool bkvoice_wss_token_char(unsigned char ch)
{
  return isalnum(ch) || ch == '!' || ch == '#' || ch == '$' || ch == '%' ||
         ch == '&' || ch == 0x27u || ch == '*' || ch == '+' || ch == '-' ||
         ch == '.' || ch == '^' || ch == '_' || ch == '`' || ch == '|' ||
         ch == '~';
}

static bool bkvoice_wss_subprotocol_valid(const char *subprotocol)
{
  size_t i;
  size_t length;

  if (subprotocol == NULL)
    {
      return false;
    }

  length = strlen(subprotocol);
  if (length == 0 || length > BKVOICE_WSS_SUBPROTOCOL_MAX)
    {
      return false;
    }

  for (i = 0; i < length; i++)
    {
      if (!bkvoice_wss_token_char((unsigned char)subprotocol[i]))
        {
          return false;
        }
    }

  return true;
}

static int bkvoice_wss_base64_encode(const uint8_t *source,
                                     size_t source_size,
                                     char *output, size_t output_size)
{
  size_t needed = ((source_size + 2u) / 3u) * 4u;
  size_t input = 0;
  size_t written = 0;

  if (source == NULL || output == NULL || output_size <= needed)
    {
      return -EMSGSIZE;
    }

  while (input + 3u <= source_size)
    {
      uint32_t word = ((uint32_t)source[input] << 16) |
                      ((uint32_t)source[input + 1u] << 8) |
                      source[input + 2u];

      output[written++] = g_bkvoice_wss_base64[(word >> 18) & 0x3fu];
      output[written++] = g_bkvoice_wss_base64[(word >> 12) & 0x3fu];
      output[written++] = g_bkvoice_wss_base64[(word >> 6) & 0x3fu];
      output[written++] = g_bkvoice_wss_base64[word & 0x3fu];
      input += 3u;
    }

  if (input < source_size)
    {
      uint32_t word = (uint32_t)source[input] << 16;

      if (input + 1u < source_size)
        {
          word |= (uint32_t)source[input + 1u] << 8;
        }

      output[written++] = g_bkvoice_wss_base64[(word >> 18) & 0x3fu];
      output[written++] = g_bkvoice_wss_base64[(word >> 12) & 0x3fu];
      output[written++] = input + 1u < source_size ?
                          g_bkvoice_wss_base64[(word >> 6) & 0x3fu] : '=';
      output[written++] = '=';
    }

  output[written] = '\0';
  return (int)written;
}

static int bkvoice_wss_error(struct bkvoice_wss_s *wss, int error,
                             bool interrupt)
{
  if (error >= 0)
    {
      error = -EIO;
    }

  __atomic_store_n(&wss->last_error, error, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->faulted, true, __ATOMIC_RELEASE);
  if (interrupt && __atomic_load_n(&wss->tls_open, __ATOMIC_ACQUIRE))
    {
      (void)wss->tls_ops.interrupt(wss->tls_context);
    }

  return error;
}

static int bkvoice_wss_raw_send_all(struct bkvoice_wss_s *wss,
                                    const uint8_t *buffer, size_t bytes,
                                    uint64_t deadline_ms)
{
  size_t offset = 0;

  while (offset < bytes)
    {
      ssize_t sent = wss->tls_ops.send(wss->tls_context, buffer + offset,
                                       bytes - offset, deadline_ms);

      if (sent < 0)
        {
          return (int)sent;
        }

      if (sent == 0)
        {
          return -EPIPE;
        }

      if ((size_t)sent > bytes - offset)
        {
          return -EPROTO;
        }

      offset += (size_t)sent;
    }

  return 0;
}

static int bkvoice_wss_raw_recv_exact(struct bkvoice_wss_s *wss,
                                      uint8_t *buffer, size_t bytes,
                                      uint64_t deadline_ms)
{
  size_t offset = 0;

  while (offset < bytes)
    {
      ssize_t received = wss->tls_ops.recv(wss->tls_context,
                                           buffer + offset, bytes - offset,
                                           deadline_ms);

      if (received < 0)
        {
          return (int)received;
        }

      if (received == 0)
        {
          return -ECONNRESET;
        }

      if ((size_t)received > bytes - offset)
        {
          return -EPROTO;
        }

      offset += (size_t)received;
    }

  return 0;
}

static int bkvoice_wss_make_accept(struct bkvoice_wss_s *wss,
                                   const char *client_key,
                                   char accept[BKVOICE_WSS_ACCEPT_KEY_BYTES])
{
  uint8_t digest[BKVOICE_WSS_SHA1_BYTES];
  char source[BKVOICE_WSS_CLIENT_KEY_BYTES + sizeof(BKVOICE_WSS_GUID)];
  int ret;

  ret = snprintf(source, sizeof(source), "%s%s", client_key,
                 BKVOICE_WSS_GUID);
  if (ret < 0 || (size_t)ret >= sizeof(source))
    {
      return -EOVERFLOW;
    }

  ret = wss->tls_ops.sha1(wss->tls_context, (const uint8_t *)source,
                          (size_t)ret, digest);
  memset(source, 0, sizeof(source));
  if (ret != 0)
    {
      memset(digest, 0, sizeof(digest));
      return ret < 0 ? ret : -EIO;
    }

  ret = bkvoice_wss_base64_encode(digest, sizeof(digest), accept,
                                  BKVOICE_WSS_ACCEPT_KEY_BYTES);
  memset(digest, 0, sizeof(digest));
  return ret < 0 ? ret : 0;
}

static bool bkvoice_wss_header_name_equal(const char *name,
                                          size_t name_size,
                                          const char *expected)
{
  return strlen(expected) == name_size &&
         strncasecmp(name, expected, name_size) == 0;
}

static const char *bkvoice_wss_trim_value(char *value)
{
  char *end;

  while (*value == ' ' || *value == '\t')
    {
      value++;
    }

  end = value + strlen(value);
  while (end > value && (*(end - 1) == ' ' || *(end - 1) == '\t'))
    {
      *--end = '\0';
    }

  return value;
}

static bool bkvoice_wss_token_list_contains(const char *value,
                                            const char *expected)
{
  size_t expected_size = strlen(expected);

  while (*value != '\0')
    {
      const char *begin;
      const char *end;

      while (*value == ' ' || *value == '\t' || *value == ',')
        {
          value++;
        }

      begin = value;
      while (*value != '\0' && *value != ',')
        {
          value++;
        }

      end = value;
      while (end > begin &&
             (*(end - 1) == ' ' || *(end - 1) == '\t'))
        {
          end--;
        }

      if ((size_t)(end - begin) == expected_size &&
          strncasecmp(begin, expected, expected_size) == 0)
        {
          return true;
        }
    }

  return false;
}

static int bkvoice_wss_validate_response(
  struct bkvoice_wss_s *wss, char *response, const char *expected_accept)
{
  char *line;
  char *next;
  bool upgrade = false;
  bool connection = false;
  bool accept = false;
  bool protocol = false;

  next = strstr(response, "\r\n");
  if (next == NULL)
    {
      return -EPROTO;
    }

  *next = '\0';
  if (strncmp(response, "HTTP/1.1 101", 12) != 0 ||
      (response[12] != '\0' && response[12] != ' '))
    {
      return -ECONNREFUSED;
    }

  line = next + 2;
  while (*line != '\0')
    {
      char *colon;
      const char *value;
      size_t name_size;

      next = strstr(line, "\r\n");
      if (next == NULL)
        {
          return -EPROTO;
        }

      *next = '\0';
      if (*line == '\0')
        {
          break;
        }

      colon = strchr(line, ':');
      if (colon == NULL || colon == line ||
          *(colon - 1) == ' ' || *(colon - 1) == '\t')
        {
          return -EPROTO;
        }

      name_size = (size_t)(colon - line);
      value = bkvoice_wss_trim_value(colon + 1);
      if (bkvoice_wss_header_name_equal(line, name_size, "Upgrade"))
        {
          if (upgrade || !bkvoice_wss_token_list_contains(value,
                                                          "websocket"))
            {
              return -EPROTO;
            }

          upgrade = true;
        }
      else if (bkvoice_wss_header_name_equal(line, name_size,
                                             "Connection"))
        {
          if (!bkvoice_wss_token_list_contains(value, "upgrade"))
            {
              return -EPROTO;
            }

          connection = true;
        }
      else if (bkvoice_wss_header_name_equal(
                 line, name_size, "Sec-WebSocket-Accept"))
        {
          if (accept || strcmp(value, expected_accept) != 0)
            {
              return -EACCES;
            }

          accept = true;
        }
      else if (bkvoice_wss_header_name_equal(
                 line, name_size, "Sec-WebSocket-Protocol"))
        {
          if (protocol || strcmp(value, wss->subprotocol) != 0)
            {
              return -EPROTO;
            }

          protocol = true;
        }
      else if (bkvoice_wss_header_name_equal(
                 line, name_size, "Sec-WebSocket-Extensions") &&
               *value != '\0')
        {
          return -ENOTSUP;
        }

      line = next + 2;
    }

  return upgrade && connection && accept && protocol ? 0 : -EPROTO;
}

static int bkvoice_wss_upgrade(struct bkvoice_wss_s *wss,
                               uint64_t deadline_ms)
{
  uint8_t nonce[BKVOICE_WSS_NONCE_BYTES];
  char client_key[BKVOICE_WSS_CLIENT_KEY_BYTES];
  char expected_accept[BKVOICE_WSS_ACCEPT_KEY_BYTES];
  char request[BKVOICE_WSS_REQUEST_BYTES];
  char response[BKVOICE_WSS_RESPONSE_BYTES];
  size_t received = 0;
  int request_size;
  int ret;

  ret = wss->tls_ops.random(wss->tls_context, nonce, sizeof(nonce));
  if (ret != 0)
    {
      return ret < 0 ? ret : -EIO;
    }

  ret = bkvoice_wss_base64_encode(nonce, sizeof(nonce), client_key,
                                  sizeof(client_key));
  memset(nonce, 0, sizeof(nonce));
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_wss_make_accept(wss, client_key, expected_accept);
  if (ret < 0)
    {
      memset(client_key, 0, sizeof(client_key));
      return ret;
    }

  if (wss->port == 443u)
    {
      request_size = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: %s\r\n\r\n",
        wss->path, wss->host, client_key, wss->subprotocol);
    }
  else
    {
      request_size = snprintf(
        request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: %s\r\n\r\n",
        wss->path, wss->host, (unsigned int)wss->port, client_key,
        wss->subprotocol);
    }

  memset(client_key, 0, sizeof(client_key));
  if (request_size < 0 || (size_t)request_size >= sizeof(request))
    {
      memset(expected_accept, 0, sizeof(expected_accept));
      return -EOVERFLOW;
    }

  ret = bkvoice_wss_raw_send_all(wss, (const uint8_t *)request,
                                 (size_t)request_size, deadline_ms);
  memset(request, 0, sizeof(request));
  if (ret < 0)
    {
      memset(expected_accept, 0, sizeof(expected_accept));
      return ret;
    }

  while (received + 1u < sizeof(response))
    {
      ret = bkvoice_wss_raw_recv_exact(
        wss, (uint8_t *)&response[received], 1, deadline_ms);
      if (ret < 0)
        {
          memset(expected_accept, 0, sizeof(expected_accept));
          return ret;
        }

      received++;
      if (received >= 4u &&
          memcmp(response + received - 4u, "\r\n\r\n", 4) == 0)
        {
          response[received] = '\0';
          ret = bkvoice_wss_validate_response(wss, response,
                                              expected_accept);
          memset(expected_accept, 0, sizeof(expected_accept));
          memset(response, 0, sizeof(response));
          return ret;
        }
    }

  memset(expected_accept, 0, sizeof(expected_accept));
  memset(response, 0, sizeof(response));
  return -EOVERFLOW;
}

static int bkvoice_wss_tx_lock(struct bkvoice_wss_s *wss)
{
  int ret = pthread_mutex_lock(&wss->tx_lock);
  return ret == 0 ? 0 : -ret;
}

static void bkvoice_wss_tx_unlock(struct bkvoice_wss_s *wss)
{
  (void)pthread_mutex_unlock(&wss->tx_lock);
}

static int bkvoice_wss_send_frame_locked(struct bkvoice_wss_s *wss,
                                         uint8_t opcode,
                                         const uint8_t *payload,
                                         size_t payload_size,
                                         uint64_t deadline_ms)
{
  uint8_t header[8];
  uint8_t mask[4];
  uint8_t chunk[BKVOICE_WSS_MASK_CHUNK_BYTES];
  size_t header_size;
  size_t offset = 0;
  int ret;

  if (payload_size != 0 && payload == NULL)
    {
      return -EINVAL;
    }

  if ((opcode & 0x08u) != 0 && payload_size > BKVOICE_WSS_CONTROL_BYTES)
    {
      return -EMSGSIZE;
    }

  if (payload_size > UINT16_MAX)
    {
      return -EMSGSIZE;
    }

  ret = wss->tls_ops.random(wss->tls_context, mask, sizeof(mask));
  if (ret != 0)
    {
      return ret < 0 ? ret : -EIO;
    }

  header[0] = 0x80u | opcode;
  if (payload_size <= 125u)
    {
      header[1] = 0x80u | (uint8_t)payload_size;
      memcpy(header + 2, mask, sizeof(mask));
      header_size = 6u;
    }
  else
    {
      header[1] = 0x80u | 126u;
      header[2] = (uint8_t)(payload_size >> 8);
      header[3] = (uint8_t)payload_size;
      memcpy(header + 4, mask, sizeof(mask));
      header_size = 8u;
    }

  ret = bkvoice_wss_raw_send_all(wss, header, header_size, deadline_ms);
  if (ret < 0)
    {
      return ret;
    }

  while (offset < payload_size)
    {
      size_t i;
      size_t bytes = payload_size - offset;

      if (bytes > sizeof(chunk))
        {
          bytes = sizeof(chunk);
        }

      for (i = 0; i < bytes; i++)
        {
          chunk[i] = payload[offset + i] ^ mask[(offset + i) & 3u];
        }

      ret = bkvoice_wss_raw_send_all(wss, chunk, bytes, deadline_ms);
      memset(chunk, 0, bytes);
      if (ret < 0)
        {
          return ret;
        }

      offset += bytes;
    }

  return 0;
}

static int bkvoice_wss_send_frame(struct bkvoice_wss_s *wss,
                                  uint8_t opcode,
                                  const uint8_t *payload,
                                  size_t payload_size,
                                  uint64_t deadline_ms)
{
  int ret = bkvoice_wss_tx_lock(wss);

  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_wss_send_frame_locked(wss, opcode, payload, payload_size,
                                      deadline_ms);
  bkvoice_wss_tx_unlock(wss);
  return ret;
}

static bool bkvoice_wss_utf8_valid(const uint8_t *text, size_t bytes)
{
  size_t offset = 0;

  while (offset < bytes)
    {
      uint8_t first = text[offset++];
      size_t continuation;

      if (first <= 0x7fu)
        {
          continue;
        }

      if (first >= 0xc2u && first <= 0xdfu)
        {
          continuation = 1u;
        }
      else if (first >= 0xe0u && first <= 0xefu)
        {
          if (offset >= bytes ||
              (first == 0xe0u && text[offset] < 0xa0u) ||
              (first == 0xedu && text[offset] > 0x9fu))
            {
              return false;
            }

          continuation = 2u;
        }
      else if (first >= 0xf0u && first <= 0xf4u)
        {
          if (offset >= bytes ||
              (first == 0xf0u && text[offset] < 0x90u) ||
              (first == 0xf4u && text[offset] > 0x8fu))
            {
              return false;
            }

          continuation = 3u;
        }
      else
        {
          return false;
        }

      if (continuation > bytes - offset)
        {
          return false;
        }

      while (continuation-- > 0)
        {
          if ((text[offset++] & 0xc0u) != 0x80u)
            {
              return false;
            }
        }
    }

  return true;
}

static bool bkvoice_wss_close_payload_valid(const uint8_t *payload,
                                            size_t payload_size)
{
  uint16_t code;

  if (payload_size == 0)
    {
      return true;
    }

  if (payload_size == 1u)
    {
      return false;
    }

  code = ((uint16_t)payload[0] << 8) | payload[1];
  if (code < 1000u || code >= 5000u || code == 1004u || code == 1005u ||
      code == 1006u || code == 1015u)
    {
      return false;
    }

  return bkvoice_wss_utf8_valid(payload + 2u, payload_size - 2u);
}

static int bkvoice_wss_read_length(struct bkvoice_wss_s *wss,
                                   uint8_t length_code,
                                   uint64_t *payload_size,
                                   uint64_t deadline_ms)
{
  uint8_t extended[8];
  size_t i;
  int ret;

  if (length_code <= 125u)
    {
      *payload_size = length_code;
      return 0;
    }

  if (length_code == 126u)
    {
      ret = bkvoice_wss_raw_recv_exact(wss, extended, 2, deadline_ms);
      if (ret < 0)
        {
          return ret;
        }

      *payload_size = ((uint64_t)extended[0] << 8) | extended[1];
      return *payload_size >= 126u ? 0 : -EPROTO;
    }

  ret = bkvoice_wss_raw_recv_exact(wss, extended, sizeof(extended),
                                   deadline_ms);
  if (ret < 0)
    {
      return ret;
    }

  if ((extended[0] & 0x80u) != 0)
    {
      return -EPROTO;
    }

  *payload_size = 0;
  for (i = 0; i < sizeof(extended); i++)
    {
      *payload_size = (*payload_size << 8) | extended[i];
    }

  if (*payload_size <= UINT16_MAX)
    {
      return -EPROTO;
    }

  return -EMSGSIZE;
}

static int bkvoice_wss_handle_control(struct bkvoice_wss_s *wss,
                                      uint8_t opcode, bool final,
                                      uint64_t payload_size,
                                      uint64_t deadline_ms)
{
  uint8_t payload[BKVOICE_WSS_CONTROL_BYTES];
  int ret;

  if (!final || payload_size > sizeof(payload))
    {
      return -EPROTO;
    }

  if (payload_size != 0)
    {
      ret = bkvoice_wss_raw_recv_exact(wss, payload, (size_t)payload_size,
                                       deadline_ms);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (opcode == BKVOICE_WSS_OPCODE_PING)
    {
      ret = bkvoice_wss_send_frame(wss, BKVOICE_WSS_OPCODE_PONG, payload,
                                   (size_t)payload_size, deadline_ms);
      if (ret >= 0)
        {
          __atomic_add_fetch(&wss->ping_count, 1u, __ATOMIC_RELAXED);
        }

      return ret;
    }

  if (opcode == BKVOICE_WSS_OPCODE_PONG)
    {
      __atomic_add_fetch(&wss->pong_count, 1u, __ATOMIC_RELAXED);
      return 0;
    }

  if (opcode == BKVOICE_WSS_OPCODE_CLOSE)
    {
      if (!bkvoice_wss_close_payload_valid(payload, (size_t)payload_size))
        {
          return -EPROTO;
        }

      ret = bkvoice_wss_send_frame(wss, BKVOICE_WSS_OPCODE_CLOSE, payload,
                                   (size_t)payload_size, deadline_ms);
      if (ret < 0)
        {
          return ret;
        }

      __atomic_store_n(&wss->peer_closed, true, __ATOMIC_RELEASE);
      __atomic_store_n(&wss->last_error, -ECONNRESET, __ATOMIC_RELEASE);
      return 1;
    }

  return -EPROTO;
}

static int bkvoice_wss_load_message(struct bkvoice_wss_s *wss,
                                    uint64_t deadline_ms)
{
  struct bkvoice_companion_header_s companion;
  const uint8_t *payload;
  bool fragmented = false;

  wss->rx_size = 0;
  wss->rx_offset = 0;
  for (; ; )
    {
      uint8_t header[2];
      uint8_t opcode;
      uint64_t payload_size;
      bool final;
      int ret;

      ret = bkvoice_wss_raw_recv_exact(wss, header, sizeof(header),
                                       deadline_ms);
      if (ret < 0)
        {
          return ret;
        }

      final = (header[0] & 0x80u) != 0;
      opcode = header[0] & 0x0fu;
      if ((header[0] & 0x70u) != 0 || (header[1] & 0x80u) != 0)
        {
          return -EPROTO;
        }

      ret = bkvoice_wss_read_length(wss, header[1] & 0x7fu,
                                    &payload_size, deadline_ms);
      if (ret < 0)
        {
          return ret;
        }

      if ((opcode & 0x08u) != 0)
        {
          ret = bkvoice_wss_handle_control(wss, opcode, final,
                                           payload_size, deadline_ms);
          if (ret != 0)
            {
              return ret;
            }

          continue;
        }

      if (opcode == BKVOICE_WSS_OPCODE_TEXT)
        {
          return -EPROTO;
        }

      if (opcode == BKVOICE_WSS_OPCODE_BINARY)
        {
          if (fragmented || wss->rx_size != 0)
            {
              return -EPROTO;
            }

          fragmented = !final;
        }
      else if (opcode == BKVOICE_WSS_OPCODE_CONTINUATION)
        {
          if (!fragmented)
            {
              return -EPROTO;
            }

          fragmented = !final;
        }
      else
        {
          return -EPROTO;
        }

      if (payload_size > sizeof(wss->rx_message) - wss->rx_size)
        {
          return -EMSGSIZE;
        }

      ret = bkvoice_wss_raw_recv_exact(
        wss, wss->rx_message + wss->rx_size, (size_t)payload_size,
        deadline_ms);
      if (ret < 0)
        {
          return ret;
        }

      wss->rx_size += (size_t)payload_size;
      if (!fragmented)
        {
          ret = bkvoice_companion_decode(wss->rx_message, wss->rx_size,
                                         &companion, &payload);
          if (ret < 0)
            {
              return ret;
            }

          (void)payload;
          __atomic_add_fetch(&wss->rx_messages, 1u, __ATOMIC_RELAXED);
          return 0;
        }
    }
}

static int bkvoice_wss_transport_open(void *context, uint64_t deadline_ms)
{
  struct bkvoice_wss_s *wss = context;
  int ret;

  if (wss == NULL || !wss->initialized)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&wss->tls_open, __ATOMIC_ACQUIRE))
    {
      ret = wss->tls_ops.close(wss->tls_context);
      if (ret != 0)
        {
          return ret < 0 ? ret : -EIO;
        }

      __atomic_store_n(&wss->tls_open, false, __ATOMIC_RELEASE);
      __atomic_store_n(&wss->upgraded, false, __ATOMIC_RELEASE);
    }

  ret = wss->tls_ops.open_verified(wss->tls_context, wss->host, wss->port,
                                   deadline_ms);
  if (ret != 0)
    {
      return bkvoice_wss_error(wss, ret < 0 ? ret : -EIO, false);
    }

  __atomic_store_n(&wss->tls_open, true, __ATOMIC_RELEASE);
  ret = bkvoice_wss_upgrade(wss, deadline_ms);
  if (ret < 0)
    {
      int close_ret = wss->tls_ops.close(wss->tls_context);

      if (close_ret == 0)
        {
          __atomic_store_n(&wss->tls_open, false, __ATOMIC_RELEASE);
        }

      return bkvoice_wss_error(wss, ret, close_ret != 0);
    }

  wss->rx_size = 0;
  wss->rx_offset = 0;
  __atomic_store_n(&wss->tx_messages, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->rx_messages, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->ping_count, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->pong_count, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->last_error, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->interrupted, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->peer_closed, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->faulted, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->upgraded, true, __ATOMIC_RELEASE);
  return 0;
}

static ssize_t bkvoice_wss_transport_send(void *context,
                                          const uint8_t *buffer,
                                          size_t bytes,
                                          uint64_t deadline_ms)
{
  struct bkvoice_wss_s *wss = context;
  struct bkvoice_companion_header_s companion;
  const uint8_t *payload;
  int ret;

  if (wss == NULL || !wss->initialized || buffer == NULL)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&wss->upgraded, __ATOMIC_ACQUIRE))
    {
      return -ENOTCONN;
    }

  if (__atomic_load_n(&wss->interrupted, __ATOMIC_ACQUIRE))
    {
      return -ECANCELED;
    }

  if (__atomic_load_n(&wss->faulted, __ATOMIC_ACQUIRE))
    {
      ret = __atomic_load_n(&wss->last_error, __ATOMIC_ACQUIRE);
      return ret < 0 ? ret : -EIO;
    }

  ret = bkvoice_companion_decode(buffer, bytes, &companion, &payload);
  if (ret < 0)
    {
      return ret;
    }

  (void)payload;
  ret = bkvoice_wss_send_frame(wss, BKVOICE_WSS_OPCODE_BINARY, buffer, bytes,
                               deadline_ms);
  if (ret < 0)
    {
      return bkvoice_wss_error(wss, ret, true);
    }

  __atomic_add_fetch(&wss->tx_messages, 1u, __ATOMIC_RELAXED);
  __atomic_store_n(&wss->last_error, 0, __ATOMIC_RELEASE);
  return (ssize_t)bytes;
}

static ssize_t bkvoice_wss_transport_recv(void *context, uint8_t *buffer,
                                          size_t bytes,
                                          uint64_t deadline_ms)
{
  struct bkvoice_wss_s *wss = context;
  size_t available;
  int ret;

  if (wss == NULL || !wss->initialized || (bytes != 0 && buffer == NULL))
    {
      return -EINVAL;
    }

  if (bytes == 0)
    {
      return 0;
    }

  if (!__atomic_load_n(&wss->upgraded, __ATOMIC_ACQUIRE))
    {
      return -ENOTCONN;
    }

  if (__atomic_load_n(&wss->interrupted, __ATOMIC_ACQUIRE))
    {
      return -ECANCELED;
    }

  if (__atomic_load_n(&wss->peer_closed, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  if (__atomic_load_n(&wss->faulted, __ATOMIC_ACQUIRE))
    {
      ret = __atomic_load_n(&wss->last_error, __ATOMIC_ACQUIRE);
      return ret < 0 ? ret : -EIO;
    }

  if (wss->rx_offset == wss->rx_size)
    {
      ret = bkvoice_wss_load_message(wss, deadline_ms);
      if (ret == 1)
        {
          return 0;
        }

      if (ret < 0)
        {
          return bkvoice_wss_error(wss, ret, true);
        }
    }

  available = wss->rx_size - wss->rx_offset;
  if (bytes > available)
    {
      bytes = available;
    }

  memcpy(buffer, wss->rx_message + wss->rx_offset, bytes);
  wss->rx_offset += bytes;
  __atomic_store_n(&wss->last_error, 0, __ATOMIC_RELEASE);
  return (ssize_t)bytes;
}

static int bkvoice_wss_transport_interrupt(void *context)
{
  struct bkvoice_wss_s *wss = context;
  int ret;

  if (wss == NULL || !wss->initialized)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&wss->tls_open, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  __atomic_store_n(&wss->interrupted, true, __ATOMIC_RELEASE);
  ret = wss->tls_ops.interrupt(wss->tls_context);
  if (ret != 0)
    {
      return bkvoice_wss_error(wss, ret < 0 ? ret : -EIO, false);
    }

  return 0;
}

static int bkvoice_wss_transport_close(void *context)
{
  struct bkvoice_wss_s *wss = context;
  int ret;

  if (wss == NULL || !wss->initialized)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&wss->tls_open, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  /* The generic transport close operation has no deadline.  Do not risk an
   * unbounded graceful-close write here: interrupt/join has already happened
   * and the verified TLS provider's close() contract is itself bounded.
   * A peer-initiated CLOSE is still echoed from the receive path using that
   * receive operation's absolute deadline.
   */

  ret = wss->tls_ops.close(wss->tls_context);
  if (ret != 0)
    {
      return bkvoice_wss_error(wss, ret < 0 ? ret : -EIO, false);
    }

  wss->rx_size = 0;
  wss->rx_offset = 0;
  __atomic_store_n(&wss->tls_open, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->upgraded, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->interrupted, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->peer_closed, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->faulted, false, __ATOMIC_RELEASE);
  __atomic_store_n(&wss->last_error, 0, __ATOMIC_RELEASE);
  return 0;
}

static const struct bkvoice_transport_ops_s g_bkvoice_wss_transport_ops =
{
  .open = bkvoice_wss_transport_open,
  .send = bkvoice_wss_transport_send,
  .recv = bkvoice_wss_transport_recv,
  .interrupt = bkvoice_wss_transport_interrupt,
  .close = bkvoice_wss_transport_close,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bkvoice_wss_initialize(
  struct bkvoice_wss_s *wss,
  const struct bkvoice_wss_tls_ops_s *tls_ops, void *tls_context,
  const struct bkvoice_wss_config_s *config)
{
  int ret;

  if (wss == NULL || tls_context == NULL || config == NULL ||
      !bkvoice_wss_tls_ops_valid(tls_ops) || config->port == 0 ||
      !bkvoice_wss_host_valid(config->host) ||
      !bkvoice_wss_path_valid(config->path) ||
      !bkvoice_wss_subprotocol_valid(config->subprotocol))
    {
      return -EINVAL;
    }

  memset(wss, 0, sizeof(*wss));
  memcpy(&wss->tls_ops, tls_ops, sizeof(*tls_ops));
  wss->tls_context = tls_context;
  wss->port = config->port;
  memcpy(wss->host, config->host, strlen(config->host) + 1u);
  memcpy(wss->path, config->path, strlen(config->path) + 1u);
  memcpy(wss->subprotocol, config->subprotocol,
         strlen(config->subprotocol) + 1u);

  ret = pthread_mutex_init(&wss->tx_lock, NULL);
  if (ret != 0)
    {
      memset(wss, 0, sizeof(*wss));
      return -ret;
    }

  wss->initialized = true;
  return 0;
}

int bkvoice_wss_uninitialize(struct bkvoice_wss_s *wss)
{
  int ret;

  if (wss == NULL || !wss->initialized)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&wss->tls_open, __ATOMIC_ACQUIRE))
    {
      return -EBUSY;
    }

  ret = pthread_mutex_destroy(&wss->tx_lock);
  if (ret != 0)
    {
      return -ret;
    }

  memset(wss, 0, sizeof(*wss));
  return 0;
}

const struct bkvoice_transport_ops_s *bkvoice_wss_transport_ops(void)
{
  return &g_bkvoice_wss_transport_ops;
}

void bkvoice_wss_snapshot(const struct bkvoice_wss_s *wss,
                          struct bkvoice_wss_snapshot_s *snapshot)
{
  if (wss == NULL || snapshot == NULL)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->tx_messages = __atomic_load_n(&wss->tx_messages,
                                          __ATOMIC_ACQUIRE);
  snapshot->rx_messages = __atomic_load_n(&wss->rx_messages,
                                          __ATOMIC_ACQUIRE);
  snapshot->ping_count = __atomic_load_n(&wss->ping_count,
                                         __ATOMIC_ACQUIRE);
  snapshot->pong_count = __atomic_load_n(&wss->pong_count,
                                         __ATOMIC_ACQUIRE);
  snapshot->last_error = __atomic_load_n(&wss->last_error,
                                         __ATOMIC_ACQUIRE);
  snapshot->tls_open = __atomic_load_n(&wss->tls_open,
                                       __ATOMIC_ACQUIRE);
  snapshot->upgraded = __atomic_load_n(&wss->upgraded,
                                       __ATOMIC_ACQUIRE);
  snapshot->interrupted = __atomic_load_n(&wss->interrupted,
                                          __ATOMIC_ACQUIRE);
  snapshot->peer_closed = __atomic_load_n(&wss->peer_closed,
                                          __ATOMIC_ACQUIRE);
  snapshot->faulted = __atomic_load_n(&wss->faulted,
                                      __ATOMIC_ACQUIRE);
  snapshot->initialized = wss->initialized;
}
