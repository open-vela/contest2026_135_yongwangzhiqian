/****************************************************************************
 * app/bk7258/bk7258_voice_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-visible operator command for the AP-owned BKVoice service.
 ****************************************************************************/

#include <nuttx/config.h>

#include "bk7258_voice_product.h"
#include "bk7258_voice_button.h"

#if !defined(CONFIG_BK7258_VOICE_PTT_BUTTON) && \
    !defined(CONFIG_BK7258_PRODUCT_KEYS)
#  define bkvoice_button_running() false
#  define bkvoice_button_stop() (-ENOSYS)
#endif
#include "bk7258_voice_protocol.h"
#include "bk7258_preferences.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define BKVOICE_STRESS_MAX 100ul

static void bkvoice_usage(void)
{
  fprintf(stderr,
          "usage: bkvoice status\n"
          "       bkvoice verify <voicepack.ini>\n"
          "       bkvoice play <voicepack.ini> <clip-id>\n"
          "       bkvoice stress <voicepack.ini> <clip-id> <count>\n"
          "       bkvoice provision\n"
          "       bkvoice connect\n"
          "       bkvoice buttons\n"
          "       bkvoice disconnect\n"
          "       bkvoice clear\n"
          "       bkvoice prefs\n"
          "       bkvoice prefs volume <0..100>\n"
          "       bkvoice prefs persona <gentle|playful|quiet|serious|tsundere_lite>\n");
}

static int bkvoice_copy_arg(char *target, size_t size, const char *source)
{
  size_t length = strlen(source);

  if (length == 0 || length >= size)
    {
      return -ENAMETOOLONG;
    }

  memcpy(target, source, length + 1u);
  return OK;
}

static void bkvoice_wipe(void *buffer, size_t size);

static int bkvoice_request_timeout(uint16_t command, const char *manifest,
                                   const char *clip_id,
                                   struct bkvoice_rpc_response_s *response,
                                   unsigned int timeout_ms)
{
  struct bkvoice_rpc_request_s request;
  int ret;

  memset(&request, 0, sizeof(request));
  memset(response, 0, sizeof(*response));
  request.command = command;

  if (manifest != NULL)
    {
      ret = bkvoice_copy_arg(request.manifest, sizeof(request.manifest),
                             manifest);
      if (ret < 0)
        {
          return ret;
        }
    }

  if (clip_id != NULL)
    {
      ret = bkvoice_copy_arg(request.clip_id, sizeof(request.clip_id),
                             clip_id);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = bkvoice_rpc_exchange(&request, response, timeout_ms);
  bkvoice_wipe(&request, sizeof(request));
  return ret < 0 ? ret : response->status;
}

static int bkvoice_request(uint16_t command, const char *manifest,
                           const char *clip_id,
                           struct bkvoice_rpc_response_s *response)
{
  return bkvoice_request_timeout(command, manifest, clip_id, response,
                                 BKVOICE_RPC_REPLY_WAIT_MS);
}

static int bkvoice_status(void)
{
  struct bkvoice_rpc_response_s response;
  int ret = bkvoice_request(BKVOICE_RPC_STATUS, NULL, NULL, &response);

  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE STATUS FAIL ret=%d\n", ret);
      return ret;
    }

  printf("BKVOICE STATUS provider=ap ready=%u configured=%u connected=%u "
         "gateway_ready=%u tls_available=%u\n",
         (response.flags & BKVOICE_STATUS_SERVICE_READY) != 0 ? 1u : 0u,
         (response.flags & BKVOICE_STATUS_CONFIGURED) != 0 ? 1u : 0u,
         (response.flags & BKVOICE_STATUS_CONNECTED) != 0 ? 1u : 0u,
         (response.flags & BKVOICE_STATUS_GATEWAY_READY) != 0 ? 1u : 0u,
         (response.flags & BKVOICE_STATUS_TLS_AVAILABLE) != 0 ? 1u : 0u);
  printf("BKVOICE PTT ready=%u link=%u pressed=%u turn=%lu presses=%lu "
         "last_error=%ld\n",
         (response.flags & BKVOICE_STATUS_PTT_OWNER_READY) != 0 ? 1u : 0u,
         (response.flags & BKVOICE_STATUS_PTT_LINK) != 0 ? 1u : 0u,
         (response.flags & BKVOICE_STATUS_PTT_PRESSED) != 0 ? 1u : 0u,
         (unsigned long)response.result.live.turn_state,
         (unsigned long)response.result.live.presses,
         (long)response.result.live.last_error);
  printf("BKVOICE FRAMES tx=%lu rx=%lu\n",
         (unsigned long)response.data_bytes,
         (unsigned long)response.duration_ms);
  if (response.flags & BKVOICE_STATUS_CLOUD_MODE)
    {
      printf("BKVOICE CLOUD ready=%u busy=%u turn_known=%u\n",
             !!(response.flags & BKVOICE_STATUS_CLOUD_READY),
             !!(response.flags & BKVOICE_STATUS_CLOUD_BUSY),
             response.result.live.turn_state != UINT32_MAX);
    }
  return OK;
}

