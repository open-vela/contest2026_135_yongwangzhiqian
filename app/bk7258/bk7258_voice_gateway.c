/****************************************************************************
 * app/bk7258/bk7258_voice_gateway.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_voice_gateway.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

static bool bkvoice_gateway_downlink_ops_valid(
  const struct bkvoice_gateway_downlink_ops_s *ops)
{
  return ops != NULL && ops->tts_start != NULL && ops->tts_audio != NULL &&
         ops->tts_end != NULL && ops->terminal != NULL;
}

static uint32_t bkvoice_gateway_get_be32(const uint8_t *data)
{
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | data[3];
}

static void bkvoice_gateway_put_be32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static void bkvoice_gateway_put_be16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)value;
}

static bool bkvoice_gateway_ota_phase_terminal(
  enum bkvoice_companion_ota_phase_e phase)
{
  return phase == BKVOICE_COMPANION_OTA_CONFIRMED ||
         phase == BKVOICE_COMPANION_OTA_ROLLED_BACK ||
         phase == BKVOICE_COMPANION_OTA_FAILED;
}

static bool bkvoice_gateway_ota_phase_valid(
  enum bkvoice_companion_ota_phase_e phase)
{
  return phase >= BKVOICE_COMPANION_OTA_DOWNLOADING &&
         phase <= BKVOICE_COMPANION_OTA_FAILED;
}

static bool bkvoice_gateway_ota_transition_valid(
  enum bkvoice_companion_ota_phase_e current, uint8_t current_progress,
  enum bkvoice_companion_ota_phase_e next, uint8_t next_progress)
{
  if (next == BKVOICE_COMPANION_OTA_FAILED)
    {
      return true;
    }

  if (next == current)
    {
      return next_progress >= current_progress;
    }

  switch (current)
    {
      case BKVOICE_COMPANION_OTA_DOWNLOADING:
        return next == BKVOICE_COMPANION_OTA_VERIFYING;
      case BKVOICE_COMPANION_OTA_VERIFYING:
        return next == BKVOICE_COMPANION_OTA_STAGED;
      case BKVOICE_COMPANION_OTA_STAGED:
        return next == BKVOICE_COMPANION_OTA_REBOOTING;
      case BKVOICE_COMPANION_OTA_REBOOTING:
        return next == BKVOICE_COMPANION_OTA_TRIAL;
      case BKVOICE_COMPANION_OTA_TRIAL:
        return next == BKVOICE_COMPANION_OTA_CONFIRMED ||
               next == BKVOICE_COMPANION_OTA_ROLLED_BACK;
      default:
        return false;
    }
}

static uint64_t bkvoice_gateway_deadline(struct bkvoice_gateway_s *gateway)
{
  uint64_t now = gateway->now_ms(gateway->clock_context);

  if (UINT64_MAX - now < gateway->config.io_timeout_ms)
    {
      return UINT64_MAX;
    }

  return now + gateway->config.io_timeout_ms;
}

static bool bkvoice_gateway_token_matches(
  const struct bkvoice_gateway_s *gateway,
  const struct bkvoice_turn_token_s *token, bool next_turn)
{
  uint32_t expected_turn;

  if (token == NULL || token->boot_generation !=
      gateway->companion.boot_generation ||
      token->session_id != gateway->companion.session_id)
    {
      return false;
    }

  expected_turn = gateway->companion.turn_id;
  if (next_turn)
    {
      if (expected_turn == UINT32_MAX)
        {
          return false;
        }

      expected_turn++;
    }

  return token->turn_id == expected_turn;
}

static int bkvoice_gateway_lock(struct bkvoice_gateway_s *gateway)
{
  int ret = pthread_mutex_lock(&gateway->lock);
  return ret == 0 ? 0 : -ret;
}

static int bkvoice_gateway_unlock(struct bkvoice_gateway_s *gateway)
{
  int ret = pthread_mutex_unlock(&gateway->lock);
  return ret == 0 ? 0 : -ret;
}

static int bkvoice_gateway_terminal_locked(
  struct bkvoice_gateway_s *gateway, int reason)
{
  if (gateway->companion.session_id == 0 ||
      gateway->companion.turn_id == 0 ||
      (gateway->companion.state != BKVOICE_COMPANION_UPLINK &&
       gateway->companion.state != BKVOICE_COMPANION_THINKING &&
       gateway->companion.state != BKVOICE_COMPANION_DOWNLINK))
    {
      return 0;
    }

  return gateway->downlink_ops.terminal(
    gateway->downlink_context, gateway->companion.session_id,
    gateway->companion.turn_id, reason);
}

static int bkvoice_gateway_fault_locked(struct bkvoice_gateway_s *gateway,
                                        int error, bool notify_terminal)
{
  if (error >= 0)
    {
      error = -EIO;
    }

  if (!gateway->faulted && notify_terminal)
    {
      (void)bkvoice_gateway_terminal_locked(gateway, error);
    }

  bkvoice_companion_session_disconnect(&gateway->companion);
  gateway->faulted = true;
  gateway->last_error = error;

  /* This is safe concurrently with send/recv and only wakes I/O.  The
   * serialized owner closes the provider after those callers join.
   */

  (void)bkvoice_transport_interrupt(&gateway->transport);
  return error;
}

