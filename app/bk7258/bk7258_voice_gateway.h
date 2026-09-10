/****************************************************************************
 * app/bk7258/bk7258_voice_gateway.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * App-private companion-v1 session owner and voice downlink dispatcher.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_GATEWAY_H
#define __APP_BK7258_BK7258_VOICE_GATEWAY_H

#include "bk7258_voice_capture.h"
#include "bk7258_voice_companion.h"
#include "bk7258_voice_transport.h"
#include "bk7258_voice_turn.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BKVOICE_GATEWAY_MAX_PAYLOAD BKVOICE_COMPANION_AUDIO_FRAME_BYTES
#define BKVOICE_GATEWAY_FRAME_BYTES \
  (BKVOICE_COMPANION_HEADER_BYTES + BKVOICE_GATEWAY_MAX_PAYLOAD)

/* Local queue envelope, never a wire format.  Even receive failures carry
 * their connection generation so an old error cannot fault a new connection.
 */

struct bkvoice_gateway_frame_s
{
  uint32_t generation;
  uint32_t size;
  int error;
  uint8_t data[BKVOICE_GATEWAY_FRAME_BYTES];
};

typedef uint64_t (*bkvoice_gateway_now_ms_t)(void *context);

/* Called by the serialized gateway dispatcher after an authenticated OTA
 * manifest digest is accepted.  This is a bounded nonblocking admission
 * hook: the adapter MUST only copy/enqueue the request and return.  It must
 * not download, write flash, wait, or re-enter gateway APIs while called.
 * Returning zero starts at DOWNLOADING. A negative value is reported as
 * FAILED and leaves no active request. */

typedef int (*bkvoice_gateway_ota_request_t)(
  void *context, uint32_t request_sequence,
  const uint8_t manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES]);

struct bkvoice_gateway_downlink_ops_s
{
  int (*tts_start)(void *context,
                   const struct bkvoice_turn_token_s *token,
                   uint64_t now_ms);
  int (*tts_audio)(void *context,
                   const struct bkvoice_turn_token_s *token,
                   const uint8_t *pcm, size_t bytes,
                   uint64_t now_ms);
  int (*tts_end)(void *context,
                 const struct bkvoice_turn_token_s *token);

  /* terminal() only posts a bounded event to the serialized product owner
   * after a remote cancel or fatal session error.  It must not synchronously
   * stop/join capture or re-enter this gateway while the callback is running;
   * the owner performs cleanup after the callback returns.
   */

  int (*terminal)(void *context, uint32_t session_id, uint32_t turn_id,
                  int reason);

  /* Optional synchronous product-owner operation; never re-enter gateway.
   * set=false reads current media policy volume. On success return its
   * percentage, including policy quantization, not the requested value.
   * This is not an independent DAC register or acoustic measurement.
   */

  int (*volume)(void *context, bool set, unsigned int requested,
                unsigned int *observed);
};

struct bkvoice_gateway_config_s
{
  uint64_t io_timeout_ms;
  bkvoice_gateway_ota_request_t ota_request;
  void *ota_context;
};

struct bkvoice_gateway_snapshot_s
{
  struct bkvoice_transport_snapshot_s transport;
  enum bkvoice_companion_state_e companion_state;
  uint32_t boot_generation;
  uint32_t session_id;
  uint32_t turn_id;
  uint32_t tx_sequence;
  uint32_t rx_sequence;
  uint32_t tx_window;
  uint32_t rx_window;
  uint32_t connection_generation;
  uint32_t downlink_sequence;
  uint32_t tx_frames;
  uint32_t rx_frames;
  uint32_t ota_request_sequence;
  uint8_t ota_phase;
  uint8_t ota_progress;
  bool ota_active;
  int last_error;
  bool initialized;
  bool faulted;
};

struct bkvoice_gateway_status_s
{
  uint8_t battery_percent;
  uint8_t charging;
  uint8_t battery_state;
  uint32_t battery_voltage_mv;
  uint16_t firmware_major;
  uint16_t firmware_minor;
  uint16_t firmware_revision;
  uint32_t firmware_build;
  uint8_t firmware_root_sha256[BKVOICE_COMPANION_FIRMWARE_ROOT_BYTES];
  bool firmware_identity_valid;
};

