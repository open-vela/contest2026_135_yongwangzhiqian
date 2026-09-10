/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_ota_store.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#define _POSIX_C_SOURCE 200809L
#include "bk7258_voice_ota_store.h"
#include "bk7258_provision_store.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <nuttx/mutex.h>

static int g_fsync_calls;
static int g_fail_fsync_call;
static int g_fail_rename;

/* This is a product-store integration test.  The test links the real
 * bkprov_store implementation and mbedTLS; its fault wrappers only inject
 * publication uncertainty at the POSIX boundary. */

int nxmutex_lock(mutex_t *mutex) { return -pthread_mutex_lock(mutex); }
int nxmutex_unlock(mutex_t *mutex) { return -pthread_mutex_unlock(mutex); }
int __real_fsync(int fd);
int __real_rename(const char *oldpath, const char *newpath);
int __wrap_fsync(int fd)
{
  g_fsync_calls++;
  if (g_fail_fsync_call == g_fsync_calls) { errno = EIO; return -1; }
  return __real_fsync(fd);
}
int __wrap_rename(const char *oldpath, const char *newpath)
{
  if (g_fail_rename) { errno = EIO; return -1; }
  return __real_rename(oldpath, newpath);
}

static void reset_faults(void)
{
  g_fsync_calls = 0; g_fail_fsync_call = 0; g_fail_rename = 0;
}

static struct bkvoice_ota_intent_s intent(uint8_t digest_byte,
                                          enum bkvoice_ota_state_e state)
{
  struct bkvoice_ota_intent_s value;
  memset(&value, 0, sizeof(value));
  value.state = state;
  memset(value.manifest_sha256, digest_byte, sizeof(value.manifest_sha256));
  value.source_version.major = 18;
  value.source_version.minor = 6;
  value.source_version.revision = 389;
  value.source_version.build = 449;
  value.source_security_counter = 449;
  if (state != BKVOICE_OTA_DOWNLOADING)
    {
      value.target_version.major = 18;
      value.target_version.minor = 6;
      value.target_version.revision = 390;
      value.target_version.build = 450;
      value.target_security_counter = 450;
    }
  value.source_boot_generation = 7;
  return value;
}

static void assert_invalid(const struct bkvoice_ota_intent_s *value)
{
  assert(bkvoice_ota_store_commit(value) == -EINVAL);
}

