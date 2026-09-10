/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "bk7258_cloud_memory.h"
#include "bk7258_cloud_history.h"
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static unsigned random_calls;
static uint8_t repeated_key[32];
static int repeat_key(void *context, unsigned char *output, size_t size)
{ (void)context; assert(size == 32); memcpy(output, repeated_key, size); return 0; }
static bool fail_random;
static int fail_sync;
int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
  if (fail_sync && --fail_sync == 0) { errno = EIO; return -1; }
  return __real_fsync(fd);
}
static int random_bytes(void *context, unsigned char *output, size_t size)
{
  (void)context;
  random_calls++;
  if (fail_random) return -1;
  for (size_t i = 0; i < size; i++) output[i] = (uint8_t)(random_calls + i);
  return 0;
}
int main(int argc, char **argv)
{
  assert(argc == 2);
  struct bkcloud_history_s *history = calloc(1, sizeof(*history));
  struct bkcloud_history_s *loaded = calloc(1, sizeof(*loaded));
  uint8_t *snapshot_bytes = malloc(BKCLOUD_HISTORY_BYTES);
  assert(history && loaded && snapshot_bytes);
  size_t snapshot_size;
  history->count = BKCLOUD_HISTORY_TURNS;
  for (size_t i = 0; i < history->count; i++)
    {
      memset(history->turns[i].user, 'u', BKCLOUD_TEXT_MAX);
      memset(history->turns[i].assistant, 'a', BKCLOUD_TEXT_MAX);
    }
  assert(bkcloud_history_encode(history, 2, snapshot_bytes, BKCLOUD_HISTORY_BYTES, &snapshot_size) == 0);
  assert(snapshot_size == BKCLOUD_HISTORY_BYTES);
  assert(bkcloud_history_decode(loaded, 2, snapshot_bytes, snapshot_size) == 0);
  assert(!memcmp(history, loaded, sizeof(*history)));
  assert(bkcloud_history_decode(loaded, 1, snapshot_bytes, snapshot_size) == -EBADMSG);
  assert(loaded->count == 0 && loaded->turns[0].user[0] == 0);
  /* Every truncation boundary, including length/header fields, is rejected. */
  for (size_t i = 0; i < snapshot_size; i++)
    assert(bkcloud_history_decode(loaded, 2, snapshot_bytes, i) == -EBADMSG);
  snapshot_bytes[10] = 0;
  assert(bkcloud_history_decode(loaded, 2, snapshot_bytes, snapshot_size) == -EBADMSG);
  memset(history, 0, sizeof(*history));
  assert(bkcloud_history_encode(history, 0, snapshot_bytes, BKCLOUD_HISTORY_BYTES, &snapshot_size) == 0);
  assert(snapshot_size == 8 && bkcloud_history_decode(loaded, 0, snapshot_bytes, snapshot_size) == 0);
  free(snapshot_bytes); free(history); free(loaded);
  struct bkcloud_memory_policy_s policy, next;
  uint8_t owner[32] = {42}, other[32] = {43};
  uint8_t plain[96], envelope[148], modified[148], restored[96];
  size_t used, decoded;
  memset(plain, 37, sizeof(plain));
  assert(bkcloud_memory_policy_load(argv[1], owner, &policy) == 0);
  assert(!policy.enabled && policy.revision == 0 && random_calls == 0);
  assert(bkcloud_memory_seal(&policy, plain, sizeof(plain), envelope,
                             sizeof(envelope), &used, random_bytes, NULL) == -EACCES);
  assert(bkcloud_memory_policy_set(argv[1], owner, false, false, random_bytes, NULL) == 0);
  assert(access(argv[1], F_OK) < 0); /* Disabled default performs no write. */
  fail_random = true;
  assert(bkcloud_memory_policy_set(argv[1], owner, true, false, random_bytes, NULL) == -EIO);
  assert(access(argv[1], F_OK) < 0);
  fail_random = false;
  assert(bkcloud_memory_policy_set(argv[1], owner, true, false, random_bytes, NULL) == 0);
  assert(bkcloud_memory_policy_load(argv[1], owner, &policy) == 0 && policy.enabled);
  assert(policy.revision == 1);
  assert(bkcloud_memory_seal(&policy, plain, sizeof(plain), envelope,
                             sizeof(envelope), &used, random_bytes, NULL) == 0);
  assert(used == sizeof(envelope) && memcmp(envelope + 52, plain, sizeof(plain)));
  assert(bkcloud_memory_open(&policy, envelope, used, restored, sizeof(restored), &decoded) == 0);
  assert(decoded == sizeof(plain) && !memcmp(restored, plain, decoded));
  for (size_t i = 0; i < sizeof(envelope); i++)
    {
      memcpy(modified, envelope, sizeof(envelope)); modified[i] ^= 1;
      memset(restored, 93, sizeof(restored));
      assert(bkcloud_memory_open(&policy, modified, sizeof(modified), restored,
                                 sizeof(restored), &decoded) < 0 && decoded == 0);
      for (size_t j = 0; j < sizeof(restored); j++) assert(restored[j] == 0);
    }
  assert(bkcloud_memory_seal(&policy, plain, sizeof(plain), modified,
                             sizeof(modified), &used, random_bytes, NULL) == 0);
  assert(memcmp(modified, envelope, sizeof(envelope))); /* New nonce. */
  char sdroot[256], sdfile[280];
  snprintf(sdroot, sizeof(sdroot), "%s/sd-test", argv[1]);
  snprintf(sdfile, sizeof(sdfile), "%s/history.enc", sdroot);
  memset(restored, 93, sizeof(restored));
  assert(bkcloud_memory_restore(sdroot, &policy, restored, sizeof(restored), &decoded) == -ENOENT);
  for (size_t i = 0; i < sizeof(restored); i++) assert(restored[i] == 0);
  assert(bkcloud_memory_save(sdroot, &policy, plain, sizeof(plain), random_bytes, NULL) == 0);
  assert(bkcloud_memory_restore(sdroot, &policy, restored, sizeof(restored), &decoded) == 0);
  assert(decoded == sizeof(plain) && !memcmp(restored, plain, decoded));
  int snapshot = open(sdfile, O_RDONLY); assert(snapshot >= 0);
  assert(read(snapshot, modified, sizeof(modified)) == sizeof(modified));
  assert(close(snapshot) == 0 && !memcmp(modified, "SMM1", 4));
  assert(memcmp(modified + 52, plain, sizeof(plain)));
  uint8_t newer[96]; memset(newer, 71, sizeof(newer));
  fail_sync = 1;
  assert(bkcloud_memory_save(sdroot, &policy, newer, sizeof(newer), random_bytes, NULL) == -EIO);
  assert(bkcloud_memory_restore(sdroot, &policy, restored, sizeof(restored), &decoded) == 0);
  assert(!memcmp(restored, plain, decoded)); /* Failed before rename: old file intact. */
  fail_sync = 2;
  assert(bkcloud_memory_save(sdroot, &policy, newer, sizeof(newer), random_bytes, NULL) == -EINPROGRESS);
  assert(bkcloud_memory_restore(sdroot, &policy, restored, sizeof(restored), &decoded) == 0);
  assert(!memcmp(restored, newer, decoded)); /* Published but durability uncertain. */
  snapshot = open(sdfile, O_WRONLY | O_TRUNC); assert(snapshot >= 0);
  assert(write(snapshot, modified, 20) == 20 && close(snapshot) == 0);
  memset(restored, 93, sizeof(restored));
  assert(bkcloud_memory_restore(sdroot, &policy, restored, sizeof(restored), &decoded) == -EBADMSG);
  for (size_t i = 0; i < sizeof(restored); i++) assert(restored[i] == 0);
  assert(unlink(sdfile) == 0);
  assert(symlink("/dev/null", sdfile) == 0);
  assert(bkcloud_memory_restore(sdroot, &policy, restored, sizeof(restored), &decoded) < 0);
  assert(unlink(sdfile) == 0 && rmdir(sdroot) == 0);
  fail_sync = 1;
  assert(bkcloud_memory_policy_set(argv[1], owner, false, false, random_bytes, NULL) == -EIO);
  assert(bkcloud_memory_policy_load(argv[1], owner, &next) == 0 && next.enabled);
  assert(next.revision == 1);
  fail_sync = 2;
  assert(bkcloud_memory_policy_set(argv[1], owner, false, false, random_bytes, NULL) == -EINPROGRESS);
  assert(bkcloud_memory_policy_load(argv[1], owner, &next) == 0 && !next.enabled);
  assert(next.revision == 2); /* Reconcile actual published state. */
  assert(bkcloud_memory_policy_set(argv[1], owner, true, false, random_bytes, NULL) == 0);
  assert(bkcloud_memory_policy_load(argv[1], owner, &next) == 0 && next.enabled);
  assert(!memcmp(policy.key, next.key, 32)); /* Disable does not mean delete. */
  memcpy(repeated_key, policy.key, 32);
  assert(bkcloud_memory_policy_set(argv[1], owner, true, true, repeat_key, NULL) == -EIO);
  assert(bkcloud_memory_policy_load(argv[1], owner, &next) == 0 && !memcmp(next.key, policy.key, 32));
  assert(bkcloud_memory_policy_set(argv[1], owner, true, true, random_bytes, NULL) == 0);
  assert(bkcloud_memory_policy_load(argv[1], owner, &next) == 0 && next.enabled);
  assert(memcmp(policy.key, next.key, 32));
  assert(bkcloud_memory_open(&next, envelope, sizeof(envelope), restored,
                             sizeof(restored), &decoded) < 0); /* Old SD copy stays deleted. */
  assert(bkcloud_memory_policy_load(argv[1], other, &next) == 0 && !next.enabled);
  for (size_t i = 0; i < sizeof(next.key); i++) assert(next.key[i] == 0);
  assert(bkcloud_memory_policy_set(argv[1], other, true, false, random_bytes, NULL) == 0);
  assert(bkcloud_memory_policy_load(argv[1], owner, &policy) == 0 && !policy.enabled);
  char path[256];
  snprintf(path, sizeof(path), "%s/config.bin", argv[1]);
  int fd = open(path, O_RDWR); assert(fd >= 0);
  uint8_t changed = 0;
  assert(pread(fd, &changed, 1, 64) == 1); changed ^= 1;
  assert(pwrite(fd, &changed, 1, 64) == 1 && close(fd) == 0);
  memset(&next, 93, sizeof(next));
  assert(bkcloud_memory_policy_load(argv[1], other, &next) == -EBADMSG);
  assert(!next.enabled && next.revision == 0);
  for (size_t i = 0; i < sizeof(next.key); i++) assert(next.key[i] == 0);
  puts("PASS: private memory policy, owner isolation, AEAD tamper rejection, key rotation, failed-write reconciliation");
  return 0;
}
