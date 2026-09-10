/****************************************************************************
 * app/bk7258/bk7258_voice_transport.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_voice_transport.h"

#include <errno.h>
#include <string.h>

static bool bkvoice_transport_ops_valid(
  const struct bkvoice_transport_ops_s *ops)
{
  return ops != NULL && ops->open != NULL && ops->send != NULL &&
         ops->recv != NULL && ops->interrupt != NULL && ops->close != NULL;
}

static int bkvoice_transport_error(struct bkvoice_transport_s *transport,
                                   int error)
{
  if (error >= 0)
    {
      error = -EIO;
    }

  __atomic_store_n(&transport->last_error, error, __ATOMIC_RELEASE);
  return error;
}

int bkvoice_transport_initialize(
  struct bkvoice_transport_s *transport,
  const struct bkvoice_transport_ops_s *ops, void *context)
{
  if (transport == NULL || context == NULL ||
      !bkvoice_transport_ops_valid(ops))
    {
      return -EINVAL;
    }

  memset(transport, 0, sizeof(*transport));
  memcpy(&transport->ops, ops, sizeof(*ops));
  transport->context = context;
  transport->initialized = true;
  return 0;
}

int bkvoice_transport_open(struct bkvoice_transport_s *transport,
                           uint64_t deadline_ms)
{
  int ret;

  if (transport == NULL || !transport->initialized)
    {
      return -EINVAL;
    }

  if (__atomic_load_n(&transport->opened, __ATOMIC_ACQUIRE))
    {
      return -EALREADY;
    }

  ret = transport->ops.open(transport->context, deadline_ms);
  if (ret < 0)
    {
      return bkvoice_transport_error(transport, ret);
    }

  __atomic_store_n(&transport->tx_bytes, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&transport->rx_bytes, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&transport->last_error, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&transport->opened, true, __ATOMIC_RELEASE);
  return 0;
}

int bkvoice_transport_send_all(struct bkvoice_transport_s *transport,
                               const uint8_t *buffer, size_t bytes,
                               uint64_t deadline_ms)
{
  size_t offset = 0;
  ssize_t sent;

  if (transport == NULL || !transport->initialized ||
      (bytes != 0 && buffer == NULL))
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&transport->opened, __ATOMIC_ACQUIRE))
    {
      return -ENOTCONN;
    }

  if ((uint64_t)bytes >
      UINT32_MAX - __atomic_load_n(&transport->tx_bytes,
                                   __ATOMIC_RELAXED))
    {
      return bkvoice_transport_error(transport, -EOVERFLOW);
    }

  while (offset < bytes)
    {
      sent = transport->ops.send(transport->context, buffer + offset,
                                 bytes - offset, deadline_ms);
      if (sent < 0)
        {
          return bkvoice_transport_error(transport, (int)sent);
        }

      if (sent == 0)
        {
          return bkvoice_transport_error(transport, -EPIPE);
        }

      if ((size_t)sent > bytes - offset)
        {
          return bkvoice_transport_error(transport, -EPROTO);
        }

      offset += (size_t)sent;
      __atomic_add_fetch(&transport->tx_bytes, (uint32_t)sent,
                         __ATOMIC_RELAXED);
    }

  __atomic_store_n(&transport->last_error, 0, __ATOMIC_RELEASE);
  return 0;
}

int bkvoice_transport_recv_exact(struct bkvoice_transport_s *transport,
                                 uint8_t *buffer, size_t bytes,
                                 uint64_t deadline_ms)
{
  size_t offset = 0;
  ssize_t received;

  if (transport == NULL || !transport->initialized ||
      (bytes != 0 && buffer == NULL))
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&transport->opened, __ATOMIC_ACQUIRE))
    {
      return -ENOTCONN;
    }

  if ((uint64_t)bytes >
      UINT32_MAX - __atomic_load_n(&transport->rx_bytes,
                                   __ATOMIC_RELAXED))
    {
      return bkvoice_transport_error(transport, -EOVERFLOW);
    }

  while (offset < bytes)
    {
      received = transport->ops.recv(transport->context, buffer + offset,
                                     bytes - offset, deadline_ms);
      if (received < 0)
        {
          return bkvoice_transport_error(transport, (int)received);
        }

      if (received == 0)
        {
          return bkvoice_transport_error(transport, -ECONNRESET);
        }

      if ((size_t)received > bytes - offset)
        {
          return bkvoice_transport_error(transport, -EPROTO);
        }

      offset += (size_t)received;
      __atomic_add_fetch(&transport->rx_bytes, (uint32_t)received,
                         __ATOMIC_RELAXED);
    }

  __atomic_store_n(&transport->last_error, 0, __ATOMIC_RELEASE);
  return 0;
}

int bkvoice_transport_interrupt(struct bkvoice_transport_s *transport)
{
  int ret;

  if (transport == NULL || !transport->initialized)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&transport->opened, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  ret = transport->ops.interrupt(transport->context);
  if (ret < 0)
    {
      return bkvoice_transport_error(transport, ret);
    }

  return 0;
}

int bkvoice_transport_close(struct bkvoice_transport_s *transport)
{
  int ret;

  if (transport == NULL || !transport->initialized)
    {
      return -EINVAL;
    }

  if (!__atomic_load_n(&transport->opened, __ATOMIC_ACQUIRE))
    {
      return 0;
    }

  ret = transport->ops.close(transport->context);
  if (ret < 0)
    {
      return bkvoice_transport_error(transport, ret);
    }

  __atomic_store_n(&transport->opened, false, __ATOMIC_RELEASE);
  __atomic_store_n(&transport->last_error, 0, __ATOMIC_RELEASE);
  return 0;
}

void bkvoice_transport_snapshot(
  const struct bkvoice_transport_s *transport,
  struct bkvoice_transport_snapshot_s *snapshot)
{
  if (transport == NULL || snapshot == NULL)
    {
      return;
    }

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->tx_bytes = __atomic_load_n(&transport->tx_bytes,
                                       __ATOMIC_ACQUIRE);
  snapshot->rx_bytes = __atomic_load_n(&transport->rx_bytes,
                                       __ATOMIC_ACQUIRE);
  snapshot->last_error = __atomic_load_n(&transport->last_error,
                                         __ATOMIC_ACQUIRE);
  snapshot->opened = __atomic_load_n(&transport->opened, __ATOMIC_ACQUIRE);
  snapshot->initialized = transport->initialized;
}