static int bkvoice_gateway_send_locked(struct bkvoice_gateway_s *gateway,
                                       uint8_t type, uint16_t flags,
                                       const uint8_t *payload,
                                       uint32_t payload_len,
                                       uint64_t deadline_ms)
{
  struct bkvoice_companion_session_s candidate;
  struct bkvoice_companion_header_s header;
  size_t encoded_size;
  int ret;

  if (payload_len > BKVOICE_GATEWAY_MAX_PAYLOAD)
    {
      return -EMSGSIZE;
    }

  candidate = gateway->companion;
  ret = bkvoice_companion_session_tx(&candidate, type, flags, payload,
                                     payload_len,
                                     gateway->now_ms(gateway->clock_context),
                                     &header);
  if (ret < 0)
    {
      if (ret == -EOVERFLOW)
        {
          return bkvoice_gateway_fault_locked(gateway, ret, false);
        }

      return ret;
    }

  ret = bkvoice_companion_encode(&header, payload, gateway->tx_frame,
                                 sizeof(gateway->tx_frame), &encoded_size);
  if (ret < 0)
    {
      return bkvoice_gateway_fault_locked(gateway, ret, false);
    }

  ret = bkvoice_transport_send_all(&gateway->transport, gateway->tx_frame,
                                   encoded_size, deadline_ms);
  if (ret < 0)
    {
      return bkvoice_gateway_fault_locked(gateway, ret, false);
    }

  gateway->companion = candidate;
  gateway->tx_frames++;
  gateway->last_error = 0;
  return 0;
}

static int bkvoice_gateway_send_ota_report_locked(
  struct bkvoice_gateway_s *gateway, uint32_t request_sequence,
  const uint8_t manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES],
  int result, enum bkvoice_companion_ota_phase_e phase, uint8_t progress)
{
  uint8_t payload[BKVOICE_COMPANION_OTA_REPORT_BYTES];

  if (request_sequence == 0 || manifest_sha256 == NULL ||
      !bkvoice_gateway_ota_phase_valid(phase) || progress > 100u ||
      result > 0 ||
      ((phase == BKVOICE_COMPANION_OTA_FAILED) != (result < 0)))
    {
      return -EINVAL;
    }

  memset(payload, 0, sizeof(payload));
  bkvoice_gateway_put_be32(payload, request_sequence);
  bkvoice_gateway_put_be32(payload + 4, (uint32_t)result);
  payload[8] = (uint8_t)phase;
  payload[9] = progress;
  memcpy(payload + 12, manifest_sha256,
         BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES);
  return bkvoice_gateway_send_locked(gateway, BKVOICE_COMPANION_OTA_REPORT,
                                    0, payload, sizeof(payload),
                                    bkvoice_gateway_deadline(gateway));
}

