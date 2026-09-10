/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>
#include <nuttx/contactless/isodep.h>

/* ISO/IEC 14443-4:2018 section 5: frame sizes include PCB and CRC.
 * FWT/SFGT use the 13.56 MHz carrier. Round up so the reader never shortens
 * the card's advertised waiting time. NXP AN12057 specifies RFU defaults.
 */

static uint32_t isodep_wait_us(unsigned int exponent)
{
  return (((uint64_t)4096 << exponent) * 1000000 + 13560000 - 1) /
         13560000;
}

static int isodep_parse_ats(struct isodep_session_s *session,
                           const uint8_t *ats, size_t length)
{
  static const uint16_t sizes[] =
  {
    16, 24, 32, 40, 48, 64, 96, 128, 256, 512, 1024, 2048, 4096
  };
  size_t cursor = 2;
  unsigned int fsci = 2;
  unsigned int fwi = 4;
  unsigned int sfgi = 0;

  if (length == 0 || length > 62 || ats[0] != length)
    {
      return -EPROTO;
    }

  if (length > 1)
    {
      fsci = ats[1] & 0x0f;
      if (ats[1] & 0x10)
        {
          if (cursor >= length)
            {
              return -EPROTO;
            }

          /* Remain at 106 kbit/s; no PPS negotiation is needed. */

          cursor++;
        }

      if (ats[1] & 0x20)
        {
          if (cursor >= length)
            {
              return -EPROTO;
            }

          fwi = ats[cursor] >> 4;
          sfgi = ats[cursor++] & 0x0f;
        }

      if (ats[1] & 0x40)
        {
          if (cursor >= length)
            {
              return -EPROTO;
            }

          /* CID and NAD are optional and are not used by this reader. */

          cursor++;
        }
    }

  /* Historical bytes are opaque and do not identify or authenticate a user. */

  session->fsc = sizes[fsci > 12 ? 12 : fsci];
  session->fwt_us = isodep_wait_us(fwi == 15 ? 4 : fwi);
  session->sfgt_us = sfgi == 0 || sfgi == 15 ? 0 : isodep_wait_us(sfgi);
  return 0;
}

void isodep_release(struct isodep_session_s *session)
{
  if (session == NULL)
    {
      return;
    }

  if (session->transport != NULL)
    {
      session->transport->release(session->arg);
    }

  memset(session, 0, sizeof(*session));
}

int isodep_activate(struct isodep_session_s *session,
                   const struct isodep_transport_s *transport,
                   void *arg, uint8_t sak)
{
  const uint8_t rats[] = {0xe0, 0x50};
  uint8_t ats[62];
  size_t length = sizeof(ats);
  int ret;

  if (session == NULL || transport == NULL ||
      transport->exchange == NULL || transport->delay_us == NULL ||
      transport->release == NULL || transport->now_ms == NULL)
    {
      return -EINVAL;
    }

  if (session->transport != NULL || session->active)
    {
      return -EBUSY;
    }

  session->transport = transport;
  session->arg = arg;
  if ((sak & 0x20) == 0 || (sak & 0x04) != 0)
    {
      ret = -EPROTONOSUPPORT;
      goto fail;
    }

  /* RATS precedes knowledge of FWI: use the maximum base FWT (FWI 14). */

  ret = transport->exchange(arg, rats, sizeof(rats), ats, &length,
                             (isodep_wait_us(14) + 999) / 1000);
  if (ret < 0)
    {
      goto fail;
    }

  ret = isodep_parse_ats(session, ats, length);
  if (ret < 0)
    {
      goto fail;
    }

  if (session->sfgt_us != 0)
    {
      ret = transport->delay_us(arg, session->sfgt_us);
      if (ret < 0)
        {
          goto fail;
        }
    }

  session->block = 0;
  session->active = true;
  return 0;

fail:
  isodep_release(session);
  return ret;
}

/* Keep WTX inside the caller's original deadline. A WTX response changes only
 * the next frame's wait; it never restarts the APDU budget or block sequence.
 */