static int bkvoice_verify(const char *manifest,
                          struct bkvoice_rpc_response_s *response)
{
  int ret = bkvoice_request(BKVOICE_RPC_VERIFY, manifest, NULL, response);

  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE VERIFY FAIL ret=%d line=%lu\n", ret,
              (unsigned long)response->result.pack.error_line);
      return ret;
    }

  printf("BKVOICE VERIFY PASS version=%lu speaker_id=%s clips=%lu "
         "authorization=explicit-consent disclosure=synthetic-voice\n",
         (unsigned long)response->result.pack.version,
         response->speaker_id,
         (unsigned long)response->result.pack.clip_count);
  return OK;
}

static int bkvoice_play(const char *manifest, const char *clip_id)
{
  struct bkvoice_rpc_response_s response;
  int ret;

  /* Verification is a separate round trip so the CP-visible disclosure is
   * emitted before AP starts producing synthetic audio.
   */

  ret = bkvoice_verify(manifest, &response);
  if (ret < 0)
    {
      return ret;
    }

  printf("BKVOICE SYNTHETIC speaker_id=%s clip=%s\n",
         response.speaker_id, clip_id);
  ret = bkvoice_request(BKVOICE_RPC_PLAY, manifest, clip_id, &response);
  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE PLAY FAIL ret=%d line=%lu clip=%s\n", ret,
              (unsigned long)response.result.pack.error_line, clip_id);
      return ret;
    }

  printf("BKVOICE PLAY PASS clip=%s bytes=%lu duration_ms=%lu\n",
         clip_id, (unsigned long)response.data_bytes,
         (unsigned long)response.duration_ms);
  return OK;
}

static int bkvoice_parse_range(const char *text, unsigned long minimum,
                               unsigned long maximum,
                               unsigned long *value_out)
{
  char *end;
  unsigned long value;

  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value < minimum ||
      value > maximum)
    {
      return -EINVAL;
    }

  *value_out = value;
  return OK;
}

