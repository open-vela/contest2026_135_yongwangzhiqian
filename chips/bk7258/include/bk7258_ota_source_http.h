/****************************************************************************
 * chips/bk7258/include/bk7258_ota_source_http.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_HTTP_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_HTTP_H

#include <nuttx/config.h>

#include <arch/chip/bk7258_ota.h>

#ifdef __cplusplus
extern "C"
{
#endif

struct bk7258_ota_http_source_s
{
  void *priv;
};

#ifdef CONFIG_BK7258_OTA_SOURCE_HTTP

struct mbedtls_x509_crt;
struct mbedtls_pk_context;
struct in_addr;

int bk7258_ota_http_source_initialize(
  struct bk7258_ota_http_source_s *source, const char *catalog_url,
  const char *ca_path);

/* The supplied credentials remain owned by the caller and must remain valid
 * until the source has been closed.  This entry point accepts HTTPS only.
 */

int bk7258_ota_http_source_initialize_with_credentials(
  struct bk7258_ota_http_source_s *source, const char *catalog_url,
  const struct in_addr *peer_address,
  struct mbedtls_x509_crt *server_ca,
  struct mbedtls_x509_crt *client_certificate,
  struct mbedtls_pk_context *client_key);

/* HTTPS with mandatory hostname/server-chain verification and a fixed IPv4
 * dialing address, without a client certificate. The caller owns server_ca
 * until source close. This does not alter catalog/image signature checks.
 */
int bk7258_ota_http_source_initialize_with_server_ca(
  struct bk7258_ota_http_source_s *source, const char *catalog_url,
  const struct in_addr *peer_address, struct mbedtls_x509_crt *server_ca);

/* Bind this source to the SHA-256 of the raw catalog bytes.  The digest is
 * copied by the source and may only be set after initialization and before
 * its first open attempt.  An unset expectation preserves legacy behavior.
 */

int bk7258_ota_http_source_expect_catalog(
  struct bk7258_ota_http_source_s *source,
  const uint8_t sha256[BK7258_OTA_SHA256_SIZE]);

const struct bk7258_ota_source_ops_s *bk7258_ota_http_source_ops(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_HTTP_H */
