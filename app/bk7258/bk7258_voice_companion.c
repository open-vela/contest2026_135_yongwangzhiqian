/****************************************************************************
 * app/bk7258/bk7258_voice_companion.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Pure companion-v1 framing and session policy.  This module deliberately
 * owns no socket, GPIO, audio device or board resource.
 ****************************************************************************/

#include "bk7258_voice_companion.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

static uint16_t bkvoice_get_be16(const uint8_t *data)
{
  return ((uint16_t)data[0] << 8) | data[1];
}

static uint32_t bkvoice_get_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

static uint64_t bkvoice_get_be64(const uint8_t *data)
{
  return ((uint64_t)bkvoice_get_be32(data) << 32) |
         bkvoice_get_be32(data + 4);
}

static void bkvoice_put_be16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)value;
}

static void bkvoice_put_be32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static void bkvoice_put_be64(uint8_t *data, uint64_t value)
{
  bkvoice_put_be32(data, (uint32_t)(value >> 32));
  bkvoice_put_be32(data + 4, (uint32_t)value);
}

static int bkvoice_volume_payload_valid(uint8_t type, const uint8_t *payload)
{
  if (type == BKVOICE_COMPANION_VOLUME_SET && bkvoice_get_be32(payload) > 100)
    {
      return -ERANGE;
    }

  if (type == BKVOICE_COMPANION_VOLUME_REPORT)
    {
      uint32_t request = bkvoice_get_be32(payload);
      int32_t result = (int32_t)bkvoice_get_be32(payload + 4);
      uint32_t volume = bkvoice_get_be32(payload + 8);
      if (request == 0 || request == UINT32_MAX || result > 0 ||
          (result == 0 ? volume > 100 : volume != UINT32_MAX))
        {
          return -ERANGE;
        }
    }

  return 0;
}

static int bkvoice_status_payload_valid(const uint8_t *payload,
                                        size_t payload_len)
{
  uint8_t battery_percent = payload[1];
  uint8_t charging = payload[2];
  uint8_t battery_state = payload[3];
  uint16_t fw_major = bkvoice_get_be16(payload + 8);
  uint16_t fw_minor = bkvoice_get_be16(payload + 10);
  uint16_t fw_revision = bkvoice_get_be16(payload + 12);
  uint16_t reserved = bkvoice_get_be16(payload + 14);
  uint32_t fw_build = bkvoice_get_be32(payload + 16);
  bool firmware_unknown;
  bool root_all_zero = true;
  bool root_all_erased = true;
  size_t index;

  if ((payload_len != BKVOICE_COMPANION_STATUS_REPORT_BYTES &&
       payload_len != BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES) ||
      (payload_len == BKVOICE_COMPANION_STATUS_REPORT_BYTES &&
       payload[0] != 1) ||
      (payload_len == BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES &&
       payload[0] != 2) ||
      (battery_percent > 100 && battery_percent != BKVOICE_COMPANION_BATTERY_UNKNOWN) ||
      charging > 2 ||
      (battery_state > BKVOICE_COMPANION_BATTERY_STATE_FAULT &&
       battery_state != BKVOICE_COMPANION_BATTERY_UNKNOWN) ||
      reserved != 0)
    {
      return -ERANGE;
    }

  firmware_unknown = fw_major == BKVOICE_COMPANION_FW_UNKNOWN &&
                     fw_minor == BKVOICE_COMPANION_FW_UNKNOWN &&
                     fw_revision == BKVOICE_COMPANION_FW_UNKNOWN &&
                     fw_build == BKVOICE_COMPANION_FW_BUILD_UNKNOWN;

  if (payload_len == BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES)
    {
      for (index = BKVOICE_COMPANION_STATUS_REPORT_BYTES;
           index < BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES; index++)
        {
          root_all_zero = root_all_zero && payload[index] == 0u;
          root_all_erased = root_all_erased && payload[index] == 0xffu;
        }

      if (firmware_unknown || root_all_zero || root_all_erased)
        {
          return -ERANGE;
        }
    }

  if (!firmware_unknown &&
      (fw_major == BKVOICE_COMPANION_FW_UNKNOWN ||
       fw_minor == BKVOICE_COMPANION_FW_UNKNOWN ||
       fw_revision == BKVOICE_COMPANION_FW_UNKNOWN ||
       fw_build == BKVOICE_COMPANION_FW_BUILD_UNKNOWN))
    {
      return -ERANGE;
    }

  if ((battery_state == BKVOICE_COMPANION_BATTERY_STATE_CHARGING && charging != 1) ||
      ((battery_state == BKVOICE_COMPANION_BATTERY_STATE_IDLE ||
        battery_state == BKVOICE_COMPANION_BATTERY_STATE_FULL ||
        battery_state == BKVOICE_COMPANION_BATTERY_STATE_DISCHARGING) && charging != 0) ||
      ((battery_state == BKVOICE_COMPANION_BATTERY_STATE_UNKNOWN ||
        battery_state == BKVOICE_COMPANION_BATTERY_STATE_FAULT ||
        battery_state == BKVOICE_COMPANION_BATTERY_UNKNOWN) && charging != 2))
    {
      return -ERANGE;
    }

  return 0;
}