static int isodep_exchange(struct isodep_session_s *session,
                          const uint8_t *tx, size_t tx_length,
                          uint8_t *rx, size_t *rx_length, uint64_t deadline)
{
  const struct isodep_transport_s *ops = session->transport;
  uint8_t wtx[2];
  uint32_t wait_us = session->fwt_us;
  uint64_t now;
  uint32_t wait_ms;
  int ret;

  for (;;)
    {
      now = ops->now_ms(session->arg);
      if (now >= deadline)
        {
          return -ETIMEDOUT;
        }

      wait_ms = (wait_us + 999) / 1000;
      if (wait_ms > deadline - now)
        {
          wait_ms = deadline - now;
        }

      *rx_length = 62;
      ret = ops->exchange(session->arg, tx, tx_length, rx, rx_length,
                          wait_ms);
      if (ret < 0)
        {
          return ret;
        }

      if (ops->now_ms(session->arg) >= deadline)
        {
          return -ETIMEDOUT;
        }

      if (*rx_length == 0 || *rx_length > 62)
        {
          return -EPROTO;
        }

      if (rx[0] != 0xf2)
        {
          return 0;
        }

      if (*rx_length != 2 || (rx[1] & 0x3f) == 0 ||
          (rx[1] & 0x3f) > 59)
        {
          return -EPROTO;
        }

      wtx[0] = 0xf2;
      wtx[1] = rx[1] & 0x3f;
      wait_us = session->fwt_us * wtx[1];
      tx = wtx;
      tx_length = sizeof(wtx);
    }
}

int isodep_transceive(struct isodep_session_s *session,
                     const uint8_t *command, size_t command_length,
                     uint8_t *response, size_t *response_length,
                     uint32_t budget_ms)
{
  uint8_t tx[62];
  uint8_t rx[62];
  size_t rx_length;
  size_t capacity;
  size_t sent = 0;
  size_t received = 0;
  size_t chunk;
  size_t max_payload;
  uint64_t deadline;
  unsigned int retries = 0;
  bool chained;
  int ret;

  if (response_length == NULL)
    {
      return -EINVAL;
    }

  capacity = *response_length;
  *response_length = 0;
  if (session == NULL || !session->active || session->transport == NULL ||
      command == NULL || command_length == 0 || response == NULL ||
      capacity == 0 || budget_ms == 0 || budget_ms > 300000)
    {
      return -EINVAL;
    }

  deadline = session->transport->now_ms(session->arg) + budget_ms;
  max_payload = session->fsc < 64 ? session->fsc - 3 : 61;

  /* Send command fragments. Only advance after the peer acknowledges the
   * next block number. Retransmission never advances the command offset.
   */

  while (sent < command_length)
    {
      chunk = command_length - sent;
      if (chunk > max_payload)
        {
          chunk = max_payload;
        }

      chained = chunk < command_length - sent;
      tx[0] = 0x02 | session->block | (chained ? 0x10 : 0);
      memcpy(tx + 1, command + sent, chunk);
      ret = isodep_exchange(session, tx, chunk + 1, rx, &rx_length,
                             deadline);
      if (ret < 0)
        {
          goto fail;
        }

      if (rx_length == 1 &&
          (rx[0] == (0xa2 | session->block) ||
           rx[0] == (0xb2 | session->block)))
        {
          if (++retries > 2)
            {
              ret = -EPROTO;
              goto fail;
            }

          continue;
        }

      retries = 0;
      if (!chained)
        {
          break;
        }

      if (rx_length != 1 || rx[0] != (0xa2 | (session->block ^ 1)))
        {
          ret = -EPROTO;
          goto fail;
        }

      sent += chunk;
      session->block ^= 1;
    }

  for (;;)
    {
      /* This reader never negotiated CID or NAD. Reject fields that would
       * otherwise be mistaken for application payload.
       */

      if ((rx[0] & 0xee) != 0x02)
        {
          ret = -EPROTO;
          goto fail;
        }

      if ((rx[0] & 1) != session->block)
        {
          /* A lost ACK can cause the preceding response fragment to repeat.
           * Acknowledge again without appending the duplicate payload.
           */

          if (received == 0 || ++retries > 2 || !(rx[0] & 0x10))
            {
              ret = -EPROTO;
              goto fail;
            }
        }
      else
        {
          if (rx_length - 1 > capacity - received)
            {
              ret = -EMSGSIZE;
              goto fail;
            }

          memcpy(response + received, rx + 1, rx_length - 1);
          received += rx_length - 1;
          session->block ^= 1;
          retries = 0;
          if (!(rx[0] & 0x10))
            {
              *response_length = received;
              return 0;
            }
        }

      tx[0] = 0xa2 | session->block;
      ret = isodep_exchange(session, tx, 1, rx, &rx_length, deadline);
      if (ret < 0)
        {
          goto fail;
        }
    }

fail:
  memset(response, 0, capacity);
  isodep_release(session);
  return ret;
}
