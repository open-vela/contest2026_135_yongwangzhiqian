/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_vision_record.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fault;
ssize_t __real_write(int fd, const void *p, size_t size);
ssize_t __wrap_write(int fd, const void *p, size_t size)
{
  if (fault == 1)
    {
      fault = 0;
      errno = EINTR;
      return -1;
    }
  if (fault == 2)
    {
      errno = ENOSPC;
      return -1;
    }
  if (fault == 3)
    {
      return 0;
    }
  if (fault == 4)
    {
      ssize_t written = __real_write(fd, p, size > 17 ? 17 : size);
      fault = 2;
      return written;
    }
  /* Exercise short writes on every normal call. */
  ssize_t written = __real_write(fd, p, size > 17 ? 17 : size);
  return written;
}

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
         (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint8_t *read_image(int fd, size_t *length)
{
  off_t end = lseek(fd, 0, SEEK_END);
  uint8_t *image;

  assert(end > 0 && (uint64_t)end <= SIZE_MAX);
  image = malloc((size_t)end);
  assert(image != NULL);
  assert(pread(fd, image, (size_t)end, 0) == end);
  *length = (size_t)end;
  return image;
}

int main(int argc, char **argv)
{
  struct bkvision_record_s r;
  uint8_t odd[] = {0xff, 0xd8, 0, 0xff, 0xd9};
  uint8_t bytes[256];
  char path[] = "/tmp/bkvision-avi-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  unlink(path);
  assert(bkvision_record_begin(&r, fd, 0, 480) == -EINVAL);
  fault = 1;
  assert(bkvision_record_begin(&r, fd, 640, 480) == 0);
  assert(bkvision_record_finish(&r, 1000) == -ENODATA);
  assert(bkvision_record_frame(&r, odd, sizeof(odd)) == 0);
  assert(bkvision_record_frame(&r, odd, sizeof(odd)) == 0);
  assert(bkvision_record_finish(&r, 1000) == 0);
  struct bkvision_record_s inspected;
  assert(bkvision_record_inspect(&inspected, fd) == 0);
  assert(inspected.frames == 2 && inspected.width == 640 &&
         inspected.height == 480 && inspected.bytes == 28);
  assert(pread(fd, bytes, sizeof(bytes), 0) == 252);
  uint32_t full_hash = 2166136261u;
  for (size_t i = 0; i < 252; i++)
    {
      full_hash = (full_hash ^ bytes[i]) * 16777619u;
    }
  assert(inspected.payload_hash == full_hash);
  assert(memcmp(bytes, "RIFF", 4) == 0 && get32(bytes + 4) == 244);
  assert(get32(bytes + 48) == 2 && get32(bytes + 140) == 2);
  assert(get32(bytes + 128) == 1000 && get32(bytes + 132) == 2000);
  assert(memcmp(bytes + 224, "00dc", 4) == 0);
  assert(get32(bytes + 228) == 5 && bytes[237] == 0);
  assert(memcmp(bytes + 238, "00dc", 4) == 0);
  bytes[234] ^= 1;
  assert(pwrite(fd, bytes + 234, 1, 234) == 1);
  assert(ftruncate(fd, 251) == 0);
  assert(bkvision_record_inspect(&inspected, fd) == -EBADMSG);
  assert(bkvision_record_frame(&r, "bad", 3) == -EINVAL);
  fault = 2;
  assert(bkvision_record_frame(&r, odd, sizeof(odd)) == -ENOSPC);
  assert(r.frames == 2);
  fault = 3;
  assert(bkvision_record_frame(&r, odd, sizeof(odd)) == -EIO);
  fault = 0;
  r.bytes = 64u * 1024u * 1024u;
  assert(bkvision_record_frame(&r, odd, sizeof(odd)) == -EFBIG);

  /* A fresh ordinary recording still produces the same AVI. */
  assert(ftruncate(fd, 0) == 0);
  assert(bkvision_record_begin(&r, fd, 640, 480) == 0);
  assert(bkvision_record_frame(&r, odd, sizeof(odd)) == 0);
  assert(bkvision_record_frame(&r, odd, sizeof(odd)) == 0);
  assert(bkvision_record_finish(&r, 1000) == 0);
  assert(bkvision_record_inspect(&inspected, fd) == 0);
  assert(inspected.frames == 2 && inspected.bytes == 28);
  assert(pread(fd, bytes, sizeof(bytes), 0) == 252);
  assert(get32(bytes + 48) == 2 && get32(bytes + 140) == 2);
  assert(memcmp(bytes + 232, odd, sizeof(odd)) == 0 && bytes[237] == 0);
  assert(memcmp(bytes + 246, odd, sizeof(odd)) == 0 && bytes[251] == 0);

  /* Buffered and unbuffered streams remain byte-identical across several
   * flush boundaries, including odd/even JPEG sizes. The caller can reuse
   * and alter its source as soon as record_frame returns. */
  {
    struct bkvision_record_s plain;
    struct bkvision_record_s buffered;
    uint8_t frame_odd[] = {0xff, 0xd8, 0x11, 0xff, 0xd9};
    uint8_t frame_even[] = {0xff, 0xd8, 0x22, 0x33, 0xff, 0xd9};
    uint8_t write_buffer[512];
    uint8_t *plain_image;
    uint8_t *buffered_image;
    size_t plain_length;
    size_t buffered_length;
    char plain_path[] = "/tmp/bkvision-plain-XXXXXX";
    char buffered_path[] = "/tmp/bkvision-buffered-XXXXXX";
    int plain_fd = mkstemp(plain_path);
    int buffered_fd = mkstemp(buffered_path);

    assert(plain_fd >= 0 && buffered_fd >= 0);
    unlink(plain_path);
    unlink(buffered_path);
    assert(bkvision_record_begin(&plain, plain_fd, 640, 480) == 0);
    assert(bkvision_record_begin(&buffered, buffered_fd, 640, 480) == 0);
    assert(bkvision_record_set_buffer(&buffered, write_buffer,
                                      sizeof(write_buffer)) == 0);
    for (int i = 0; i < 128; i++)
      {
        uint8_t *frame = (i & 1) ? frame_even : frame_odd;
        size_t size = (i & 1) ? sizeof(frame_even) : sizeof(frame_odd);

        frame[2] = (uint8_t)i;
        assert(bkvision_record_frame(&plain, frame, size) == 0);
        frame[2] ^= 0x40;
        frame[2] = (uint8_t)i;
        assert(bkvision_record_frame(&buffered, frame, size) == 0);
        frame[2] ^= 0x40;
      }
    assert(bkvision_record_finish(&plain, 1000) == 0);
    assert(bkvision_record_finish(&buffered, 1000) == 0);
    plain_image = read_image(plain_fd, &plain_length);
    buffered_image = read_image(buffered_fd, &buffered_length);
    assert(plain_length == buffered_length);
    assert(memcmp(plain_image, buffered_image, plain_length) == 0);
    free(plain_image);
    free(buffered_image);
    close(plain_fd);
    close(buffered_fd);
  }

  /* A partial tail is emitted only by finish. */
  {
    uint8_t tail_buffer[512];
    uint8_t tail_frame[] = {0xff, 0xd8, 0x44, 0x55, 0xff, 0xd9};
    char tail_path[] = "/tmp/bkvision-tail-XXXXXX";
    int tail_fd = mkstemp(tail_path);

    assert(tail_fd >= 0);
    unlink(tail_path);
    assert(bkvision_record_begin(&r, tail_fd, 640, 480) == 0);
    assert(bkvision_record_set_buffer(&r, tail_buffer,
                                      sizeof(tail_buffer)) == 0);
    assert(bkvision_record_frame(&r, tail_frame, sizeof(tail_frame)) == 0);
    assert(bkvision_record_frame(&r, tail_frame, sizeof(tail_frame)) == 0);
    assert(lseek(tail_fd, 0, SEEK_END) == 224);
    assert(bkvision_record_finish(&r, 1000) == 0);
    assert(lseek(tail_fd, 0, SEEK_END) == 252);
    close(tail_fd);
  }

  /* A failed flush is latched: finish must return the same error without
   * replaying the buffered bytes. */
  for (int injected_fault = 2; injected_fault <= 3; injected_fault++)
    {
      uint8_t flush_buffer[512];
      uint8_t flush_frame[70] = {0xff, 0xd8};
      char flush_path[] = "/tmp/bkvision-flush-XXXXXX";
      int flush_fd = mkstemp(flush_path);

      flush_frame[68] = 0xff;
      flush_frame[69] = 0xd9;
      assert(flush_fd >= 0);
      unlink(flush_path);
      assert(bkvision_record_begin(&r, flush_fd, 640, 480) == 0);
      assert(bkvision_record_set_buffer(&r, flush_buffer,
                                        sizeof(flush_buffer)) == 0);
      for (int i = 0; i < 3; i++)
        {
          assert(bkvision_record_frame(&r, flush_frame,
                                       sizeof(flush_frame)) == 0);
        }
      fault = injected_fault;
      assert(bkvision_record_frame(&r, flush_frame,
                                   sizeof(flush_frame)) ==
             (injected_fault == 2 ? -ENOSPC : -EIO));
      fault = 0;
      assert(r.frames == 3);
      assert(r.write_error == (injected_fault == 2 ? -ENOSPC : -EIO));
      assert(lseek(flush_fd, 0, SEEK_END) == 224);
      assert(bkvision_record_finish(&r, 1000) ==
             (injected_fault == 2 ? -ENOSPC : -EIO));
      assert(lseek(flush_fd, 0, SEEK_END) == 224);
      close(flush_fd);
    }

  /* A short write followed by ENOSPC must leave the committed prefix in
   * place and finish must not replay it. */
  {
    uint8_t flush_buffer[512];
    uint8_t flush_frame[70] = {0xff, 0xd8};
    char flush_path[] = "/tmp/bkvision-short-enospc-XXXXXX";
    int flush_fd = mkstemp(flush_path);

    flush_frame[68] = 0xff;
    flush_frame[69] = 0xd9;
    assert(flush_fd >= 0);
    unlink(flush_path);
    assert(bkvision_record_begin(&r, flush_fd, 640, 480) == 0);
    assert(bkvision_record_set_buffer(&r, flush_buffer,
                                      sizeof(flush_buffer)) == 0);
    for (int i = 0; i < 3; i++)
      {
        assert(bkvision_record_frame(&r, flush_frame,
                                     sizeof(flush_frame)) == 0);
      }
    fault = 4;
    assert(bkvision_record_frame(&r, flush_frame,
                                 sizeof(flush_frame)) == -ENOSPC);
    assert(r.write_error == -ENOSPC && r.frames == 3);
    assert(lseek(flush_fd, 0, SEEK_END) == 241);
    fault = 0;
    assert(bkvision_record_finish(&r, 1000) == -ENOSPC);
    assert(lseek(flush_fd, 0, SEEK_END) == 241);
    close(flush_fd);
  }

  /* Capacity validation and the before-first-frame/one-time enable contract. */
  {
    uint8_t valid_buffer[512];
    char config_path[] = "/tmp/bkvision-config-XXXXXX";
    int config_fd = mkstemp(config_path);

    assert(config_fd >= 0);
    unlink(config_path);
    assert(bkvision_record_begin(&r, config_fd, 640, 480) == 0);
    assert(bkvision_record_set_buffer(&r, NULL, sizeof(valid_buffer)) ==
           -EINVAL);
    assert(bkvision_record_set_buffer(&r, valid_buffer, 256) == -EINVAL);
    assert(bkvision_record_set_buffer(&r, valid_buffer, 513) == -EINVAL);
    assert(bkvision_record_set_buffer(&r, valid_buffer,
                                      sizeof(valid_buffer)) == 0);
    assert(bkvision_record_set_buffer(&r, valid_buffer,
                                      sizeof(valid_buffer)) == -EINVAL);
    close(config_fd);

    assert(ftruncate(fd, 0) == 0);
    assert(bkvision_record_begin(&r, fd, 640, 480) == 0);
    assert(bkvision_record_frame(&r, odd, sizeof(odd)) == 0);
    assert(bkvision_record_set_buffer(&r, valid_buffer,
                                      sizeof(valid_buffer)) == -EINVAL);
  }
  close(fd);

  /* Optional real JPEG fixture -> independently probe/decode the output. */
  if (argc == 3)
    {
      FILE *f = fopen(argv[1], "rb");
      uint8_t *jpeg;
      long size;
      unsigned int i;
      assert(f != NULL);
      assert(fseek(f, 0, SEEK_END) == 0);
      size = ftell(f);
      assert(size > 0);
      rewind(f);
      jpeg = malloc(size);
      assert(jpeg != NULL && fread(jpeg, 1, size, f) == (size_t)size);
      fclose(f);
      fd = open(argv[2], O_CREAT | O_EXCL | O_RDWR, 0600);
      assert(fd >= 0);
      assert(bkvision_record_begin(&r, fd, 640, 480) == 0);
      for (i = 0; i < 30; i++)
        {
          assert(bkvision_record_frame(&r, jpeg, size) == 0);
        }
      assert(bkvision_record_finish(&r, 1000) == 0);
      close(fd);
      free(jpeg);
    }
  puts("BKVISION_RECORD_HOST_TEST_PASS");
  return 0;
}
