/* SPDX-License-Identifier: Apache-2.0 */
/* POSIX socket adapter used only by test_bk7258_ota_source_http.py. */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mbedtls/x509_crt.h>

#include <arch/chip/bk7258_ota_source_http.h>
#include <nuttx/net/net.h>

static int socket_errno(void)
{
  return errno == 0 ? -EIO : -errno;
}

int psock_socket(int domain, int type, int protocol, struct socket *psocket)
{
  if (psocket == NULL)
    {
      return -EINVAL;
    }

  /* A synchronous loopback connect reaches the production connected branch
   * without implementing NuttX's asynchronous poll registration.  This is
   * not an RPMsg or production psock transport test. */
  psocket->fd = socket(domain, type & ~SOCK_NONBLOCK, protocol);
  return psocket->fd < 0 ? socket_errno() : 0;
}

int psock_close(struct socket *psocket)
{
  int ret = close(psocket->fd);
  psocket->fd = -1;
  return ret == 0 ? 0 : socket_errno();
}

int psock_connect(struct socket *psocket, const struct sockaddr *address,
                  socklen_t length)
{
  return connect(psocket->fd, address, length) == 0 ? 0 : socket_errno();
}

ssize_t psock_send(struct socket *psocket, const void *data, size_t size,
                   int flags)
{
  ssize_t ret = send(psocket->fd, data, size, flags);
  return ret < 0 ? socket_errno() : ret;
}

ssize_t psock_recv(struct socket *psocket, void *data, size_t size, int flags)
{
  ssize_t ret = recv(psocket->fd, data, size, flags);
  return ret < 0 ? socket_errno() : ret;
}

int psock_setsockopt(struct socket *psocket, int level, int option,
                     const void *data, socklen_t length)
{
  return setsockopt(psocket->fd, level, option, data, length) == 0 ?
         0 : socket_errno();
}

int psock_getsockopt(struct socket *psocket, int level, int option,
                     void *data, socklen_t *length)
{
  return getsockopt(psocket->fd, level, option, data, length) == 0 ?
         0 : socket_errno();
}

int psock_ioctl(struct socket *psocket, int request, ...)
{
  va_list arguments;
  unsigned long argument;
  int ret;

  va_start(arguments, request);
  argument = va_arg(arguments, unsigned long);
  va_end(arguments);
  ret = ioctl(psocket->fd, request, argument);
  return ret == 0 ? 0 : socket_errno();
}

int psock_shutdown(struct socket *psocket, int how)
{
  return shutdown(psocket->fd, how) == 0 ? 0 : socket_errno();
}

int psock_poll(struct socket *psocket, struct pollfd *fds, bool setup)
{
  (void)psocket;
  (void)fds;
  (void)setup;
  return -ENOSYS;
}

static void decode_digest(const char *hex, uint8_t digest[32])
{
  size_t index;

  assert(strlen(hex) == 64u);
  for (index = 0u; index < 32u; index++)
    {
      unsigned int value;
      assert(sscanf(hex + index * 2u, "%2x", &value) == 1);
      digest[index] = (uint8_t)value;
    }
}

static void assert_open(const char *url, const uint8_t digest[32],
                        uint32_t security_counter, int wanted)
{
  struct bk7258_ota_http_source_s source = {0};
  struct bk7258_ota_manifest_s manifest;
  const struct bk7258_ota_source_ops_s *ops;
  int ret;

  assert(bk7258_ota_http_source_initialize(&source, url, "") == 0);
  assert(bk7258_ota_http_source_expect_catalog(&source, digest) == 0);
  ops = bk7258_ota_http_source_ops();
  ret = ops->open(&source, &manifest);
  assert(ret == wanted);
  if (wanted == 0)
    {
      assert(manifest.security_counter == security_counter);
      assert(bk7258_ota_http_source_expect_catalog(&source, digest) == -EBUSY);
    }
  ops->close(&source);
}

static void assert_legacy_open(const char *url, uint32_t security_counter)
{
  struct bk7258_ota_http_source_s source = {0};
  struct bk7258_ota_manifest_s manifest;
  const struct bk7258_ota_source_ops_s *ops = bk7258_ota_http_source_ops();

  assert(bk7258_ota_http_source_initialize(&source, url, "") == 0);
  assert(ops->open(&source, &manifest) == 0);
  assert(manifest.security_counter == security_counter);
  ops->close(&source);
}