static bool bkvoice_ota_digest_valid(const uint8_t *digest)
{
  bool all_zero = true;
  bool all_erased = true;
  size_t index;

  for (index = 0; index < BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES;
       index++)
    {
      all_zero = all_zero && digest[index] == 0u;
      all_erased = all_erased && digest[index] == 0xffu;
    }

  return !all_zero && !all_erased;
}

static bool bkvoice_ota_phase_valid(uint8_t phase)
{
  return phase >= BKVOICE_COMPANION_OTA_DOWNLOADING &&
         phase <= BKVOICE_COMPANION_OTA_FAILED;
}

static int bkvoice_ota_payload_valid(uint8_t type, const uint8_t *payload)
{
  int32_t result;
  uint8_t phase;

  if (type == BKVOICE_COMPANION_OTA_REQUEST)
    {
      return bkvoice_ota_digest_valid(payload) ? 0 : -ERANGE;
    }

  if (type != BKVOICE_COMPANION_OTA_REPORT)
    {
      return 0;
    }

  result = (int32_t)bkvoice_get_be32(payload + 4);
  phase = payload[8];
  if (bkvoice_get_be32(payload) == 0 || result > 0 ||
      !bkvoice_ota_phase_valid(phase) || payload[9] > 100u ||
      bkvoice_get_be16(payload + 10) != 0 ||
      !bkvoice_ota_digest_valid(payload + 12) ||
      ((phase == BKVOICE_COMPANION_OTA_FAILED) != (result < 0)))
    {
      return -ERANGE;
    }

  return 0;
}

static bool bkvoice_type_valid(uint8_t type)
{
  return type >= BKVOICE_COMPANION_HELLO &&
         type <= BKVOICE_COMPANION_OTA_REPORT;
}

static bool bkvoice_type_requires_turn(uint8_t type)
{
  return type == BKVOICE_COMPANION_TURN_START ||
         type == BKVOICE_COMPANION_AUDIO_UP ||
         type == BKVOICE_COMPANION_TURN_END ||
         type == BKVOICE_COMPANION_TTS_START ||
         type == BKVOICE_COMPANION_AUDIO_DOWN ||
         type == BKVOICE_COMPANION_TTS_END ||
         type == BKVOICE_COMPANION_VISION_START ||
         type == BKVOICE_COMPANION_VISION_CHUNK ||
         type == BKVOICE_COMPANION_VISION_END ||
         type == BKVOICE_COMPANION_CANCEL;
}