static int bkvoice_stress(const char *manifest, const char *clip_id,
                          const char *count_text)
{
  struct bkvoice_rpc_response_s response;
  unsigned long count;
  unsigned long iteration;
  int ret;

  ret = bkvoice_parse_range(count_text, 1, BKVOICE_STRESS_MAX, &count);
  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE STRESS FAIL ret=%d count=%s max=%lu\n",
              ret, count_text, BKVOICE_STRESS_MAX);
      return ret;
    }

  ret = bkvoice_verify(manifest, &response);
  if (ret < 0)
    {
      return ret;
    }

  printf("BKVOICE SYNTHETIC speaker_id=%s clip=%s mode=stress count=%lu\n",
         response.speaker_id, clip_id, count);
  for (iteration = 1; iteration <= count; iteration++)
    {
      ret = bkvoice_request(BKVOICE_RPC_PLAY, manifest, clip_id, &response);
      if (ret < 0)
        {
          fprintf(stderr,
                  "BKVOICE STRESS FAIL iteration=%lu/%lu ret=%d line=%lu\n",
                  iteration, count, ret,
                  (unsigned long)response.result.pack.error_line);
          return ret;
        }

      if (iteration == count || iteration % 10ul == 0)
        {
          printf("BKVOICE STRESS PROGRESS completed=%lu/%lu\n",
                 iteration, count);
        }
    }

  printf("BKVOICE STRESS PASS count=%lu clip=%s\n", count, clip_id);
  return OK;
}

static void bkvoice_wipe(void *buffer, size_t size)
{
  volatile unsigned char *cursor = buffer;

  while (size-- > 0)
    {
      *cursor++ = 0;
    }
}

static int bkvoice_hidden_readline(char *line, size_t size)
{
  struct timespec deadline;
  size_t length = 0;
  int ret;

  if (size < 2 || clock_gettime(CLOCK_MONOTONIC, &deadline) < 0)
    {
      return size < 2 ? -EINVAL : (errno > 0 ? -errno : -EIO);
    }

  deadline.tv_sec += 10;
  for (;;)
    {
      struct pollfd pfd;
      struct timespec now;
      int64_t remaining;
      unsigned char byte;
      ssize_t count;

      if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        {
          return errno > 0 ? -errno : -EIO;
        }

      remaining = (int64_t)(deadline.tv_sec - now.tv_sec) * 1000 +
                  (int64_t)(deadline.tv_nsec - now.tv_nsec) / 1000000;
      if (remaining <= 0)
        {
          return -ETIMEDOUT;
        }

      memset(&pfd, 0, sizeof(pfd));
      pfd.fd = STDIN_FILENO;
      pfd.events = POLLIN;
      ret = poll(&pfd, 1, remaining > INT_MAX ? INT_MAX : (int)remaining);
      if (ret < 0)
        {
          return errno == EINTR ? -EINTR : (errno > 0 ? -errno : -EIO);
        }

      if (ret == 0)
        {
          return -ETIMEDOUT;
        }

      count = read(STDIN_FILENO, &byte, 1);
      if (count != 1)
        {
          return count == 0 ? -EIO : (errno > 0 ? -errno : -EIO);
        }

      if (byte == '\r')
        {
          struct pollfd tail;

          memset(&tail, 0, sizeof(tail));
          tail.fd = STDIN_FILENO;
          tail.events = POLLIN;
          if (poll(&tail, 1, 0) > 0 && (tail.revents & POLLIN) != 0)
            {
              count = read(STDIN_FILENO, &byte, 1);
              if (count != 1 || byte != '\n')
                {
                  return -EINVAL;
                }
            }

          line[length] = '\0';
          return length == 0 ? -EINVAL : OK;
        }

      if (byte == '\n')
        {
          line[length] = '\0';
          return length == 0 ? -EINVAL : OK;
        }

      if (byte < 0x20u || byte == 0x7fu ||
          (byte >= 0x80u && byte <= 0x9fu) || length + 1u >= size)
        {
          return -EINVAL;
        }

      line[length++] = (char)byte;
    }
}