static int bkvoice_gateway_report_ota_locked(
  struct bkvoice_gateway_s *gateway, uint32_t request_sequence,
  const uint8_t manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES],
  int result, enum bkvoice_companion_ota_phase_e phase, uint8_t progress)
{
  bool terminal;
  int ret;

  if (!gateway->ota_active || request_sequence == 0 ||
      request_sequence != gateway->ota_request_sequence ||
      manifest_sha256 == NULL ||
      memcmp(manifest_sha256, gateway->ota_manifest_sha256,
             sizeof(gateway->ota_manifest_sha256)) != 0 ||
      !bkvoice_gateway_ota_transition_valid(gateway->ota_phase,
                                             gateway->ota_progress,
                                             phase, progress))
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_send_ota_report_locked(gateway, request_sequence,
                                                manifest_sha256, result,
                                                phase, progress);
  if (ret < 0)
    {
      return ret;
    }

  terminal = bkvoice_gateway_ota_phase_terminal(phase);
  if (terminal)
    {
      gateway->ota_active = false;
      gateway->ota_request_sequence = 0;
      gateway->ota_phase = 0;
      gateway->ota_progress = 0;
      memset(gateway->ota_manifest_sha256, 0,
             sizeof(gateway->ota_manifest_sha256));
    }
  else
    {
      gateway->ota_phase = (uint8_t)phase;
      gateway->ota_progress = progress;
    }

  return 0;
}

static int bkvoice_gateway_grant_downlink_locked(
  struct bkvoice_gateway_s *gateway, uint32_t bytes)
{
  uint8_t payload[sizeof(uint32_t)];

  bkvoice_gateway_put_be32(payload, bytes);
  return bkvoice_gateway_send_locked(
    gateway, BKVOICE_COMPANION_WINDOW_UPDATE, 0, payload,
    sizeof(payload), bkvoice_gateway_deadline(gateway));
}