static int bkvoice_header_validate(
  const struct bkvoice_companion_header_s *header)
{
  bool end_of_stream;
  bool synthetic_type;
  bool synthetic;

  if (header == NULL)
    {
      return -EINVAL;
    }

  if (header->magic != BKVOICE_COMPANION_MAGIC ||
      header->version != BKVOICE_COMPANION_VERSION ||
      header->header_len != BKVOICE_COMPANION_HEADER_BYTES ||
      header->reserved != 0 || !bkvoice_type_valid(header->type))
    {
      return -EPROTO;
    }

  if ((header->flags & ~BKVOICE_COMPANION_FLAG_MASK) != 0 ||
      header->payload_len > BKVOICE_COMPANION_MAX_PAYLOAD ||
      header->boot_generation == 0 || header->session_id == 0 ||
      header->sequence == 0)
    {
      return -EPROTO;
    }

  if (header->type == BKVOICE_COMPANION_VISION_START ||
      header->type == BKVOICE_COMPANION_VISION_CHUNK ||
      header->type == BKVOICE_COMPANION_VISION_END)
    {
      return -ENOTSUP;
    }

  if ((bkvoice_type_requires_turn(header->type) &&
       header->turn_id == 0) ||
      ((header->type == BKVOICE_COMPANION_HELLO ||
        header->type == BKVOICE_COMPANION_WELCOME) &&
       header->turn_id != 0))
    {
      return -EPROTO;
    }

  if ((header->type == BKVOICE_COMPANION_AUDIO_UP ||
       header->type == BKVOICE_COMPANION_AUDIO_DOWN) &&
      header->payload_len != BKVOICE_COMPANION_AUDIO_FRAME_BYTES)
    {
      return -EMSGSIZE;
    }

  if (header->type == BKVOICE_COMPANION_WINDOW_UPDATE &&
      header->payload_len != sizeof(uint32_t))
    {
      return -EMSGSIZE;
    }

  if ((header->type == BKVOICE_COMPANION_HELLO &&
       header->payload_len != 0 && header->payload_len != 4) ||
      (header->type == BKVOICE_COMPANION_VOLUME_GET && header->payload_len != 0) ||
      (header->type == BKVOICE_COMPANION_VOLUME_SET && header->payload_len != 4) ||
      (header->type == BKVOICE_COMPANION_VOLUME_REPORT && header->payload_len != 12))
    {
      return -EMSGSIZE;
    }

  if (header->type >= BKVOICE_COMPANION_VOLUME_GET && header->turn_id != 0)
    {
      return -EPROTO;
    }

  if (header->type == BKVOICE_COMPANION_STATUS_REPORT &&
      (header->flags != 0 ||
       (header->payload_len != BKVOICE_COMPANION_STATUS_REPORT_BYTES &&
        header->payload_len != BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES)))
    {
      return header->flags != 0 ? -EPROTO : -EMSGSIZE;
    }

  if ((header->type == BKVOICE_COMPANION_OTA_REQUEST &&
       (header->flags != 0 ||
        header->payload_len != BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES)) ||
      (header->type == BKVOICE_COMPANION_OTA_REPORT &&
       (header->flags != 0 ||
        header->payload_len != BKVOICE_COMPANION_OTA_REPORT_BYTES)))
    {
      return header->flags != 0 ? -EPROTO : -EMSGSIZE;
    }

  if ((header->type == BKVOICE_COMPANION_TURN_START ||
       header->type == BKVOICE_COMPANION_TURN_END ||
       header->type == BKVOICE_COMPANION_TTS_START ||
       header->type == BKVOICE_COMPANION_TTS_END ||
       header->type == BKVOICE_COMPANION_CANCEL ||
       header->type == BKVOICE_COMPANION_HEARTBEAT) &&
      header->payload_len != 0)
    {
      return -EMSGSIZE;
    }

  synthetic_type = header->type == BKVOICE_COMPANION_TTS_START ||
                   header->type == BKVOICE_COMPANION_AUDIO_DOWN ||
                   header->type == BKVOICE_COMPANION_TTS_END;
  synthetic = (header->flags & BKVOICE_COMPANION_FLAG_SYNTHETIC) != 0;
  if (synthetic_type && !synthetic)
    {
      return -EACCES;
    }

  if (!synthetic_type && synthetic)
    {
      return -EPROTO;
    }

  end_of_stream =
    (header->flags & BKVOICE_COMPANION_FLAG_END_OF_STREAM) != 0;
  if (end_of_stream && header->type != BKVOICE_COMPANION_AUDIO_UP &&
      header->type != BKVOICE_COMPANION_AUDIO_DOWN)
    {
      return -EPROTO;
    }

  return 0;
}