static bool bkvoice_hex(const char *text, size_t bytes)
{
  size_t index;

  for (index = 0; index < bytes * 2u; index++)
    {
      char value = text[index];

      if (!((value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f') ||
            (value >= 'A' && value <= 'F')))
        {
          return false;
        }
    }

  return text[bytes * 2u] == '\0';
}

static int bkvoice_provision(void)
{
  struct termios original;
  struct termios hidden;
  struct bkvoice_rpc_response_s response;
  char line[BKVOICE_PROVISION_CHUNK_BYTES * 2u + 1u];
  char number[sizeof("8192")];
  char offset_text[sizeof("4294967295")];
  unsigned long total;
  uint32_t offset = 0;
  bool termios_changed = false;
  int ret;

  memset(&response, 0, sizeof(response));
  memset(line, 0, sizeof(line));
  memset(number, 0, sizeof(number));
  memset(offset_text, 0, sizeof(offset_text));
#if !defined(CONFIG_BK7258_PRODUCT_KEYS) && \
    !defined(CONFIG_BK7258_VOICE_PTT_AUTOSTART)
  if (bkvoice_button_running())
    {
      ret = -EBUSY;
      goto out;
    }
#endif
  /* Product GPIO sampling also serves first-time claiming and stays alive
   * before credentials exist. CONFIG_BEGIN on AP owns the actual audio,
   * pairing, network and identity transaction exclusion.
   */

  if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &original) < 0)
    {
      ret = -ENOTTY;
      goto out;
    }

  hidden = original;
  hidden.c_lflag &= ~(ECHO | ECHONL);
  if (tcsetattr(STDIN_FILENO, TCSANOW, &hidden) < 0)
    {
      ret = errno > 0 ? -errno : -EIO;
      goto out;
    }

  termios_changed = true;
  (void)tcflush(STDIN_FILENO, TCIFLUSH);
  printf("BKVOICE PROVISION READY\n");
  fflush(stdout);

  ret = bkvoice_hidden_readline(line, sizeof(line));
  if (ret < 0 || bkvoice_parse_range(line, 1, BKVOICE_PROVISION_MAX_BYTES,
                                      &total) < 0 ||
      snprintf(number, sizeof(number), "%lu", total) < 0)
    {
      ret = ret < 0 ? ret : -EINVAL;
      goto out;
    }

  ret = bkvoice_request_timeout(BKVOICE_RPC_CONFIG_BEGIN, number, NULL,
                                &response, 3000u);
  if (ret < 0)
    {
      goto out;
    }

  printf("BKVOICE PROVISION NEXT offset=0\n");
  fflush(stdout);
  while (offset < total)
    {
      size_t bytes = total - offset;
      uint32_t accepted;

      if (bytes > BKVOICE_PROVISION_CHUNK_BYTES)
        {
          bytes = BKVOICE_PROVISION_CHUNK_BYTES;
        }

      memset(line, 0, sizeof(line));
      ret = bkvoice_hidden_readline(line, sizeof(line));
      if (ret < 0 || !bkvoice_hex(line, bytes) ||
          snprintf(offset_text, sizeof(offset_text), "%lu",
                   (unsigned long)offset) < 0)
        {
          ret = ret < 0 ? ret : -EINVAL;
          goto out;
        }

      ret = bkvoice_request_timeout(BKVOICE_RPC_CONFIG_DATA, line,
                                    offset_text, &response, 3000u);
      if (ret < 0)
        {
          goto out;
        }

      accepted = response.data_bytes;
      if (accepted != offset + bytes || accepted > total)
        {
          ret = -EPROTO;
          goto out;
        }

      offset = accepted;
      printf("BKVOICE PROVISION NEXT offset=%lu\n", (unsigned long)offset);
      fflush(stdout);
    }

  /* The AP file worker owns durable identity publication; each retry is a
   * fresh RPC sequence polling the same copied record, never another write.
   */
  struct timespec deadline;
  if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) { ret = -errno; goto out; }
  deadline.tv_sec += 30;
  for (;;)
    {
      struct timespec now;
      if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) { ret = -errno; break; }
      int64_t remaining = (int64_t)(deadline.tv_sec - now.tv_sec) * 1000 +
                          (deadline.tv_nsec - now.tv_nsec) / 1000000;
      if (remaining <= 0) { ret = -ETIMEDOUT; break; }
      ret = bkvoice_request_timeout(BKVOICE_RPC_CONFIG_COMMIT, NULL, NULL,
                                    &response, remaining < 15000 ? remaining : 15000);
      if (ret != -EAGAIN) break;
      usleep(100000);
    }

