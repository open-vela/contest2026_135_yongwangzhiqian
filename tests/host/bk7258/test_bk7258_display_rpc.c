/****************************************************************************
 * Host contract tests for the BKDisplay AP request dispatcher.
 ****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bk7258_display_rpc_core.h"
#include "bk7258_display_service.h"

static struct bkdisplay_service_status_s g_status;
static char g_requested_expression[BKDISPLAY_EXPRESSION_SIZE];
static int g_set_result;
static int g_status_result;
static unsigned int g_set_calls;
static unsigned int g_mapping_test_calls;
static int g_mapping_test_result;

int bk7258_display_set_expression(const char *expression)
{
  size_t length;

  g_set_calls++;
  length = strnlen(expression, sizeof(g_requested_expression) - 1u);
  memcpy(g_requested_expression, expression, length);
  g_requested_expression[length] = '\0';
  if (g_set_result == 0)
    {
      memcpy(g_status.expression, g_requested_expression, length + 1u);
      g_status.render_sequence++;
    }

  return g_set_result;
}

int bk7258_display_get_status(struct bkdisplay_service_status_s *status)
{
  if (g_status_result < 0)
    {
      return g_status_result;
    }

  *status = g_status;
  return 0;
}

int bk7258_display_show_mapping_test(void)
{
  g_mapping_test_calls++;
  if (g_mapping_test_result == 0)
    {
      memcpy(g_status.expression, "mapping-test", sizeof("mapping-test"));
      g_status.render_sequence++;
    }

  return g_mapping_test_result;
}

static void reset_fixture(void)
{
  memset(&g_status, 0, sizeof(g_status));
  memset(g_requested_expression, 0, sizeof(g_requested_expression));
  g_status.state = BKDISPLAY_SERVICE_READY;
  g_status.screen_count = 2;
  g_status.render_sequence = 7;
  g_status.pack_revision = 3;
  memcpy(g_status.expression, "neutral", sizeof("neutral"));
  memcpy(g_status.pack_id, "shaniu-default-v1",
         sizeof("shaniu-default-v1"));
  g_set_result = 0;
  g_status_result = 0;
  g_set_calls = 0;
  g_mapping_test_calls = 0;
  g_mapping_test_result = 0;
}

static struct bkdisplay_rpc_request_s make_request(uint16_t command,
                                                   const char *expression)
{
  struct bkdisplay_rpc_request_s request;

  memset(&request, 0, sizeof(request));
  request.magic = BKDISPLAY_RPC_MAGIC;
  request.version = BKDISPLAY_RPC_VERSION;
  request.command = command;
  request.session = 41;
  request.sequence = 9;
  if (expression != NULL)
    {
      assert(strlen(expression) < sizeof(request.expression));
      memcpy(request.expression, expression, strlen(expression) + 1u);
    }

  return request;
}

static void test_request_validation(void)
{
  struct bkdisplay_rpc_request_s request;

  request = make_request(BKDISPLAY_RPC_STATUS, NULL);
  assert(bkdisplay_rpc_request_valid(&request));

  request = make_request(BKDISPLAY_RPC_SET_EXPRESSION, "happy");
  assert(bkdisplay_rpc_request_valid(&request));

  request = make_request(BKDISPLAY_RPC_SHOW_MAPPING_TEST, NULL);
  assert(bkdisplay_rpc_request_valid(&request));

  request = make_request(BKDISPLAY_RPC_SHOW_MAPPING_TEST, "happy");
  assert(!bkdisplay_rpc_request_valid(&request));

  request = make_request(BKDISPLAY_RPC_STATUS, "happy");
  assert(!bkdisplay_rpc_request_valid(&request));

  request = make_request(BKDISPLAY_RPC_SET_EXPRESSION, NULL);
  assert(!bkdisplay_rpc_request_valid(&request));

  request = make_request(BKDISPLAY_RPC_SET_EXPRESSION, "happy");
  memset(request.expression, 'x', sizeof(request.expression));
  assert(!bkdisplay_rpc_request_valid(&request));

  request = make_request(BKDISPLAY_RPC_STATUS, NULL);
  request.reserved = 1;
  assert(!bkdisplay_rpc_request_valid(&request));

  request = make_request(99, NULL);
  assert(!bkdisplay_rpc_request_valid(&request));

  assert(!bkdisplay_rpc_request_valid(NULL));
}

static void test_status_snapshot(void)
{
  struct bkdisplay_rpc_request_s request;
  struct bkdisplay_rpc_response_s response;

  reset_fixture();
  request = make_request(BKDISPLAY_RPC_STATUS, NULL);
  assert(bkdisplay_rpc_handle_request(&request, &response) == 0);
  assert(response.magic == BKDISPLAY_RPC_MAGIC);
  assert(response.version == BKDISPLAY_RPC_VERSION);
  assert(response.command == BKDISPLAY_RPC_RESPONSE);
  assert(response.session == request.session);
  assert(response.sequence == request.sequence);
  assert(response.status == 0);
  assert(response.state == BKDISPLAY_RPC_STATE_READY);
  assert(response.last_error == 0);
  assert(response.screen_count == 2);
  assert(response.render_sequence == 7);
  assert(response.pack_revision == 3);
  assert(strcmp(response.expression, "neutral") == 0);
  assert(strcmp(response.pack_id, "shaniu-default-v1") == 0);
  assert((response.flags & BKDISPLAY_STATUS_SERVICE_READY) != 0);
  assert((response.flags & BKDISPLAY_STATUS_PACK_SELECTED) != 0);
  assert((response.flags & BKDISPLAY_STATUS_MAPPING_VERIFIED) == 0);
  assert(g_set_calls == 0);
}

static void test_mood_dispatch(void)
{
  struct bkdisplay_rpc_request_s request;
  struct bkdisplay_rpc_response_s response;

  reset_fixture();
  request = make_request(BKDISPLAY_RPC_SET_EXPRESSION, "happy");
  assert(bkdisplay_rpc_handle_request(&request, &response) == 0);
  assert(g_set_calls == 1);
  assert(strcmp(g_requested_expression, "happy") == 0);
  assert(strcmp(response.expression, "happy") == 0);
  assert(response.render_sequence == 8);
  assert(response.status == 0);
}

static void test_mood_failure_keeps_snapshot(void)
{
  struct bkdisplay_rpc_request_s request;
  struct bkdisplay_rpc_response_s response;

  reset_fixture();
  g_set_result = -ENOENT;
  g_status.state = BKDISPLAY_SERVICE_WAITING_ASSET;
  g_status.last_error = -ENOENT;
  request = make_request(BKDISPLAY_RPC_SET_EXPRESSION, "missing");
  assert(bkdisplay_rpc_handle_request(&request, &response) == -ENOENT);
  assert(response.status == -ENOENT);
  assert(response.state == BKDISPLAY_RPC_STATE_WAITING_ASSET);
  assert(response.last_error == -ENOENT);
  assert(g_set_calls == 1);
}

static void test_mapping_test_dispatch(void)
{
  struct bkdisplay_rpc_request_s request;
  struct bkdisplay_rpc_response_s response;

  reset_fixture();
  request = make_request(BKDISPLAY_RPC_SHOW_MAPPING_TEST, NULL);
  assert(bkdisplay_rpc_handle_request(&request, &response) == 0);
  assert(g_mapping_test_calls == 1);
  assert(g_set_calls == 0);
  assert(strcmp(response.expression, "mapping-test") == 0);
  assert(response.render_sequence == 8);
  assert((response.flags & BKDISPLAY_STATUS_MAPPING_VERIFIED) == 0);
}

static void test_mapping_test_failure_keeps_snapshot(void)
{
  struct bkdisplay_rpc_request_s request;
  struct bkdisplay_rpc_response_s response;

  reset_fixture();
  g_mapping_test_result = -EAGAIN;
  request = make_request(BKDISPLAY_RPC_SHOW_MAPPING_TEST, NULL);
  assert(bkdisplay_rpc_handle_request(&request, &response) == -EAGAIN);
  assert(response.status == -EAGAIN);
  assert(strcmp(response.expression, "neutral") == 0);
  assert(response.render_sequence == 7);
  assert(g_mapping_test_calls == 1);
}

static void test_status_failure_is_operation_failure(void)
{
  struct bkdisplay_rpc_request_s request;
  struct bkdisplay_rpc_response_s response;

  reset_fixture();
  g_status_result = -EIO;
  request = make_request(BKDISPLAY_RPC_STATUS, NULL);
  assert(bkdisplay_rpc_handle_request(&request, &response) == -EIO);
  assert(response.status == -EIO);
}

static void test_invalid_dispatch_gets_correlated_error(void)
{
  struct bkdisplay_rpc_request_s request;
  struct bkdisplay_rpc_response_s response;

  reset_fixture();
  request = make_request(BKDISPLAY_RPC_STATUS, "unexpected");
  assert(bkdisplay_rpc_handle_request(&request, &response) == -EINVAL);
  assert(response.status == -EINVAL);
  assert(response.session == request.session);
  assert(response.sequence == request.sequence);
  assert(g_set_calls == 0);
}

int main(void)
{
  test_request_validation();
  test_status_snapshot();
  test_mood_dispatch();
  test_mood_failure_keeps_snapshot();
  test_mapping_test_dispatch();
  test_mapping_test_failure_keeps_snapshot();
  test_status_failure_is_operation_failure();
  test_invalid_dispatch_gets_correlated_error();
  puts("BKDISPLAY_RPC_HOST_TEST_PASS");
  return 0;
}