int bkvoice_companion_encode(
  const struct bkvoice_companion_header_s *header,
  const void *payload, uint8_t *frame, size_t frame_size,
  size_t *encoded_size)
{
  size_t needed;
  int ret;

  if (frame == NULL || encoded_size == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_header_validate(header);
  if (ret < 0)
    {
      return ret;
    }

  needed = BKVOICE_COMPANION_HEADER_BYTES + header->payload_len;
  if (frame_size < needed || (header->payload_len != 0 && payload == NULL))
    {
      return -EMSGSIZE;
    }

  ret = bkvoice_volume_payload_valid(header->type, payload);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_ota_payload_valid(header->type, payload);
  if (ret < 0)
    {
      return ret;
    }

  if (header->type == BKVOICE_COMPANION_STATUS_REPORT)
    {
      ret = bkvoice_status_payload_valid(payload, header->payload_len);
      if (ret < 0)
        {
          return ret;
        }
    }

  bkvoice_put_be32(frame, header->magic);
  frame[4] = header->version;
  frame[5] = header->type;
  bkvoice_put_be16(frame + 6, header->flags);
  bkvoice_put_be16(frame + 8, header->header_len);
  bkvoice_put_be16(frame + 10, header->reserved);
  bkvoice_put_be32(frame + 12, header->payload_len);
  bkvoice_put_be32(frame + 16, header->boot_generation);
  bkvoice_put_be32(frame + 20, header->session_id);
  bkvoice_put_be32(frame + 24, header->turn_id);
  bkvoice_put_be32(frame + 28, header->sequence);
  bkvoice_put_be64(frame + 32, header->timestamp_ms);
  if (header->payload_len != 0)
    {
      memcpy(frame + BKVOICE_COMPANION_HEADER_BYTES, payload,
             header->payload_len);
    }

  *encoded_size = needed;
  return 0;
}

int bkvoice_companion_decode(
  const uint8_t *frame, size_t frame_size,
  struct bkvoice_companion_header_s *header,
  const uint8_t **payload)
{
  size_t needed;
  int ret;

  if (frame == NULL || header == NULL || payload == NULL)
    {
      return -EINVAL;
    }

  if (frame_size < BKVOICE_COMPANION_HEADER_BYTES)
    {
      return -EMSGSIZE;
    }

  memset(header, 0, sizeof(*header));
  header->magic = bkvoice_get_be32(frame);
  header->version = frame[4];
  header->type = frame[5];
  header->flags = bkvoice_get_be16(frame + 6);
  header->header_len = bkvoice_get_be16(frame + 8);
  header->reserved = bkvoice_get_be16(frame + 10);
  header->payload_len = bkvoice_get_be32(frame + 12);
  header->boot_generation = bkvoice_get_be32(frame + 16);
  header->session_id = bkvoice_get_be32(frame + 20);
  header->turn_id = bkvoice_get_be32(frame + 24);
  header->sequence = bkvoice_get_be32(frame + 28);
  header->timestamp_ms = bkvoice_get_be64(frame + 32);

  ret = bkvoice_header_validate(header);
  if (ret < 0)
    {
      return ret;
    }

  needed = BKVOICE_COMPANION_HEADER_BYTES + header->payload_len;
  if (frame_size != needed)
    {
      return -EMSGSIZE;
    }

  *payload = frame + BKVOICE_COMPANION_HEADER_BYTES;
  ret = bkvoice_volume_payload_valid(header->type, *payload);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_ota_payload_valid(header->type, *payload);
  if (ret < 0)
    {
      return ret;
    }

  return header->type == BKVOICE_COMPANION_STATUS_REPORT ?
         bkvoice_status_payload_valid(*payload, header->payload_len) : 0;
}

int bkvoice_companion_session_init(
  struct bkvoice_companion_session_s *session,
  uint32_t boot_generation)
{
  if (session == NULL || boot_generation == 0)
    {
      return -EINVAL;
    }

  memset(session, 0, sizeof(*session));
  session->boot_generation = boot_generation;
  session->state = BKVOICE_COMPANION_DISCONNECTED;
  return 0;
}

int bkvoice_companion_session_connect(
  struct bkvoice_companion_session_s *session)
{
  if (session == NULL)
    {
      return -EINVAL;
    }

  if (session->state != BKVOICE_COMPANION_DISCONNECTED)
    {
      return -EALREADY;
    }

  if (session->last_session_id == UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  session->session_id = ++session->last_session_id;
  session->turn_id = 0;
  session->tx_sequence = 0;
  session->rx_sequence = 0;
  session->tx_window = 0;
  session->rx_window = 0;
  session->capabilities = 0;
  session->state = BKVOICE_COMPANION_CONNECTING;
  return 0;
}

void bkvoice_companion_session_disconnect(
  struct bkvoice_companion_session_s *session)
{
  if (session == NULL)
    {
      return;
    }

  session->session_id = 0;
  session->turn_id = 0;
  session->tx_sequence = 0;
  session->rx_sequence = 0;
  session->tx_window = 0;
  session->rx_window = 0;
  session->state = BKVOICE_COMPANION_DISCONNECTED;
}

static bool bkvoice_session_ready(
  const struct bkvoice_companion_session_s *session)
{
  return session->state == BKVOICE_COMPANION_IDLE ||
         session->state == BKVOICE_COMPANION_UPLINK ||
         session->state == BKVOICE_COMPANION_THINKING ||
         session->state == BKVOICE_COMPANION_DOWNLINK;
}

static int bkvoice_tx_transition(struct bkvoice_companion_session_s *next,
                                 uint8_t type, const uint8_t *payload,
                                 uint32_t payload_len,
                                 uint32_t *turn_id)
{
  uint32_t credit;
  bool active = next->state == BKVOICE_COMPANION_UPLINK ||
                next->state == BKVOICE_COMPANION_THINKING ||
                next->state == BKVOICE_COMPANION_DOWNLINK;

  *turn_id = active ? next->turn_id : 0;
  switch (type)
    {
      case BKVOICE_COMPANION_HELLO:
        if (next->state != BKVOICE_COMPANION_CONNECTING ||
            next->tx_sequence != 0)
          {
            return -EPERM;
          }

        if (payload_len != 0 && (payload_len != 4 || payload == NULL))
          {
            return -EINVAL;
          }

        next->capabilities = payload_len ? bkvoice_get_be32(payload) : 0;
        next->state = BKVOICE_COMPANION_HELLO_SENT;
        break;

      case BKVOICE_COMPANION_TURN_START:
        if (next->state != BKVOICE_COMPANION_IDLE)
          {
            return -EBUSY;
          }

        if (next->turn_id == UINT32_MAX)
          {
            return -EOVERFLOW;
          }

        next->turn_id++;
        *turn_id = next->turn_id;
        next->state = BKVOICE_COMPANION_UPLINK;
        break;

      case BKVOICE_COMPANION_AUDIO_UP:
        if (next->state != BKVOICE_COMPANION_UPLINK)
          {
            return -EPERM;
          }

        if (payload_len > next->tx_window)
          {
            return -EAGAIN;
          }

        next->tx_window -= payload_len;
        break;

      case BKVOICE_COMPANION_TURN_END:
        if (next->state != BKVOICE_COMPANION_UPLINK)
          {
            return -EPERM;
          }

        next->state = BKVOICE_COMPANION_THINKING;
        break;

      case BKVOICE_COMPANION_CANCEL:
        if (!active)
          {
            return -EALREADY;
          }

        next->state = BKVOICE_COMPANION_IDLE;
        break;

      case BKVOICE_COMPANION_ACK:
        if (!bkvoice_session_ready(next))
          {
            return -EPERM;
          }

        /* Completion ACK retains its turn after cleanup enters IDLE. */

        *turn_id = next->turn_id;
        break;

      case BKVOICE_COMPANION_VOLUME_REPORT:
        if (!bkvoice_session_ready(next) ||
            !(next->capabilities & BKVOICE_COMPANION_CAP_VOLUME))
          {
            return -ENOTSUP;
          }

        *turn_id = 0;
        break;

      case BKVOICE_COMPANION_STATUS_REPORT:
        if (!bkvoice_session_ready(next) ||
            !(next->capabilities & BKVOICE_COMPANION_CAP_STATUS_REPORT))
          {
            return -ENOTSUP;
          }

        *turn_id = 0;
        break;

      case BKVOICE_COMPANION_OTA_REPORT:
        if (!bkvoice_session_ready(next) ||
            !(next->capabilities & BKVOICE_COMPANION_CAP_OTA))
          {
            return -ENOTSUP;
          }

        *turn_id = 0;
        break;

      case BKVOICE_COMPANION_HEARTBEAT:
      case BKVOICE_COMPANION_ERROR:
        if (!bkvoice_session_ready(next))
          {
            return -EPERM;
          }
        break;

      case BKVOICE_COMPANION_WINDOW_UPDATE:
        if (!bkvoice_session_ready(next) || payload == NULL ||
            payload_len != sizeof(uint32_t))
          {
            return -EPERM;
          }

        credit = bkvoice_get_be32(payload);
        if (credit == 0 || credit > BKVOICE_COMPANION_MAX_WINDOW ||
            next->rx_window > BKVOICE_COMPANION_MAX_WINDOW - credit)
          {
            return -EOVERFLOW;
          }

        next->rx_window += credit;
        break;

      default:
        return -EPERM;
    }

  return 0;
}

int bkvoice_companion_session_tx(
  struct bkvoice_companion_session_s *session, uint8_t type,
  uint16_t flags, const uint8_t *payload, uint32_t payload_len,
  uint64_t timestamp_ms,
  struct bkvoice_companion_header_s *header)
{
  struct bkvoice_companion_session_s next;
  struct bkvoice_companion_header_s candidate;
  uint32_t turn_id;
  int ret;

  if (session == NULL || header == NULL)
    {
      return -EINVAL;
    }

  if (payload_len != 0 && payload == NULL)
    {
      return -EMSGSIZE;
    }

  if (session->state == BKVOICE_COMPANION_DISCONNECTED)
    {
      return -ENOTCONN;
    }

  if (session->tx_sequence >= UINT32_MAX - 1u)
    {
      return -EOVERFLOW;
    }

  next = *session;
  ret = bkvoice_tx_transition(&next, type, payload, payload_len, &turn_id);
  if (ret < 0)
    {
      return ret;
    }

  memset(&candidate, 0, sizeof(candidate));
  candidate.magic = BKVOICE_COMPANION_MAGIC;
  candidate.version = BKVOICE_COMPANION_VERSION;
  candidate.type = type;
  candidate.flags = flags;
  candidate.header_len = BKVOICE_COMPANION_HEADER_BYTES;
  candidate.payload_len = payload_len;
  candidate.boot_generation = session->boot_generation;
  candidate.session_id = session->session_id;
  candidate.turn_id = turn_id;
  candidate.sequence = session->tx_sequence + 1u;
  candidate.timestamp_ms = timestamp_ms;

  ret = bkvoice_header_validate(&candidate);
  if (ret < 0)
    {
      return ret;
    }

  if (candidate.payload_len != 0 && payload == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_volume_payload_valid(type, payload);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_ota_payload_valid(type, payload);
  if (ret < 0)
    {
      return ret;
    }

  if (type == BKVOICE_COMPANION_STATUS_REPORT)
    {
      ret = bkvoice_status_payload_valid(payload, payload_len);
      if (ret < 0)
        {
          return ret;
        }
    }

  next.tx_sequence = candidate.sequence;
  *session = next;
  *header = candidate;
  return 0;
}

static int bkvoice_rx_turn_match(
  const struct bkvoice_companion_session_s *session,
  const struct bkvoice_companion_header_s *header)
{
  if (header->turn_id == 0 || header->turn_id == session->turn_id)
    {
      return 0;
    }

  return -ESTALE;
}

static int bkvoice_rx_transition(
  struct bkvoice_companion_session_s *next,
  const struct bkvoice_companion_header_s *header,
  const uint8_t *payload)
{
  uint32_t credit;
  int ret;

  switch (header->type)
    {
      case BKVOICE_COMPANION_WELCOME:
        if (next->state != BKVOICE_COMPANION_HELLO_SENT ||
            header->turn_id != 0)
          {
            return -EPERM;
          }

        next->state = BKVOICE_COMPANION_IDLE;
        break;

      case BKVOICE_COMPANION_WINDOW_UPDATE:
        if (!bkvoice_session_ready(next) || payload == NULL)
          {
            return -EPERM;
          }

        ret = bkvoice_rx_turn_match(next, header);
        if (ret < 0)
          {
            return ret;
          }

        credit = bkvoice_get_be32(payload);
        if (credit == 0 || credit > BKVOICE_COMPANION_MAX_WINDOW ||
            next->tx_window > BKVOICE_COMPANION_MAX_WINDOW - credit)
          {
            return -EOVERFLOW;
          }

        next->tx_window += credit;
        break;

      case BKVOICE_COMPANION_TTS_START:
        if (next->state != BKVOICE_COMPANION_THINKING ||
            header->turn_id != next->turn_id)
          {
            return -ESTALE;
          }

        next->state = BKVOICE_COMPANION_DOWNLINK;
        break;

      case BKVOICE_COMPANION_AUDIO_DOWN:
        if (next->state != BKVOICE_COMPANION_DOWNLINK ||
            header->turn_id != next->turn_id)
          {
            return -ESTALE;
          }

        if (next->rx_window < header->payload_len)
          {
            return -ENOBUFS;
          }

        next->rx_window -= header->payload_len;
        break;

      case BKVOICE_COMPANION_TTS_END:
        if (next->state != BKVOICE_COMPANION_DOWNLINK ||
            header->turn_id != next->turn_id)
          {
            return -ESTALE;
          }

        next->state = BKVOICE_COMPANION_IDLE;
        break;

      case BKVOICE_COMPANION_CANCEL:
        /* Local completion/cancellation can cross a remote CANCEL.  Admit
         * the same nonzero turn while idle; the turn owner makes cleanup
         * idempotent.  Never admit an old turn after the next one starts.
         */

        if (!bkvoice_session_ready(next) || header->turn_id == 0 ||
            header->turn_id != next->turn_id)
          {
            return -ESTALE;
          }

        next->state = BKVOICE_COMPANION_IDLE;
        break;

      case BKVOICE_COMPANION_ACK:
      case BKVOICE_COMPANION_HEARTBEAT:
        return bkvoice_rx_turn_match(next, header);

      case BKVOICE_COMPANION_VOLUME_GET:
      case BKVOICE_COMPANION_VOLUME_SET:
        if (!bkvoice_session_ready(next) ||
            !(next->capabilities & BKVOICE_COMPANION_CAP_VOLUME))
          {
            return -ENOTSUP;
          }

        if (header->type == BKVOICE_COMPANION_VOLUME_SET &&
            (payload == NULL || bkvoice_get_be32(payload) > 100))
          {
            return -ERANGE;
          }

        return 0;

      case BKVOICE_COMPANION_STATUS_REPORT:
        return -EPERM;

      case BKVOICE_COMPANION_OTA_REQUEST:
        if (!bkvoice_session_ready(next) ||
            !(next->capabilities & BKVOICE_COMPANION_CAP_OTA))
          {
            return -ENOTSUP;
          }

        return 0;

      case BKVOICE_COMPANION_OTA_REPORT:
        return -EPERM;

      case BKVOICE_COMPANION_ERROR:
        if (next->state == BKVOICE_COMPANION_CONNECTING ||
            next->state == BKVOICE_COMPANION_HELLO_SENT)
          {
            return -ECONNREFUSED;
          }

        ret = bkvoice_rx_turn_match(next, header);
        if (ret < 0)
          {
            return ret;
          }

        if (header->turn_id == 0)
          {
            next->session_id = 0;
            next->turn_id = 0;
            next->tx_sequence = 0;
            next->rx_sequence = 0;
            next->tx_window = 0;
            next->rx_window = 0;
            next->state = BKVOICE_COMPANION_DISCONNECTED;
          }
        else
          {
            next->state = BKVOICE_COMPANION_IDLE;
          }
        break;

      case BKVOICE_COMPANION_VISION_START:
      case BKVOICE_COMPANION_VISION_CHUNK:
      case BKVOICE_COMPANION_VISION_END:
        return -ENOTSUP;

      default:
        return -EPERM;
    }

  return 0;
}

int bkvoice_companion_session_rx(
  struct bkvoice_companion_session_s *session,
  const struct bkvoice_companion_header_s *header,
  const uint8_t *payload)
{
  struct bkvoice_companion_session_s next;
  int ret;

  if (session == NULL)
    {
      return -EINVAL;
    }

  if (session->state == BKVOICE_COMPANION_DISCONNECTED)
    {
      return -ENOTCONN;
    }

  ret = bkvoice_header_validate(header);
  if (ret < 0)
    {
      return ret;
    }

  if (header->payload_len != 0 && payload == NULL)
    {
      return -EMSGSIZE;
    }

  ret = bkvoice_ota_payload_valid(header->type, payload);
  if (ret < 0)
    {
      return ret;
    }

  if (header->boot_generation != session->boot_generation ||
      header->session_id != session->session_id)
    {
      return -ESTALE;
    }

  if (header->sequence <= session->rx_sequence)
    {
      return -EALREADY;
    }

  if (header->sequence == UINT32_MAX)
    {
      return -EOVERFLOW;
    }

  if (header->sequence != session->rx_sequence + 1u)
    {
      return -EPROTO;
    }

  next = *session;
  ret = bkvoice_rx_transition(&next, header, payload);
  if (ret < 0)
    {
      return ret;
    }

  if (next.state != BKVOICE_COMPANION_DISCONNECTED)
    {
      next.rx_sequence = header->sequence;
    }

  *session = next;
  return 0;
}

const char *bkvoice_companion_state_name(
  enum bkvoice_companion_state_e state)
{
  switch (state)
    {
      case BKVOICE_COMPANION_DISCONNECTED:
        return "disconnected";
      case BKVOICE_COMPANION_CONNECTING:
        return "connecting";
      case BKVOICE_COMPANION_HELLO_SENT:
        return "hello-sent";
      case BKVOICE_COMPANION_IDLE:
        return "idle";
      case BKVOICE_COMPANION_UPLINK:
        return "uplink";
      case BKVOICE_COMPANION_THINKING:
        return "thinking";
      case BKVOICE_COMPANION_DOWNLINK:
        return "downlink";
      default:
        return "invalid";
    }
}
