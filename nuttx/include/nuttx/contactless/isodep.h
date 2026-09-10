/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __NUTTX_CONTACTLESS_ISODEP_H
#define __NUTTX_CONTACTLESS_ISODEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Transport handles CRC_A, selection and exclusive ownership of the RF field.
 * Lengths exclude CRC. exchange returns zero or a negative errno; rx_length
 * is capacity on entry and actual length on success. release drops the RF
 * field and abandons selection. All operations are synchronous.
 */

struct isodep_transport_s
{
  int (*exchange)(void *arg, const uint8_t *tx, size_t tx_length,
                  uint8_t *rx, size_t *rx_length, uint32_t timeout_ms);
  int (*delay_us)(void *arg, uint32_t delay_us);
  void (*release)(void *arg);
  uint64_t (*now_ms)(void *arg); /* Monotonic, including transport waits */
};

struct isodep_session_s
{
  const struct isodep_transport_s *transport;
  void *arg;
  uint32_t fwt_us;
  uint32_t sfgt_us;
  uint16_t fsc;
  uint8_t block;
  bool active;
};

/* Zero-initialize the session before first use. Activate immediately after
 * ISO14443-A selection. This implementation advertises a 64-byte receive
 * frame and uses 106 kbit/s, CID 0 without a CID field, and no NAD.
 * Card support for other rates/CID/NAD does not require using those options.
 */

int isodep_activate(struct isodep_session_s *session,
                   const struct isodep_transport_s *transport,
                   void *arg, uint8_t sak);
/* Exchange one APDU, including outgoing/incoming chaining and WTX.
 * response_length is capacity on entry and actual length on success.
 * budget_ms bounds the entire exchange, not each WTX independently.
 * Protocol/transport failures clear response and release the RF field.
 */

int isodep_transceive(struct isodep_session_s *session,
                     const uint8_t *command, size_t command_length,
                     uint8_t *response, size_t *response_length,
                     uint32_t budget_ms);
void isodep_release(struct isodep_session_s *session);

#endif