static void assert_tls_open(const char *url, const struct in_addr *address,
                            mbedtls_x509_crt *ca,
                            const uint8_t digest[32],
                            uint32_t security_counter, int wanted)
{
  struct bk7258_ota_http_source_s source = {0};
  struct bk7258_ota_manifest_s manifest;
  const struct bk7258_ota_source_ops_s *ops = bk7258_ota_http_source_ops();

  assert(bk7258_ota_http_source_initialize_with_server_ca(
           &source, url, address, ca) == 0);
  assert(bk7258_ota_http_source_expect_catalog(&source, digest) == 0);
  assert(ops->open(&source, &manifest) == wanted);
  if (wanted == 0)
    {
      assert(manifest.security_counter == security_counter);
    }
  ops->close(&source);
}

int main(int argc, char **argv)
{
  struct bk7258_ota_http_source_s source = {0};
  struct in_addr address;
  mbedtls_x509_crt ca;
  mbedtls_x509_crt bad_ca;
  uint8_t digest[32];
  uint8_t mismatch[32];
  bool tls;
  char *end;
  unsigned long parsed_counter;
  uint32_t security_counter;
  char url[160];

  tls = argc == 7;
  assert(argc == 5 || tls);
  decode_digest(argv[3], digest);
  errno = 0;
  parsed_counter = strtoul(argv[4], &end, 10);
  assert(errno == 0 && *argv[4] != '\0' && *end == '\0' &&
         parsed_counter > 0u && parsed_counter <= UINT32_MAX);
  security_counter = (uint32_t)parsed_counter;
  memcpy(mismatch, digest, sizeof(mismatch));
  mismatch[0] ^= 1u;
  assert(inet_pton(AF_INET, "127.0.0.1", &address) == 1);
  mbedtls_x509_crt_init(&ca);
  mbedtls_x509_crt_init(&bad_ca);
  assert(mbedtls_x509_crt_parse_file(&ca, argv[2]) == 0);

  assert(bk7258_ota_http_source_expect_catalog(NULL, digest) == -EINVAL);
  assert(bk7258_ota_http_source_expect_catalog(&source, digest) == -EINVAL);
  assert(bk7258_ota_http_source_initialize_with_server_ca(
           &source, "http://shaniu-update.local/catalog.json", &address,
           &ca) == -EPROTONOSUPPORT);
  assert(bk7258_ota_http_source_initialize_with_server_ca(
           &source, "https://shaniu-update.local/catalog.json", NULL,
           &ca) == -EKEYREJECTED);
  assert(bk7258_ota_http_source_initialize_with_credentials(
           &source, "https://shaniu-update.local/catalog.json", &address,
           &ca, NULL, NULL) == -EINVAL);
  assert(bk7258_ota_http_source_initialize_with_server_ca(
           &source, "https://shaniu-update.local/catalog.json", &address,
           &ca) == 0);
  assert(bk7258_ota_http_source_expect_catalog(&source, NULL) == -EINVAL);
  bk7258_ota_http_source_ops()->close(&source);

  if (tls)
    {
      assert(strcmp(argv[5], "tls") == 0);
      assert(mbedtls_x509_crt_parse_file(&bad_ca, argv[6]) == 0);
      assert(snprintf(url, sizeof(url),
                      "https://shaniu-update.local:%s/valid/catalog.json",
                      argv[1]) > 0);
      assert_tls_open(url, &address, &ca, digest, security_counter, 0);
      assert_tls_open(url, &address, &bad_ca, digest, security_counter,
                      -EKEYREJECTED);
      assert(snprintf(url, sizeof(url),
                      "https://wrong-update.local:%s/valid/catalog.json",
                      argv[1]) > 0);
      assert_tls_open(url, &address, &ca, digest, security_counter,
                      -EKEYREJECTED);
      mbedtls_x509_crt_free(&bad_ca);
      mbedtls_x509_crt_free(&ca);
      return 0;
    }

  assert(snprintf(url, sizeof(url), "http://127.0.0.1:%s/valid/catalog.json",
                  argv[1]) > 0);
  assert_legacy_open(url, security_counter);
  assert_open(url, digest, security_counter, 0);
  assert_open(url, mismatch, security_counter, -EBADMSG);
  assert(snprintf(url, sizeof(url), "http://127.0.0.1:%s/badsig/catalog.json",
                  argv[1]) > 0);
  assert_open(url, digest, security_counter, -EKEYREJECTED);
  mbedtls_x509_crt_free(&bad_ca);
  mbedtls_x509_crt_free(&ca);
  return 0;
}