static int bkvoice_gateway_dispatch_locked(
  struct bkvoice_gateway_s *gateway,
  const struct bkvoice_companion_header_s *header,
  const uint8_t *payload)
{
  struct bkvoice_companion_session_s candidate;
  struct bkvoice_turn_token_s token;
  uint8_t volume_report[12];
  unsigned int observed = UINT32_MAX;
  uint64_t now_ms;
  int ret;

  candidate = gateway->companion;
  ret = bkvoice_companion_session_rx(&candidate, header, payload);
  if (ret < 0)
    {
      return bkvoice_gateway_fault_locked(gateway, ret, true);
    }

  memset(&token, 0, sizeof(token));
  switch (header->type)
    {
      case BKVOICE_COMPANION_OTA_REQUEST:
        if (gateway->ota_active || candidate.state != BKVOICE_COMPANION_IDLE)
          {
            gateway->companion = candidate;
            gateway->rx_frames++;
            ret = bkvoice_gateway_send_ota_report_locked(
              gateway, header->sequence, payload, -EBUSY,
              BKVOICE_COMPANION_OTA_FAILED, 0);
            return ret < 0 ? ret : -EBUSY;
          }

        if (gateway->config.ota_request == NULL)
          {
            return -ENOTSUP;
          }

        /* The adapter only admits work.  It runs under the gateway lock so
         * it must return promptly and must not re-enter any gateway API.
         * The first report is emitted here, before later owner updates. */

        ret = gateway->config.ota_request(
          gateway->config.ota_context, header->sequence, payload);
        if (ret > 0)
          {
            ret = -EIO;
          }

        gateway->companion = candidate;
        gateway->rx_frames++;
        gateway->ota_active = true;
        gateway->ota_request_sequence = header->sequence;
        gateway->ota_phase = BKVOICE_COMPANION_OTA_DOWNLOADING;
        gateway->ota_progress = 0;
        memcpy(gateway->ota_manifest_sha256, payload,
               sizeof(gateway->ota_manifest_sha256));
        return bkvoice_gateway_report_ota_locked(
          gateway, header->sequence, payload, ret,
          ret < 0 ? BKVOICE_COMPANION_OTA_FAILED :
                    BKVOICE_COMPANION_OTA_DOWNLOADING,
          0);

      case BKVOICE_COMPANION_VOLUME_GET:
      case BKVOICE_COMPANION_VOLUME_SET:
        ret = gateway->downlink_ops.volume == NULL ? -ENOTSUP :
          gateway->downlink_ops.volume(gateway->downlink_context,
            header->type == BKVOICE_COMPANION_VOLUME_SET,
            header->payload_len ? bkvoice_gateway_get_be32(payload) : 0,
            &observed);
        if (ret > 0 || (ret == 0 && observed > 100u))
          {
            ret = -EIO;
          }

        bkvoice_gateway_put_be32(volume_report, header->sequence);
        bkvoice_gateway_put_be32(volume_report + 4, (uint32_t)ret);
        bkvoice_gateway_put_be32(volume_report + 8,
                                  ret < 0 ? UINT32_MAX : observed);
        gateway->companion = candidate;
        gateway->rx_frames++;
        return bkvoice_gateway_send_locked(gateway,
          BKVOICE_COMPANION_VOLUME_REPORT, 0, volume_report,
          sizeof(volume_report), bkvoice_gateway_deadline(gateway));

      case BKVOICE_COMPANION_TTS_START:
      case BKVOICE_COMPANION_AUDIO_DOWN:
      case BKVOICE_COMPANION_TTS_END:
        if (gateway->downlink_sequence == UINT32_MAX)
          {
            return bkvoice_gateway_fault_locked(gateway, -EOVERFLOW, true);
          }

        token.boot_generation = header->boot_generation;
        token.session_id = header->session_id;
        token.turn_id = header->turn_id;
        token.sequence = gateway->downlink_sequence + 1u;
        now_ms = gateway->now_ms(gateway->clock_context);

        if (header->type == BKVOICE_COMPANION_TTS_START)
          {
            ret = gateway->downlink_ops.tts_start(
              gateway->downlink_context, &token, now_ms);
          }
        else if (header->type == BKVOICE_COMPANION_AUDIO_DOWN)
          {
            ret = gateway->downlink_ops.tts_audio(
              gateway->downlink_context, &token, payload,
              header->payload_len, now_ms);
          }
        else
          {
            ret = gateway->downlink_ops.tts_end(
              gateway->downlink_context, &token);
          }

        if (ret < 0)
          {
            return bkvoice_gateway_fault_locked(gateway, ret, true);
          }

        gateway->downlink_sequence = token.sequence;
        if (header->type == BKVOICE_COMPANION_AUDIO_DOWN)
          {
            /* rx() consumed this frame's advertised window and tts_audio()
             * has now handed the complete frame to the local playback queue.
             * Commit that receive before returning the same credit.  Calling
             * the public helper here would re-enter the gateway mutex.
             */

            gateway->companion = candidate;
            gateway->rx_frames++;
            return bkvoice_gateway_grant_downlink_locked(
              gateway, header->payload_len);
          }

        break;

      case BKVOICE_COMPANION_CANCEL:
        ret = gateway->downlink_ops.terminal(
          gateway->downlink_context, header->session_id, header->turn_id,
          -ECANCELED);
        if (ret < 0)
          {
            return bkvoice_gateway_fault_locked(gateway, ret, false);
          }
        break;

      case BKVOICE_COMPANION_ERROR:
        ret = bkvoice_gateway_terminal_locked(gateway, -EREMOTEIO);
        if (ret < 0)
          {
            return bkvoice_gateway_fault_locked(gateway, ret, false);
          }

        return bkvoice_gateway_fault_locked(gateway, -EREMOTEIO, false);

      default:
        break;
    }

  gateway->companion = candidate;
  gateway->rx_frames++;
  gateway->last_error = 0;
  return 0;
}

