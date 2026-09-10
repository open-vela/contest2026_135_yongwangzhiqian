/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_CLOUD_HISTORY_H
#define __APP_BK7258_CLOUD_HISTORY_H
#include "bk7258_cloud_request.h"
#define BKCLOUD_HISTORY_TURNS 3u
struct bkcloud_history_s
{
  size_t count;
  struct
  {
    char user[BKCLOUD_TEXT_MAX + 1];
    char assistant[BKCLOUD_TEXT_MAX + 1];
  } turns[BKCLOUD_HISTORY_TURNS];
};

#define BKCLOUD_HISTORY_BYTES (8u + BKCLOUD_HISTORY_TURNS * (4u + 2u * BKCLOUD_TEXT_MAX))
/* Portable bounded snapshot; no struct padding, pointers or size_t on disk.
 * Persona is authenticated by the enclosing memory AEAD. Decode publishes
 * only a complete matching-persona record; all failures clear destination.
 */
int bkcloud_history_encode(const struct bkcloud_history_s *history,
                           unsigned persona, void *bytes, size_t capacity,
                           size_t *used);
int bkcloud_history_decode(struct bkcloud_history_s *history,
                           unsigned persona, const void *bytes, size_t size);
#endif
