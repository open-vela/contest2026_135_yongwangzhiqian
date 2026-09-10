/****************************************************************************
 * app/bk7258/bk7258_voice_companion.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Transport-independent companion-v1 framing and turn-state contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_COMPANION_H
#define __APP_BK7258_BK7258_VOICE_COMPANION_H

#include <stddef.h>
#include <stdint.h>

#define BKVOICE_COMPANION_MAGIC             0x424b5631u /* BKV1 */
#define BKVOICE_COMPANION_VERSION           1u
#define BKVOICE_COMPANION_HEADER_BYTES      40u
#define BKVOICE_COMPANION_AUDIO_FRAME_BYTES 640u
#define BKVOICE_COMPANION_MAX_PAYLOAD       (64u * 1024u)
#define BKVOICE_COMPANION_MAX_WINDOW        (256u * 1024u)

#define BKVOICE_COMPANION_FLAG_SYNTHETIC    (1u << 0)
#define BKVOICE_COMPANION_FLAG_END_OF_STREAM (1u << 1)
#define BKVOICE_COMPANION_FLAG_MASK         \
  (BKVOICE_COMPANION_FLAG_SYNTHETIC |       \
   BKVOICE_COMPANION_FLAG_END_OF_STREAM)

/* Optional HELLO payload: uint32 big-endian capability bitmap. Empty HELLO
 * is a legacy peer with no advertised controls. Volume responses carry
 * request sequence, signed errno result, and percent (UINT32_MAX if unknown).
 */

#define BKVOICE_COMPANION_CAP_VOLUME (1u << 0)
#define BKVOICE_COMPANION_CAP_PLAYBACK_ACK (1u << 1)
#define BKVOICE_COMPANION_CAP_STATUS_REPORT (1u << 2)
#define BKVOICE_COMPANION_CAP_OTA (1u << 3)

#define BKVOICE_COMPANION_STATUS_REPORT_BYTES 20u
#define BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES 52u
#define BKVOICE_COMPANION_FIRMWARE_ROOT_BYTES 32u
#define BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES 32u
#define BKVOICE_COMPANION_OTA_REPORT_BYTES 44u
#define BKVOICE_COMPANION_BATTERY_UNKNOWN 0xffu
#define BKVOICE_COMPANION_BATTERY_MV_UNKNOWN UINT32_MAX
#define BKVOICE_COMPANION_FW_UNKNOWN UINT16_MAX
#define BKVOICE_COMPANION_FW_BUILD_UNKNOWN UINT32_MAX
#define BKVOICE_COMPANION_BATTERY_STATE_UNKNOWN 0u
#define BKVOICE_COMPANION_BATTERY_STATE_IDLE 1u
#define BKVOICE_COMPANION_BATTERY_STATE_FULL 2u
#define BKVOICE_COMPANION_BATTERY_STATE_DISCHARGING 3u
#define BKVOICE_COMPANION_BATTERY_STATE_CHARGING 4u
#define BKVOICE_COMPANION_BATTERY_STATE_FAULT 5u

enum bkvoice_companion_type_e
{
  BKVOICE_COMPANION_HELLO = 1,
  BKVOICE_COMPANION_WELCOME,
  BKVOICE_COMPANION_TURN_START,
  BKVOICE_COMPANION_AUDIO_UP,
  BKVOICE_COMPANION_TURN_END,
  BKVOICE_COMPANION_TTS_START,
  BKVOICE_COMPANION_AUDIO_DOWN,
  BKVOICE_COMPANION_TTS_END,
  BKVOICE_COMPANION_VISION_START,
  BKVOICE_COMPANION_VISION_CHUNK,
  BKVOICE_COMPANION_VISION_END,
  BKVOICE_COMPANION_CANCEL,
  BKVOICE_COMPANION_ACK,
  BKVOICE_COMPANION_WINDOW_UPDATE,
  BKVOICE_COMPANION_HEARTBEAT,
  BKVOICE_COMPANION_ERROR,
  BKVOICE_COMPANION_VOLUME_GET,
  BKVOICE_COMPANION_VOLUME_SET,
  BKVOICE_COMPANION_VOLUME_REPORT,
  BKVOICE_COMPANION_STATUS_REPORT,
  BKVOICE_COMPANION_OTA_REQUEST,
  BKVOICE_COMPANION_OTA_REPORT,
};

enum bkvoice_companion_ota_phase_e
{
  BKVOICE_COMPANION_OTA_DOWNLOADING = 1,
  BKVOICE_COMPANION_OTA_VERIFYING,
  BKVOICE_COMPANION_OTA_STAGED,
  BKVOICE_COMPANION_OTA_REBOOTING,
  BKVOICE_COMPANION_OTA_TRIAL,
  BKVOICE_COMPANION_OTA_CONFIRMED,
  BKVOICE_COMPANION_OTA_ROLLED_BACK,
  BKVOICE_COMPANION_OTA_FAILED,
};

enum bkvoice_companion_state_e
{
  BKVOICE_COMPANION_DISCONNECTED = 0,
  BKVOICE_COMPANION_CONNECTING,
  BKVOICE_COMPANION_HELLO_SENT,
  BKVOICE_COMPANION_IDLE,
  BKVOICE_COMPANION_UPLINK,
  BKVOICE_COMPANION_THINKING,
  BKVOICE_COMPANION_DOWNLINK,
};

struct bkvoice_companion_header_s
{
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t flags;
  uint16_t header_len;
  uint16_t reserved;
  uint32_t payload_len;
  uint32_t boot_generation;
  uint32_t session_id;
  uint32_t turn_id;
  uint32_t sequence;
  uint64_t timestamp_ms;
};

struct bkvoice_companion_session_s
{
  uint32_t boot_generation;
  uint32_t session_id;
  uint32_t turn_id;
  uint32_t tx_sequence;
  uint32_t rx_sequence;
  uint32_t tx_window;
  uint32_t rx_window;
  uint32_t last_session_id;
  uint32_t capabilities;
  enum bkvoice_companion_state_e state;
};

int bkvoice_companion_encode(
  const struct bkvoice_companion_header_s *header,
  const void *payload, uint8_t *frame, size_t frame_size,
  size_t *encoded_size);
int bkvoice_companion_decode(
  const uint8_t *frame, size_t frame_size,
  struct bkvoice_companion_header_s *header,
  const uint8_t **payload);

int bkvoice_companion_session_init(
  struct bkvoice_companion_session_s *session,
  uint32_t boot_generation);
int bkvoice_companion_session_connect(
  struct bkvoice_companion_session_s *session);
void bkvoice_companion_session_disconnect(
  struct bkvoice_companion_session_s *session);
int bkvoice_companion_session_tx(
  struct bkvoice_companion_session_s *session, uint8_t type,
  uint16_t flags, const uint8_t *payload, uint32_t payload_len,
  uint64_t timestamp_ms,
  struct bkvoice_companion_header_s *header);
int bkvoice_companion_session_rx(
  struct bkvoice_companion_session_s *session,
  const struct bkvoice_companion_header_s *header,
  const uint8_t *payload);

/* A connection-scoped ERROR (turn_id == 0) invalidates the current session.
 * Any -EOVERFLOW result is likewise terminal for that session; the transport
 * must disconnect and establish a fresh monotonically numbered session.
 */

const char *bkvoice_companion_state_name(
  enum bkvoice_companion_state_e state);

#endif /* __APP_BK7258_BK7258_VOICE_COMPANION_H */
