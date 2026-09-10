/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_MEMORY_H
#define __APP_BK7258_CLOUD_MEMORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BKCLOUD_MEMORY_MAX 131072u
#define BKCLOUD_MEMORY_OVERHEAD 52u
struct bkcloud_memory_policy_s
{
  uint8_t key[32];
  uint64_t revision;
  bool enabled;
};
typedef int (*bkcloud_memory_random_t)(void *, unsigned char *, size_t);
/* Caller serializes all operations and wipes borrowed policy keys. Policy
 * owner is the current committed SCB3 owner key, not a caller-selected value.
 * root is private on-chip LittleFS, not removable media. Missing policy is
 * disabled; a changed owner cannot resume another owner's retained memory.
 */
int bkcloud_memory_policy_load(const char *root, const uint8_t owner[32],
                               struct bkcloud_memory_policy_s *policy);
/* Set is explicit user intent. rotate cryptographically deletes old SD copies.
 * EINPROGRESS means publication is uncertain: reload before further actions.
 * No plaintext or key is returned through a device control response.
 */
int bkcloud_memory_policy_set(const char *root, const uint8_t owner[32],
                              bool enabled, bool rotate,
                              bkcloud_memory_random_t random, void *context);
/* AEAD envelope for an SD payload; header, key generation and length are
 * authenticated. Fresh 96-bit random nonce per seal. Open wipes output on
 * authentication/validation error for a bounded destination. Input/output
 * buffers must be disjoint. These functions do no I/O.
 */
int bkcloud_memory_seal(const struct bkcloud_memory_policy_s *policy,
                        const void *plain, size_t size, void *sealed,
                        size_t capacity, size_t *used,
                        bkcloud_memory_random_t random, void *context);
int bkcloud_memory_open(const struct bkcloud_memory_policy_s *policy,
                        const void *sealed, size_t size, void *plain,
                        size_t capacity, size_t *used);
/* Encrypted snapshot I/O. Caller holds the shared media lease/mount through
 * bk7258_preferences_with_storage for the entire call. root is the mounted
 * memory directory, never the private policy directory. Save returns
 * EINPROGRESS after publication if durability cannot be established. FAT
 * power-loss recovery still requires physical validation. Read clears the
 * destination on any failure, including missing file; missing is ENOENT.
 */
int bkcloud_memory_save(const char *root,
                        const struct bkcloud_memory_policy_s *policy,
                        const void *plain, size_t size,
                        bkcloud_memory_random_t random, void *context);
int bkcloud_memory_restore(const char *root,
                           const struct bkcloud_memory_policy_s *policy,
                           void *plain, size_t capacity, size_t *used);
#endif
