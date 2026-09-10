/****************************************************************************
 * app/bk7258/bk7258_voice_protocol.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Versioned CP command to AP voice-service wire contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_PROTOCOL_H
#define __APP_BK7258_BK7258_VOICE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "bk7258_voice_pack.h"

#define BKVOICE_RPC_MAGIC            0x564f4943u /* VOIC */
#define BKVOICE_RPC_VERSION          1u
#define BKVOICE_RPC_ENDPOINT         "bkvoice-v1"
#define BKVOICE_RPC_ENDPOINT_WAIT_MS 3000u
#define BKVOICE_RPC_SEND_WAIT_MS     1000u
#define BKVOICE_RPC_REPLY_WAIT_MS    120000u
#define BKVOICE_RPC_ATTEMPTS         2u

#define BKVOICE_STATUS_SERVICE_READY (1u << 0)
#define BKVOICE_STATUS_BLOCK_PRESENT (1u << 1)
#define BKVOICE_STATUS_LOCAL_ONLY    (1u << 2)
#define BKVOICE_STATUS_PTT_OWNER_READY (1u << 3)
#define BKVOICE_STATUS_CONFIGURED      (1u << 4)
#define BKVOICE_STATUS_CONNECTED       (1u << 5)
#define BKVOICE_STATUS_GATEWAY_READY   (1u << 6)
#define BKVOICE_STATUS_PTT_LINK        (1u << 7)
#define BKVOICE_STATUS_PTT_PRESSED     (1u << 8)
#define BKVOICE_STATUS_TLS_AVAILABLE   (1u << 9)
#define BKVOICE_STATUS_CLOUD_MODE      (1u << 10)
#define BKVOICE_STATUS_CLOUD_READY     (1u << 11)
#define BKVOICE_STATUS_CLOUD_BUSY      (1u << 12)

#define BKVOICE_PROVISION_MAX_BYTES    8192u
#define BKVOICE_PROVISION_CHUNK_BYTES  64u

#define BKVOICE_PREFS_DEFAULT_VOLUME  (1u << 0)
#define BKVOICE_PREFS_DEFAULT_PERSONA (1u << 1)

enum bkvoice_rpc_command_e
{
  BKVOICE_RPC_STATUS = 1,
  BKVOICE_RPC_VERIFY = 2,
  BKVOICE_RPC_PLAY = 3,
  BKVOICE_RPC_CONFIG_BEGIN = 4,
  BKVOICE_RPC_CONFIG_DATA = 5,
  BKVOICE_RPC_CONFIG_COMMIT = 6,
  BKVOICE_RPC_CONFIG_CLEAR = 7,
  BKVOICE_RPC_CONNECT = 8,
  BKVOICE_RPC_DISCONNECT = 9,
  BKVOICE_RPC_PREFS_GET = 10,
  BKVOICE_RPC_PREFS_VOLUME = 11,
  BKVOICE_RPC_PREFS_PERSONA = 12,
  BKVOICE_RPC_HIL_CAPTURE = 0x100,
  BKVOICE_RPC_RESPONSE = 0x8000,
};

struct bkvoice_rpc_pack_result_s
{
  uint32_t version;
  uint32_t clip_count;
  uint32_t error_line;
};

union bkvoice_rpc_result_u
{
  struct bkvoice_rpc_pack_result_s pack;
  struct
  {
    uint32_t turn_state;
    uint32_t presses;
    int32_t last_error;
  } live;
  struct
  {
    uint32_t volume_percent;
    uint32_t persona;
    uint32_t default_flags;
  } preferences;
};

struct bkvoice_rpc_request_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  char manifest[BKVOICE_PATH_SIZE];
  char clip_id[BKVOICE_ID_SIZE];
};

struct bkvoice_rpc_response_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t command;
  uint32_t session;
  uint32_t sequence;
  int32_t status;
  uint32_t flags;
  union bkvoice_rpc_result_u result;
  uint32_t data_bytes;
  uint32_t duration_ms;
  char speaker_id[BKVOICE_ID_SIZE];
};

/* CP and AP are built separately, so make accidental ABI drift a compile
 * failure instead of silently changing the RPMsg wire layout.  All integer
 * fields use the native little-endian BK7258 representation.
 */

_Static_assert(sizeof(struct bkvoice_rpc_request_s) == 320,
               "bkvoice request wire size changed");
_Static_assert(offsetof(struct bkvoice_rpc_request_s, manifest) == 16,
               "bkvoice request manifest offset changed");
_Static_assert(offsetof(struct bkvoice_rpc_request_s, clip_id) == 272,
               "bkvoice request clip offset changed");
_Static_assert(sizeof(struct bkvoice_rpc_response_s) == 92,
               "bkvoice response wire size changed");
_Static_assert(offsetof(struct bkvoice_rpc_response_s, status) == 16,
               "bkvoice response status offset changed");
_Static_assert(offsetof(struct bkvoice_rpc_response_s, result) == 24,
               "bkvoice response result offset changed");
_Static_assert(offsetof(struct bkvoice_rpc_response_s, speaker_id) == 44,
               "bkvoice response speaker offset changed");

int bkvoice_rpc_client_initialize(void);
int bkvoice_rpc_exchange(struct bkvoice_rpc_request_s *request,
                         struct bkvoice_rpc_response_s *response,
                         unsigned int timeout_ms);

#endif /* __APP_BK7258_BK7258_VOICE_PROTOCOL_H */
