/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "bk7258_vision_core.h"

static struct bkvision_rpc_request_s request(void)
{
  struct bkvision_rpc_request_s r;
  memset(&r, 0, sizeof(r));
  r.magic = BKVISION_RPC_MAGIC;
  r.version = BKVISION_RPC_VERSION;
  r.command = BKVISION_RPC_SNAPSHOT;
  r.session_id = 2;
  r.sequence = 3;
  return r;
}

int main(void)
{
  struct bkvision_rpc_request_s q = request();
  struct bkvision_rpc_response_s r;
  const uint8_t jpeg[] = {0xff, 0xd8, 0x11, 0x22, 0xff, 0xd9};
  const uint8_t no_soi[] = {0x00, 0x01, 0x11, 0x22, 0xff, 0xd9};
  const uint8_t no_eoi[] = {0xff, 0xd8, 0x11, 0x22, 0x00, 0x01};
  uint8_t copy[sizeof(jpeg)] = {0};
  size_t copied = 99;
  assert(bkvision_rpc_request_valid(&q));
  bkvision_rpc_make_response(&r, &q, 0);
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), sizeof(jpeg),
                                     0, 640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                     8) == 0);
  assert(r.operation_status == 0);
  assert(r.flags == (BKVISION_FLAG_JPEG_SOI | BKVISION_FLAG_JPEG_EOI));
  assert(r.session_id == 2 && r.sequence == 3 && r.reserved[0] == 0);
  assert(bkvision_rpc_response_valid(&r));
  assert(bkvision_copy_jpeg_frame(&r, copy, sizeof(copy), &copied,
                                  jpeg, sizeof(jpeg), sizeof(jpeg), 0,
                                  640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                  9) == 0);
  assert(copied == sizeof(jpeg) && memcmp(copy, jpeg, sizeof(jpeg)) == 0);
  copied = 99;
  assert(bkvision_copy_jpeg_frame(&r, copy, sizeof(copy) - 1, &copied,
                                  jpeg, sizeof(jpeg), sizeof(jpeg), 0,
                                  640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                  9) == -ENOSPC);
  assert(copied == 0 && r.operation_status == -ENOSPC && r.bytes_used == 0);
  copied = 99;
  assert(bkvision_copy_jpeg_frame(&r, copy, sizeof(copy), &copied,
                                  no_eoi, sizeof(no_eoi), sizeof(no_eoi), 0,
                                  640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                  9) == -EPROTO);
  assert(copied == 0 && r.bytes_used == 0);
  copied = 99;
  assert(bkvision_copy_jpeg_frame(&r, NULL, sizeof(copy), &copied,
                                  jpeg, sizeof(jpeg), sizeof(jpeg), 0,
                                  640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                  9) == -EINVAL);
  assert(copied == 0);
  r.operation_status = 1;
  assert(!bkvision_rpc_response_valid(&r));
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), sizeof(jpeg),
                                     0, 640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                     8) == 0);
  r.rpc_status = -EIO;
  assert(!bkvision_rpc_response_valid(&r));
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), sizeof(jpeg),
                                     0, 640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                     8) == 0);
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), 1, 0, 640, 480,
                                     BKVISION_PIXEL_FORMAT_JPEG, 8) == -EINVAL);
  assert(r.width == 0 && r.flags == 0);
  assert(bkvision_rpc_response_valid(&r));
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), sizeof(jpeg), 1,
                                     640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                     8) == -EIO);
  r.rpc_status = 0;
  assert(bkvision_rpc_response_valid(&r));
  assert(bkvision_rpc_validate_frame(&r, no_soi, sizeof(no_soi),
                                     sizeof(no_soi), 0, 640, 480,
                                     BKVISION_PIXEL_FORMAT_JPEG, 8) == -EPROTO);
  assert(r.width == 0 && r.flags == 0);
  assert(bkvision_rpc_validate_frame(&r, no_eoi, sizeof(no_eoi),
                                     sizeof(no_eoi), 0, 640, 480,
                                     BKVISION_PIXEL_FORMAT_JPEG, 8) == -EPROTO);
  assert(r.width == 0 && r.flags == 0);
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), sizeof(jpeg), 0,
                                     640, 480, 0, 8) == -EPROTONOSUPPORT);
  assert(r.width == 0 && r.bytes_used == 0);
  assert(bkvision_rpc_validate_frame(&r, NULL, sizeof(jpeg), sizeof(jpeg), 0,
                                     640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                     8) == -EINVAL);
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), sizeof(jpeg) + 1,
                                     0, 640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                     8) == -EINVAL);
  q.magic = 0;
  assert(!bkvision_rpc_request_valid(&q));
  q = request();
  q.version = BKVISION_RPC_VERSION + 1;
  assert(!bkvision_rpc_request_valid(&q));
  q = request();
  q.command = BKVISION_RPC_RESPONSE;
  assert(!bkvision_rpc_request_valid(&q));
  q = request();
  q.session_id = 0;
  assert(!bkvision_rpc_request_valid(&q));
  q = request();
  q.sequence = 0;
  assert(!bkvision_rpc_request_valid(&q));
  q = request();
  q.reserved = 1;
  assert(!bkvision_rpc_request_valid(&q));
  q = request();
  q.command = BKVISION_RPC_RECORD;
  assert(!bkvision_rpc_request_valid(&q));
  q.duration_ms = 1000;
  assert(bkvision_rpc_request_valid(&q));
  q.duration_ms = 60001;
  assert(!bkvision_rpc_request_valid(&q));
  q.duration_ms = 60000;
  assert(bkvision_rpc_request_valid(&q));
  bkvision_rpc_make_response(&r, &q, 0);
  assert(bkvision_rpc_validate_frame(&r, jpeg, sizeof(jpeg), sizeof(jpeg),
                                    0, 640, 480, BKVISION_PIXEL_FORMAT_JPEG,
                                    8) == 0);
  r.flags |= BKVISION_FLAG_RECORDED;
  r.bytes_used = 10000;
  r.reserved[0] = 30;
  r.reserved[1] = 1000;
  assert(bkvision_rpc_response_valid(&r));
  r.reserved[0] = 1;
  assert(!bkvision_rpc_response_valid(&r));
  r.reserved[0] = 30;
  r.operation_status = -EIO;
  assert(!bkvision_rpc_response_valid(&r));
  q = request();
  q.command = BKVISION_RPC_CHECK_RECORD;
  q.duration_ms = 0x28013b3b;
  q.reserved = 2;
  assert(bkvision_rpc_request_valid(&q));
  bkvision_rpc_make_response(&r, &q, 0);
  r.flags = BKVISION_FLAG_CHECKED;
  r.width = 640;
  r.height = 480;
  r.pixel_format = BKVISION_PIXEL_FORMAT_JPEG;
  r.bytes_used = 893112;
  r.reserved[0] = 40;
  r.reserved[1] = 0; /* A full-file FNV hash can be zero. */
  assert(!bkvision_rpc_response_valid(&r)); /* ENODATA is not success. */
  r.operation_status = 0;
  assert(bkvision_rpc_response_valid(&r));
  r.capture_sequence = 1;
  assert(!bkvision_rpc_response_valid(&r));
  puts("BKVISION_CORE_HOST_TEST_PASS");
  return 0;
}
