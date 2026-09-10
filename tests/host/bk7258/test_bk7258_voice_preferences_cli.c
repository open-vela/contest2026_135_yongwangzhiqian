/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_preferences_cli.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_voice_protocol.h"

int bkvoice_main(int argc, char **argv);

static struct bkvoice_rpc_request_s g_request;
static struct bkvoice_rpc_response_s g_response;
static int g_exchange_result;
static unsigned int g_exchange_calls;

int bkvoice_rpc_client_initialize(void)
{
  return 0;
}

int bkvoice_rpc_exchange(struct bkvoice_rpc_request_s *request,
                         struct bkvoice_rpc_response_s *response,
                         unsigned int timeout_ms)
{
  (void)timeout_ms;
  g_exchange_calls++;
  g_request = *request;
  *response = g_response;
  return g_exchange_result;
}

static void reset_response(void)
{
  memset(&g_request, 0, sizeof(g_request));
  memset(&g_response, 0, sizeof(g_response));
  g_exchange_result = 0;
  g_exchange_calls = 0;
  g_response.status = 0;
  g_response.result.preferences.volume_percent = 50u;
  g_response.result.preferences.persona = 0u;
}

static void assert_preferences_request(uint16_t command, const char *value)
{
  assert(g_exchange_calls == 1u);
  assert(g_request.command == command);
  assert(g_request.manifest[sizeof(g_request.manifest) - 1u] == '\0');
  assert(g_request.clip_id[0] == '\0');
  if (value == NULL)
    {
      assert(g_request.manifest[0] == '\0');
    }
  else
    {
      assert(strcmp(g_request.manifest, value) == 0);
    }
}

int main(void)
{
  char *get_argv[] = { "bkvoice", "prefs", NULL };
  char *volume_argv[] = { "bkvoice", "prefs", "volume", "73", NULL };
  char *persona_argv[] = { "bkvoice", "prefs", "persona", "quiet", NULL };

  reset_response();
  g_response.result.preferences.volume_percent = 50u;
  g_response.result.preferences.persona = 0u;
  assert(bkvoice_main(2, get_argv) == 0);
  assert_preferences_request(BKVOICE_RPC_PREFS_GET, NULL);

  reset_response();
  g_response.result.preferences.volume_percent = 73u;
  g_response.result.preferences.persona = 2u;
  assert(bkvoice_main(4, volume_argv) == 0);
  assert_preferences_request(BKVOICE_RPC_PREFS_VOLUME, "73");

  reset_response();
  g_response.result.preferences.volume_percent = 73u;
  g_response.result.preferences.persona = 2u;
  assert(bkvoice_main(4, persona_argv) == 0);
  assert_preferences_request(BKVOICE_RPC_PREFS_PERSONA, "quiet");

  reset_response();
  g_exchange_result = -ETIMEDOUT;
  assert(bkvoice_main(2, get_argv) == 1);
  assert_preferences_request(BKVOICE_RPC_PREFS_GET, NULL);

  reset_response();
  g_response.result.preferences.persona = 99u;
  assert(bkvoice_main(2, get_argv) == 1);
  assert_preferences_request(BKVOICE_RPC_PREFS_GET, NULL);

  puts("BK7258_VOICE_PREFERENCES_CLI_HOST_PASS");
  return 0;
}