static int bkvoice_gateway_sink_start(
  void *context, const struct bkvoice_turn_token_s *token)
{
  struct bkvoice_gateway_s *gateway = context;
  int ret;

  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted)
    {
      ret = -ENOTCONN;
    }
  else if (gateway->ota_active)
    {
      ret = -EBUSY;
    }
  else if (!bkvoice_gateway_token_matches(gateway, token, true))
    {
      ret = -ESTALE;
    }
  else
    {
      ret = bkvoice_gateway_send_locked(
        gateway, BKVOICE_COMPANION_TURN_START, 0, NULL, 0,
        bkvoice_gateway_deadline(gateway));
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

static int bkvoice_gateway_sink_audio(
  void *context, const struct bkvoice_turn_token_s *token,
  const uint8_t *pcm, size_t bytes)
{
  struct bkvoice_gateway_s *gateway = context;
  int ret;

  if (gateway == NULL || !gateway->initialized || pcm == NULL)
    {
      return -EINVAL;
    }

  if (bytes != BKVOICE_COMPANION_AUDIO_FRAME_BYTES)
    {
      return -EMSGSIZE;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted)
    {
      ret = -ENOTCONN;
    }
  else if (!bkvoice_gateway_token_matches(gateway, token, false))
    {
      ret = -ESTALE;
    }
  else
    {
      ret = bkvoice_gateway_send_locked(
        gateway, BKVOICE_COMPANION_AUDIO_UP, 0, pcm, (uint32_t)bytes,
        bkvoice_gateway_deadline(gateway));
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

static int bkvoice_gateway_sink_end(
  void *context, const struct bkvoice_turn_token_s *token)
{
  struct bkvoice_gateway_s *gateway = context;
  int ret;

  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted)
    {
      ret = -ENOTCONN;
    }
  else if (!bkvoice_gateway_token_matches(gateway, token, false))
    {
      ret = -ESTALE;
    }
  else
    {
      ret = bkvoice_gateway_send_locked(
        gateway, BKVOICE_COMPANION_TURN_END, 0, NULL, 0,
        bkvoice_gateway_deadline(gateway));
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

int bkvoice_gateway_ack_stopped(struct bkvoice_gateway_s *gateway,
                               uint32_t session_id, uint32_t turn_id)
{
  int ret;

  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted ||
      gateway->companion.state != BKVOICE_COMPANION_IDLE ||
      session_id != gateway->companion.session_id || turn_id == 0 ||
      turn_id != gateway->companion.turn_id)
    {
      ret = -ESTALE;
    }
  else
    {
      ret = bkvoice_gateway_send_locked(gateway, BKVOICE_COMPANION_ACK,
                                        0, NULL, 0,
                                        bkvoice_gateway_deadline(gateway));
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

int bkvoice_gateway_report_status(
  struct bkvoice_gateway_s *gateway,
  const struct bkvoice_gateway_status_s *status)
{
  uint8_t payload[BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES];
  size_t payload_size;
  int ret;

  if (gateway == NULL || !gateway->initialized || status == NULL)
    {
      return -EINVAL;
    }

  memset(payload, 0, sizeof(payload));
  payload_size = status->firmware_identity_valid ?
                 BKVOICE_COMPANION_STATUS_REPORT_V2_BYTES :
                 BKVOICE_COMPANION_STATUS_REPORT_BYTES;
  payload[0] = status->firmware_identity_valid ? 2 : 1;
  payload[1] = status->battery_percent;
  payload[2] = status->charging;
  payload[3] = status->battery_state;
  bkvoice_gateway_put_be32(payload + 4, status->battery_voltage_mv);
  bkvoice_gateway_put_be16(payload + 8, status->firmware_major);
  bkvoice_gateway_put_be16(payload + 10, status->firmware_minor);
  bkvoice_gateway_put_be16(payload + 12, status->firmware_revision);
  bkvoice_gateway_put_be32(payload + 16, status->firmware_build);
  if (status->firmware_identity_valid)
    {
      memcpy(payload + BKVOICE_COMPANION_STATUS_REPORT_BYTES,
             status->firmware_root_sha256,
             BKVOICE_COMPANION_FIRMWARE_ROOT_BYTES);
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted)
    {
      ret = -ENOTCONN;
    }
  else if (gateway->companion.state != BKVOICE_COMPANION_IDLE)
    {
      ret = -EBUSY;
    }
  else
    {
      ret = bkvoice_gateway_send_locked(
        gateway, BKVOICE_COMPANION_STATUS_REPORT, 0, payload,
        payload_size, bkvoice_gateway_deadline(gateway));
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

int bkvoice_gateway_report_ota(
  struct bkvoice_gateway_s *gateway, uint32_t request_sequence,
  const uint8_t manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES],
  int result, enum bkvoice_companion_ota_phase_e phase,
  uint8_t progress)
{
  int ret;

  if (gateway == NULL || !gateway->initialized || manifest_sha256 == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted || gateway->companion.state !=
      BKVOICE_COMPANION_IDLE)
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = bkvoice_gateway_report_ota_locked(gateway, request_sequence,
                                               manifest_sha256, result,
                                               phase, progress);
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

static int bkvoice_gateway_sink_cancel(
  void *context, const struct bkvoice_turn_token_s *token, int reason)
{
  struct bkvoice_gateway_s *gateway = context;
  int ret;

  (void)reason;
  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted ||
      gateway->companion.state == BKVOICE_COMPANION_DISCONNECTED)
    {
      ret = 0;
    }
  else if (gateway->companion.state != BKVOICE_COMPANION_UPLINK &&
           gateway->companion.state != BKVOICE_COMPANION_THINKING &&
           gateway->companion.state != BKVOICE_COMPANION_DOWNLINK)
    {
      ret = 0;
    }
  else if (!bkvoice_gateway_token_matches(gateway, token, false))
    {
      ret = -ESTALE;
    }
  else
    {
      ret = bkvoice_gateway_send_locked(
        gateway, BKVOICE_COMPANION_CANCEL, 0, NULL, 0,
        bkvoice_gateway_deadline(gateway));
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

static const struct bkvoice_capture_sink_ops_s g_bkvoice_gateway_sink_ops =
{
  .start = bkvoice_gateway_sink_start,
  .audio = bkvoice_gateway_sink_audio,
  .end = bkvoice_gateway_sink_end,
  .cancel = bkvoice_gateway_sink_cancel,
};

int bkvoice_gateway_initialize(
  struct bkvoice_gateway_s *gateway,
  const struct bkvoice_transport_ops_s *transport_ops,
  void *transport_context,
  const struct bkvoice_gateway_downlink_ops_s *downlink_ops,
  void *downlink_context,
  bkvoice_gateway_now_ms_t now_ms, void *clock_context,
  const struct bkvoice_gateway_config_s *config,
  uint32_t boot_generation)
{
  int ret;

  if (gateway == NULL || downlink_context == NULL || now_ms == NULL ||
      config == NULL || config->io_timeout_ms == 0 ||
      !bkvoice_gateway_downlink_ops_valid(downlink_ops))
    {
      return -EINVAL;
    }

  memset(gateway, 0, sizeof(*gateway));
  ret = bkvoice_transport_initialize(&gateway->transport, transport_ops,
                                     transport_context);
  if (ret < 0)
    {
      return ret;
    }

  ret = bkvoice_companion_session_init(&gateway->companion,
                                       boot_generation);
  if (ret < 0)
    {
      return ret;
    }

  ret = pthread_mutex_init(&gateway->lock, NULL);
  if (ret != 0)
    {
      return -ret;
    }

  memcpy(&gateway->downlink_ops, downlink_ops, sizeof(*downlink_ops));
  memcpy(&gateway->config, config, sizeof(*config));
  gateway->downlink_context = downlink_context;
  gateway->clock_context = clock_context;
  gateway->now_ms = now_ms;
  gateway->initialized = true;
  return 0;
}

int bkvoice_gateway_uninitialize(struct bkvoice_gateway_s *gateway)
{
  struct bkvoice_transport_snapshot_s transport;
  int ret;

  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  bkvoice_transport_snapshot(&gateway->transport, &transport);
  if (transport.opened || gateway->companion.state !=
      BKVOICE_COMPANION_DISCONNECTED)
    {
      (void)bkvoice_gateway_unlock(gateway);
      return -EBUSY;
    }

  gateway->initialized = false;
  (void)bkvoice_gateway_unlock(gateway);
  ret = pthread_mutex_destroy(&gateway->lock);
  if (ret != 0)
    {
      gateway->initialized = true;
      return -ret;
    }

  memset(gateway, 0, sizeof(*gateway));
  return 0;
}

int bkvoice_gateway_connect(struct bkvoice_gateway_s *gateway,
                            uint64_t deadline_ms)
{
  uint8_t capabilities[4];
  uint32_t capability_mask;
  int close_ret;
  int ret;

  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted)
    {
      ret = -EIO;
      goto out;
    }

  if (gateway->connection_generation == UINT32_MAX)
    {
      ret = -EOVERFLOW;
      goto out;
    }

  ret = bkvoice_transport_open(&gateway->transport, deadline_ms);
  if (ret < 0)
    {
      gateway->last_error = ret;
      goto out;
    }

  gateway->connection_generation++;
  ret = bkvoice_companion_session_connect(&gateway->companion);
  if (ret < 0)
    {
      gateway->last_error = ret;
      close_ret = bkvoice_transport_close(&gateway->transport);
      if (close_ret < 0)
        {
          gateway->faulted = true;
        }

      goto out;
    }

  gateway->downlink_sequence = 0;
  gateway->tx_frames = 0;
  gateway->rx_frames = 0;
  capability_mask = BKVOICE_COMPANION_CAP_PLAYBACK_ACK |
                    BKVOICE_COMPANION_CAP_STATUS_REPORT;
  if (gateway->downlink_ops.volume != NULL)
    {
      capability_mask |= BKVOICE_COMPANION_CAP_VOLUME;
    }

  if (gateway->config.ota_request != NULL)
    {
      capability_mask |= BKVOICE_COMPANION_CAP_OTA;
    }

  bkvoice_gateway_put_be32(capabilities, capability_mask);
  ret = bkvoice_gateway_send_locked(gateway, BKVOICE_COMPANION_HELLO, 0,
                                    capabilities, sizeof(capabilities),
                                    deadline_ms);
  if (ret < 0)
    {
      /* No receive worker exists before HELLO succeeds, so this close cannot
       * race I/O.  A close failure remains retryable through disconnect().
       */

      close_ret = bkvoice_transport_close(&gateway->transport);
      if (close_ret >= 0)
        {
          bkvoice_companion_session_disconnect(&gateway->companion);
        }
    }

out:
  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

int bkvoice_gateway_interrupt(struct bkvoice_gateway_s *gateway)
{
  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  return bkvoice_transport_interrupt(&gateway->transport);
}

int bkvoice_gateway_disconnect(struct bkvoice_gateway_s *gateway,
                               int reason)
{
  int ret;

  if (gateway == NULL || !gateway->initialized)
    {
      return -EINVAL;
    }

  if (reason >= 0)
    {
      reason = -ENOTCONN;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (!gateway->faulted)
    {
      (void)bkvoice_gateway_terminal_locked(gateway, reason);
    }

  ret = bkvoice_transport_close(&gateway->transport);
  if (ret < 0)
    {
      gateway->faulted = true;
      gateway->last_error = ret;
      (void)bkvoice_gateway_unlock(gateway);
      return ret;
    }

  bkvoice_companion_session_disconnect(&gateway->companion);
  gateway->downlink_sequence = 0;
  gateway->ota_active = false;
  gateway->ota_request_sequence = 0;
  gateway->ota_phase = 0;
  gateway->ota_progress = 0;
  memset(gateway->ota_manifest_sha256, 0,
         sizeof(gateway->ota_manifest_sha256));
  gateway->faulted = false;
  gateway->last_error = reason;
  (void)bkvoice_gateway_unlock(gateway);
  return 0;
}

int bkvoice_gateway_receive_frame(struct bkvoice_gateway_s *gateway,
                                  struct bkvoice_gateway_frame_s *frame,
                                  uint64_t deadline_ms)
{
  uint32_t payload_len;
  int ret;

  if (frame == NULL)
    {
      return -EINVAL;
    }

  memset(frame, 0, sizeof(*frame));
  if (gateway == NULL || !gateway->initialized)
    {
      frame->error = -EINVAL;
      return frame->error;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      frame->error = ret;
      return ret;
    }

  frame->generation = gateway->connection_generation;
  ret = gateway->faulted || gateway->companion.state ==
        BKVOICE_COMPANION_DISCONNECTED ? -ENOTCONN : 0;
  (void)bkvoice_gateway_unlock(gateway);
  if (ret < 0)
    {
      frame->error = ret;
      return ret;
    }

  ret = bkvoice_transport_recv_exact(&gateway->transport, frame->data,
                                     BKVOICE_COMPANION_HEADER_BYTES,
                                     deadline_ms);
  if (ret < 0)
    {
      frame->error = ret;
      return ret;
    }

  payload_len = bkvoice_gateway_get_be32(frame->data + 12);
  if (payload_len > BKVOICE_GATEWAY_MAX_PAYLOAD)
    {
      frame->error = -EMSGSIZE;
      return frame->error;
    }

  ret = bkvoice_transport_recv_exact(
    &gateway->transport, frame->data + BKVOICE_COMPANION_HEADER_BYTES,
    payload_len, deadline_ms);
  if (ret < 0)
    {
      frame->error = ret;
      return ret;
    }

  frame->size = BKVOICE_COMPANION_HEADER_BYTES + payload_len;
  return 0;
}

int bkvoice_gateway_dispatch_frame(
  struct bkvoice_gateway_s *gateway,
  const struct bkvoice_gateway_frame_s *frame)
{
  struct bkvoice_companion_header_s header;
  const uint8_t *payload;
  int ret;

  if (gateway == NULL || !gateway->initialized || frame == NULL)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  /* Check generation before inspecting errors or payload.  A queued frame
   * from the previous connection has no authority over this connection.
   */

  if (frame->generation == 0 ||
      frame->generation != gateway->connection_generation)
    {
      ret = -ESTALE;
      goto out;
    }

  if (gateway->faulted || gateway->companion.state ==
      BKVOICE_COMPANION_DISCONNECTED)
    {
      ret = -ENOTCONN;
      goto out;
    }

  if (frame->error != 0)
    {
      ret = bkvoice_gateway_fault_locked(gateway, frame->error, true);
      goto out;
    }

  if (frame->size < BKVOICE_COMPANION_HEADER_BYTES ||
      frame->size > sizeof(frame->data))
    {
      ret = bkvoice_gateway_fault_locked(gateway, -EMSGSIZE, true);
      goto out;
    }

  ret = bkvoice_companion_decode(frame->data, frame->size, &header, &payload);
  if (ret < 0)
    {
      ret = bkvoice_gateway_fault_locked(gateway, ret, true);
    }
  else
    {
      ret = bkvoice_gateway_dispatch_locked(gateway, &header, payload);
    }

out:
  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

int bkvoice_gateway_receive_one(struct bkvoice_gateway_s *gateway,
                                uint64_t deadline_ms)
{
  struct bkvoice_gateway_frame_s frame;
  int ret;

  ret = bkvoice_gateway_receive_frame(gateway, &frame, deadline_ms);
  if (frame.generation == 0)
    {
      return ret;
    }

  return bkvoice_gateway_dispatch_frame(gateway, &frame);
}

int bkvoice_gateway_grant_downlink(struct bkvoice_gateway_s *gateway,
                                   uint32_t bytes)
{
  int ret;

  if (gateway == NULL || !gateway->initialized || bytes == 0 ||
      bytes > BKVOICE_COMPANION_MAX_WINDOW)
    {
      return -EINVAL;
    }

  ret = bkvoice_gateway_lock(gateway);
  if (ret < 0)
    {
      return ret;
    }

  if (gateway->faulted)
    {
      ret = -ENOTCONN;
    }
  else
    {
      ret = bkvoice_gateway_grant_downlink_locked(gateway, bytes);
    }

  (void)bkvoice_gateway_unlock(gateway);
  return ret;
}

const struct bkvoice_capture_sink_ops_s *bkvoice_gateway_capture_sink_ops(void)
{
  return &g_bkvoice_gateway_sink_ops;
}

void bkvoice_gateway_snapshot(struct bkvoice_gateway_s *gateway,
                              struct bkvoice_gateway_snapshot_s *snapshot)
{
  if (gateway == NULL || snapshot == NULL || !gateway->initialized)
    {
      return;
    }

  if (bkvoice_gateway_lock(gateway) < 0)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  bkvoice_transport_snapshot(&gateway->transport, &snapshot->transport);
  snapshot->companion_state = gateway->companion.state;
  snapshot->boot_generation = gateway->companion.boot_generation;
  snapshot->session_id = gateway->companion.session_id;
  snapshot->turn_id = gateway->companion.turn_id;
  snapshot->tx_sequence = gateway->companion.tx_sequence;
  snapshot->rx_sequence = gateway->companion.rx_sequence;
  snapshot->tx_window = gateway->companion.tx_window;
  snapshot->rx_window = gateway->companion.rx_window;
  snapshot->connection_generation = gateway->connection_generation;
  snapshot->downlink_sequence = gateway->downlink_sequence;
  snapshot->tx_frames = gateway->tx_frames;
  snapshot->rx_frames = gateway->rx_frames;
  snapshot->ota_request_sequence = gateway->ota_request_sequence;
  snapshot->ota_phase = gateway->ota_phase;
  snapshot->ota_progress = gateway->ota_progress;
  snapshot->ota_active = gateway->ota_active;
  snapshot->last_error = gateway->last_error;
  snapshot->initialized = gateway->initialized;
  snapshot->faulted = gateway->faulted;
  (void)bkvoice_gateway_unlock(gateway);
}