int main(int argc, char **argv)
{
  struct bkvoice_ota_intent_s first = intent(0xa5, BKVOICE_OTA_DOWNLOADING);
  struct bkvoice_ota_intent_s next = intent(0xa5, BKVOICE_OTA_STAGED);
  struct bkvoice_ota_intent_s admitted;
  struct bkvoice_ota_intent_s other = intent(0xb6, BKVOICE_OTA_DOWNLOADING);
  struct bkvoice_ota_intent_s loaded;
  uint64_t revision = UINT64_MAX;
  char active[256];
  uint8_t wire[68];
  static const uint8_t expected_source[] =
    {18, 6, 0x01, 0x85, 0x00, 0x00, 0x01, 0xc1,
     0x00, 0x00, 0x01, 0xc1};
  static const uint8_t expected_empty_target[] =
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  static const uint8_t expected_boot_generation[] = {0, 0, 0, 7};
  uint8_t corrupt = 0xff;
  int fd;

  assert(argc == 2);
  assert(bkvoice_ota_store_start(NULL) == -EINVAL);
  assert(bkvoice_ota_store_start("relative") == -EINVAL);
  assert(bkvoice_ota_store_load(&loaded, &revision) == -ENODEV);
  assert(bkvoice_ota_store_commit(&first) == -ENODEV);
  assert(bkvoice_ota_store_start(argv[1]) == 0);
  assert(bkvoice_ota_store_start(argv[1]) == -EALREADY);
  assert(bkvoice_ota_store_load(&loaded, &revision) == -ENOENT && revision == 0);

  other = first; memset(other.manifest_sha256, 0, sizeof(other.manifest_sha256));
  assert_invalid(&other);
  other = first; memset(other.manifest_sha256, 0xff, sizeof(other.manifest_sha256));
  assert_invalid(&other);
  other = first; other.source_boot_generation = 0; assert_invalid(&other);
  other = first; other.source_boot_generation = UINT32_MAX; assert_invalid(&other);
  other = first; memset(&other.source_version, 0, sizeof(other.source_version));
  assert_invalid(&other);
  other = first; other.source_version.major = 0xff; assert_invalid(&other);
  other = first; other.source_version.minor = 0xff; assert_invalid(&other);
  other = first; other.source_version.revision = UINT16_MAX; assert_invalid(&other);
  other = first; other.source_version.build = UINT32_MAX; assert_invalid(&other);
  other = next; memset(&other.target_version, 0, sizeof(other.target_version));
  assert_invalid(&other);
  other = next; other.target_security_counter = other.source_security_counter;
  assert_invalid(&other);
  other = next; other.target_version = other.source_version;
  assert_invalid(&other);
  other = first; other.state = 0; assert_invalid(&other);
  other = first; other.state = (enum bkvoice_ota_state_e)5; assert_invalid(&other);
  assert(bkvoice_ota_store_load(&loaded, &revision) == -ENOENT && revision == 0);

  assert(bkvoice_ota_store_commit(&first) == 0);
  assert(snprintf(active, sizeof(active), "%s/config.bin", argv[1]) > 0);
  fd = open(active, O_RDONLY);
  assert(fd >= 0);
  assert(pread(fd, wire, sizeof(wire), 64) == (ssize_t)sizeof(wire));
  assert(close(fd) == 0);
  assert(!memcmp(wire, "BVO2", 4));
  assert(wire[4] == BKVOICE_OTA_DOWNLOADING && !wire[5] && !wire[6] && !wire[7]);
  assert(!memcmp(wire + 8, first.manifest_sha256, 32));
  assert(!memcmp(wire + 40, expected_source, sizeof(expected_source)));
  assert(!memcmp(wire + 52, expected_empty_target, sizeof(expected_empty_target)));
  assert(!memcmp(wire + 64, expected_boot_generation,
                 sizeof(expected_boot_generation)));
  assert(bkvoice_ota_store_load(&loaded, &revision) == 0 && revision == 1);
  assert(loaded.state == BKVOICE_OTA_DOWNLOADING);
  assert(!memcmp(loaded.manifest_sha256, first.manifest_sha256, 32));
  assert(bkvoice_ota_store_reload() == 0);
  assert(bkvoice_ota_store_load(&loaded, &revision) == 0 && revision == 1);

  /* Same state/record does not publish a second revision. */
  assert(bkvoice_ota_store_commit(&first) == 0);
  assert(bkvoice_ota_store_load(&loaded, &revision) == 0 && revision == 1);
  other = intent(0xb6, BKVOICE_OTA_DOWNLOADING);
  assert(bkvoice_ota_store_commit(&other) == -EALREADY);

  /* Publish the authenticated target while the intent is still DOWNLOADING.
   * This record survives a reset before the CP commit/COMPLETE boundary. */

  admitted = first;
  admitted.target_version = next.target_version;
  admitted.target_security_counter = next.target_security_counter;
  assert(bkvoice_ota_store_commit(&admitted) == 0);
  assert(bkvoice_ota_store_load(&loaded, &revision) == 0 && revision == 2);
  assert(loaded.state == BKVOICE_OTA_DOWNLOADING);
  assert(bk7258_mcuboot_version_equal(&loaded.target_version,
                                      &next.target_version));
  assert(loaded.target_security_counter == next.target_security_counter);

  assert(bkvoice_ota_store_commit(&next) == 0);
  assert(bkvoice_ota_store_load(&loaded, &revision) == 0 && revision == 3);
  assert(bk7258_mcuboot_version_equal(&loaded.target_version,
                                      &next.target_version));
  assert(loaded.target_security_counter == next.target_security_counter);
  assert(bkvoice_ota_store_commit(&first) == -ESTALE);
  assert(bkvoice_ota_store_clear(other.manifest_sha256) == -ESTALE);

  /* Rename uncertainty invalidates RAM; reload reconciles the prior active. */
  reset_faults(); g_fail_rename = 1;
  assert(bkvoice_ota_store_clear(first.manifest_sha256) == -EINPROGRESS);
  reset_faults();
  assert(bkvoice_ota_store_reload() == 0);
  assert(bkvoice_ota_store_load(&loaded, &revision) == 0 && revision == 3);

  /* Directory fsync uncertainty may have published the tombstone. */
  reset_faults(); g_fail_fsync_call = 2;
  assert(bkvoice_ota_store_clear(first.manifest_sha256) == -EINPROGRESS);
  reset_faults();
  assert(bkvoice_ota_store_reload() == 0);
  assert(bkvoice_ota_store_load(&loaded, &revision) == -ENOENT && revision == 4);
  assert(bkvoice_ota_store_clear(first.manifest_sha256) == -ENOENT);

  /* Malformed active content never becomes a fresh first boot. */
  assert(bkvoice_ota_store_commit(&first) == 0);
  fd = open(active, O_WRONLY);
  assert(fd >= 0);
  assert(pwrite(fd, &corrupt, 1, 64 + 4) == 1);
  assert(close(fd) == 0);
  assert(bkvoice_ota_store_reload() == 0);
  assert(bkvoice_ota_store_load(&loaded, &revision) == -EBADMSG);
  assert(bkvoice_ota_store_commit(&next) == -EBADMSG);
  assert(unlink(active) == 0);
  assert(rmdir(argv[1]) == 0);

  puts("BKVOICE_OTA_STORE_PRODUCT_INTEGRATION_PASS");
  return 0;
}
