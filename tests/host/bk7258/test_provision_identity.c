/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_provision_identity.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static size_t read_bytes(const char *path, uint8_t *out)
{
  FILE *f = fopen(path, "rb"); assert(f);
  size_t n = fread(out, 1, 4096, f);
  assert(n > 0 && n < 4096 && !ferror(f)); assert(fclose(f) == 0); return n;
}
static size_t bundle(uint8_t *out, const uint8_t *cert, size_t cn, const uint8_t *key, size_t kn)
{
  memset(out, 0, 48); memcpy(out, "BPI1", 4); out[5] = 1;
  out[8] = cn >> 8; out[9] = cn; out[10] = kn >> 8; out[11] = kn;
  memset(out + 16, 42, 32); memcpy(out + 48, cert, cn); memcpy(out + 48 + cn, key, kn);
  return 48 + cn + kn;
}
int main(int argc, char **argv)
{
  uint8_t leaf[4096], ca[4096], key[4096], record[8192];
  struct bkprov_identity_s identity = {0}, empty = {0};
  assert(argc == 4);
  size_t cn = read_bytes(argv[1], leaf), kn = read_bytes(argv[2], key);
  size_t an = read_bytes(argv[3], ca);
  size_t n = bundle(record, leaf, cn, key, kn);
  assert(bkprov_identity_load(&identity, record, n) == 0);
  assert(identity.size == n && identity.certificate_size == cn && identity.key_size == kn);
  assert(!memcmp(identity.record, record, n));
  record[16] ^= 1; assert(identity.secret[0] == 42);
  assert(bkprov_identity_load(&identity, record, n) == -EBUSY);
  bkprov_identity_clear(&identity); assert(!memcmp(&identity, &empty, sizeof(empty)));
  assert(bkprov_identity_load(&identity, record, n - 1) < 0);
  memset(record + 16, 0, 32); assert(bkprov_identity_load(&identity, record, n) < 0);
  n = bundle(record, leaf, cn, key, kn);
  record[n - 1] ^= 1; assert(bkprov_identity_load(&identity, record, n) < 0);
  assert(!memcmp(&identity, &empty, sizeof(empty)));
  leaf[cn] = 0;
  n = bundle(record, leaf, cn + 1, key, kn);
  assert(bkprov_identity_load(&identity, record, n) < 0);
  assert(!memcmp(&identity, &empty, sizeof(empty)));
  n = bundle(record, ca, an, key, kn);
  assert(bkprov_identity_load(&identity, record, n) < 0);
  assert(!memcmp(&identity, &empty, sizeof(empty)));
  puts("BKPROV_IDENTITY_PASS: leaf/key validation, ownership and failed-load cleanup");
  return 0;
}