out:
  if (termios_changed)
    {
      if (ret < 0)
        {
          (void)tcflush(STDIN_FILENO, TCIFLUSH);
        }

      if (tcsetattr(STDIN_FILENO, TCSANOW, &original) < 0 && ret >= 0)
        {
          ret = errno > 0 ? -errno : -EIO;
        }

      if (ret < 0)
        {
          (void)tcflush(STDIN_FILENO, TCIFLUSH);
        }
    }

  bkvoice_wipe(line, sizeof(line));
  bkvoice_wipe(number, sizeof(number));
  bkvoice_wipe(offset_text, sizeof(offset_text));
  bkvoice_wipe(&response, sizeof(response));
  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE PROVISION FAIL ret=%d\n", ret);
    }
  else
    {
      printf("BKVOICE PROVISION PASS bytes=%lu\n", total);
    }

  return ret;
}

static int bkvoice_button_start_for_connect(void)
{
#if defined(CONFIG_BK7258_VOICE_PTT_BUTTON) || \
    defined(CONFIG_BK7258_PRODUCT_KEYS)
  if (!bkvoice_button_running())
    {
#ifdef CONFIG_BK7258_PRODUCT_KEYS
      return bkvoice_button_start("/dev/buttons", false);
#else
      return bkvoice_button_start(CONFIG_BK7258_VOICE_PTT_DEVPATH,
                                  CONFIG_BK7258_VOICE_PTT_ACTIVE_LOW);
#endif
    }
#endif

  return OK;
}

static int bkvoice_disconnect(void)
{
  struct bkvoice_rpc_response_s response;
  int button_ret = OK;
  int rpc_ret;

#if !defined(CONFIG_BK7258_PRODUCT_KEYS) && \
    !defined(CONFIG_BK7258_VOICE_PTT_AUTOSTART)
  if (bkvoice_button_running())
    {
      button_ret = bkvoice_button_stop();
    }
#endif

  rpc_ret = bkvoice_request_timeout(BKVOICE_RPC_DISCONNECT, NULL, NULL,
                                    &response, 15000u);
  if (button_ret < 0 || rpc_ret < 0)
    {
      fprintf(stderr, "BKVOICE DISCONNECT FAIL button=%d rpc=%d\n",
              button_ret, rpc_ret);
      return button_ret < 0 ? button_ret : rpc_ret;
    }

  printf("BKVOICE DISCONNECT PASS\n");
  return OK;
}

static int bkvoice_connect(void)
{
  struct bkvoice_rpc_response_s response;
  int ret;

  ret = bkvoice_button_start_for_connect();
  if (ret >= 0)
    {
      ret = bkvoice_request_timeout(BKVOICE_RPC_CONNECT, NULL, NULL,
                                    &response, 15000u);
    }

  if (ret < 0)
    {
#if !defined(CONFIG_BK7258_PRODUCT_KEYS) && \
    !defined(CONFIG_BK7258_VOICE_PTT_AUTOSTART)
      if (bkvoice_button_running())
        {
          (void)bkvoice_button_stop();
        }
#endif

      fprintf(stderr, "BKVOICE CONNECT FAIL ret=%d\n", ret);
      return ret;
    }

  printf("BKVOICE CONNECT PASS\n");
  return OK;
}

static int bkvoice_clear(void)
{
  struct bkvoice_rpc_response_s response;
  int disconnect_ret = bkvoice_disconnect();
  int clear_ret = bkvoice_request_timeout(BKVOICE_RPC_CONFIG_CLEAR, NULL,
                                          NULL, &response, 3000u);

  if (disconnect_ret < 0 || clear_ret < 0)
    {
      fprintf(stderr, "BKVOICE CLEAR FAIL disconnect=%d clear=%d\n",
              disconnect_ret, clear_ret);
      return disconnect_ret < 0 ? disconnect_ret : clear_ret;
    }

  printf("BKVOICE CLEAR PASS\n");
  return OK;
}