struct bkvoice_gateway_s
{
  struct bkvoice_transport_s transport;
  struct bkvoice_companion_session_s companion;
  struct bkvoice_gateway_downlink_ops_s downlink_ops;
  struct bkvoice_gateway_config_s config;
  bkvoice_gateway_now_ms_t now_ms;
  void *clock_context;
  void *downlink_context;
  pthread_mutex_t lock;
  uint8_t tx_frame[BKVOICE_GATEWAY_FRAME_BYTES];
  uint32_t connection_generation;
  uint32_t downlink_sequence;
  uint32_t tx_frames;
  uint32_t rx_frames;
  uint32_t ota_request_sequence;
  uint8_t ota_manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES];
  uint8_t ota_phase;
  uint8_t ota_progress;
  bool ota_active;
  int last_error;
  bool initialized;
  bool faulted;
};

/* The product owner serializes connect/disconnect/uninitialize and joins all
 * I/O callers before disconnect.  Capture callbacks and one receive worker
 * may run concurrently; the gateway lock serializes companion state and wire
 * writes while receive I/O remains interruptible.  Downlink callbacks hand
 * events to the existing PTT/turn owner and must not re-enter the gateway.
 */

int bkvoice_gateway_initialize(
  struct bkvoice_gateway_s *gateway,
  const struct bkvoice_transport_ops_s *transport_ops,
  void *transport_context,
  const struct bkvoice_gateway_downlink_ops_s *downlink_ops,
  void *downlink_context,
  bkvoice_gateway_now_ms_t now_ms, void *clock_context,
  const struct bkvoice_gateway_config_s *config,
  uint32_t boot_generation);
int bkvoice_gateway_uninitialize(struct bkvoice_gateway_s *gateway);
int bkvoice_gateway_connect(struct bkvoice_gateway_s *gateway,
                            uint64_t deadline_ms);
int bkvoice_gateway_interrupt(struct bkvoice_gateway_s *gateway);
int bkvoice_gateway_disconnect(struct bkvoice_gateway_s *gateway,
                               int reason);

/* One RX caller performs only stream I/O and bounded length checks.  Queue
 * the envelope on both success and error; stop RX after an error until the
 * owner has dispatched it.  RX never calls downlink/terminal or changes the
 * companion state.  The serialized owner dispatches envelopes in RX order.
 * A stale generation returns -ESTALE without changing the current session.
 * Interrupt and join RX before disconnect/close or uninitialize.
 */

int bkvoice_gateway_receive_frame(struct bkvoice_gateway_s *gateway,
                                  struct bkvoice_gateway_frame_s *frame,
                                  uint64_t deadline_ms);
int bkvoice_gateway_dispatch_frame(
  struct bkvoice_gateway_s *gateway,
  const struct bkvoice_gateway_frame_s *frame);
int bkvoice_gateway_receive_one(struct bkvoice_gateway_s *gateway,
                                uint64_t deadline_ms);
/* Advertise initial receive capacity. Successfully accepted AUDIO_DOWN frames
 * replenish their own credit inside the serialized dispatcher.
 */

int bkvoice_gateway_grant_downlink(struct bkvoice_gateway_s *gateway,
                                   uint32_t bytes);
/* Called only by the serialized product owner after successful playback or
 * cancellation cleanup.  ACK identifies the same session/turn and never
 * acknowledges a newer turn or merely queued terminal event.
 */

int bkvoice_gateway_ack_stopped(struct bkvoice_gateway_s *gateway,
                               uint32_t session_id, uint32_t turn_id);
/* Called by the serialized product owner while the companion is idle. */

int bkvoice_gateway_report_status(
  struct bkvoice_gateway_s *gateway,
  const struct bkvoice_gateway_status_s *status);
/* Serialized product-owner progress update for the currently admitted OTA
 * request. The request sequence and digest must exactly match the request;
 * stale or out-of-order updates are rejected without a wire write. */

int bkvoice_gateway_report_ota(
  struct bkvoice_gateway_s *gateway, uint32_t request_sequence,
  const uint8_t manifest_sha256[BKVOICE_COMPANION_OTA_MANIFEST_SHA256_BYTES],
  int result, enum bkvoice_companion_ota_phase_e phase,
  uint8_t progress);
const struct bkvoice_capture_sink_ops_s *bkvoice_gateway_capture_sink_ops(void);
void bkvoice_gateway_snapshot(struct bkvoice_gateway_s *gateway,
                              struct bkvoice_gateway_snapshot_s *snapshot);

#endif /* __APP_BK7258_BK7258_VOICE_GATEWAY_H */