static int bkvoice_preferences(uint16_t command, const char *value)
{
  struct bkvoice_rpc_response_s response;
  const char *persona;
  int ret = bkvoice_request(command, value, NULL, &response);

  if (ret < 0)
    {
      fprintf(stderr, "BKVOICE PREFS FAIL ret=%d\n", ret);
      return ret;
    }

  persona = bk7258_preferences_persona_name(
    response.result.preferences.persona);
  if (persona == NULL || response.result.preferences.volume_percent > 100u)
    {
      return -EPROTO;
    }

  printf("BKVOICE PREFS desired_volume=%lu desired_persona=%s "
         "default_flags=%lu\n",
         (unsigned long)response.result.preferences.volume_percent, persona,
         (unsigned long)response.result.preferences.default_flags);
  return OK;
}

int main(int argc, char **argv)
{
  struct bkvoice_rpc_response_s response;
  int ret;

  if (argc == 2 && strcmp(argv[1], "status") == 0)
    {
      ret = bkvoice_status();
    }
  else if (argc == 3 && strcmp(argv[1], "verify") == 0)
    {
      ret = bkvoice_verify(argv[2], &response);
    }
  else if (argc == 4 && strcmp(argv[1], "play") == 0)
    {
      ret = bkvoice_play(argv[2], argv[3]);
    }
  else if (argc == 5 && strcmp(argv[1], "stress") == 0)
    {
      ret = bkvoice_stress(argv[2], argv[3], argv[4]);
    }
  else if (argc == 2 && strcmp(argv[1], "provision") == 0)
    {
      ret = bkvoice_provision();
    }
  else if (argc == 2 && strcmp(argv[1], "connect") == 0)
    {
      ret = bkvoice_connect();
    }
  else if (argc == 2 && strcmp(argv[1], "buttons") == 0)
    {
#if defined(CONFIG_BK7258_VOICE_PTT_BUTTON) || \
    defined(CONFIG_BK7258_PRODUCT_KEYS)
      ret = bkvoice_button_start_for_connect();
#else
      ret = -ENOSYS;
#endif
      printf("BKVOICE BUTTONS ready=%u ret=%d\n",
             bkvoice_button_running() ? 1u : 0u, ret);
    }
#ifdef CONFIG_BK7258_VOICE_HIL_TEST
  else if (argc == 3 && strcmp(argv[1], "test-capture") == 0)
    {
      ret = bkvoice_request(BKVOICE_RPC_HIL_CAPTURE, argv[2], NULL, &response);
      printf("BKVOICE HIL CAPTURE scheduled=%u ret=%d physical=0\n",
             ret == 0 ? 1u : 0u, ret);
    }
#endif
  else if (argc == 2 && strcmp(argv[1], "disconnect") == 0)
    {
      ret = bkvoice_disconnect();
    }
  else if (argc == 2 && strcmp(argv[1], "clear") == 0)
    {
      ret = bkvoice_clear();
    }
  else if (argc == 2 && strcmp(argv[1], "prefs") == 0)
    {
      ret = bkvoice_preferences(BKVOICE_RPC_PREFS_GET, NULL);
    }
  else if (argc == 4 && strcmp(argv[1], "prefs") == 0 &&
           strcmp(argv[2], "volume") == 0)
    {
      ret = bkvoice_preferences(BKVOICE_RPC_PREFS_VOLUME, argv[3]);
    }
  else if (argc == 4 && strcmp(argv[1], "prefs") == 0 &&
           strcmp(argv[2], "persona") == 0)
    {
      ret = bkvoice_preferences(BKVOICE_RPC_PREFS_PERSONA, argv[3]);
    }
  else
    {
      bkvoice_usage();
      return EXIT_FAILURE;
    }

  return ret == OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
