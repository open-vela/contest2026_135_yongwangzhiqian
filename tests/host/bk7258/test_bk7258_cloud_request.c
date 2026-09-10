/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_request.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int emit(void *context, const void *data, size_t size)
{
  size_t *total = context;
  assert(size <= 1024);
  *total += size;
  return fwrite(data, 1, size, stdout) == size ? 0 : -EIO;
}

static int cancel(void *context, const void *data, size_t size)
{
  unsigned int *calls = context;
  (void)data; (void)size;
  return ++*calls == 1 ? -ECANCELED : 0;
}

int main(int argc, char **argv)
{
  if (argc == 5)
    {
      assert(!strcmp(argv[1], "image"));
      size_t jpeg_size = strtoul(argv[2], NULL, 10);
      size_t chunk = strtoul(argv[4], NULL, 10);
      char prefix[256];
      const char suffix[] = "\"}}]}]}";
      struct bkcloud_image_source_s source;
      assert(jpeg_size >= 4 && jpeg_size <= BKCLOUD_JPEG_MAX);
      int length = snprintf(prefix, sizeof(prefix),
        "{\"model\":\"%s\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\"describe\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,",
        argv[3]);
      assert(length > 0 && (size_t)length < sizeof(prefix));
      uint8_t *jpeg = malloc(jpeg_size);
      assert(jpeg != NULL);
      memset(jpeg, 0x5a, jpeg_size);
      jpeg[0] = 0xff; jpeg[1] = 0xd8;
      jpeg[jpeg_size - 2] = 0xff; jpeg[jpeg_size - 1] = 0xd9;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == 0);
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, 3) == -EINVAL);
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, BKCLOUD_JPEG_MAX + 1) == -EINVAL);
      jpeg[0] = 0;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == -EINVAL);
      jpeg[0] = 0xff;
      jpeg[jpeg_size - 1] = 0;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == -EINVAL);
      jpeg[jpeg_size - 1] = 0xd9;
      assert(bkcloud_image_source_init(&source, prefix, (size_t)length, suffix,
             sizeof(suffix) - 1, jpeg, jpeg_size) == 0);
      size_t total = 0;
      char buffer[1024];
      while (source.offset < source.length)
        {
          size_t count = sizeof(buffer);
          const void *data;
          assert(bkcloud_image_body(buffer, &count, &data, chunk, &source) == 0);
          assert(count > 0 && count <= chunk);
          assert(emit(&total, data, count) == 0);
        }
      bkcloud_image_source_clear(&source);
      for (size_t i = 0; i < sizeof(source); i++)
        assert(((unsigned char *)&source)[i] == 0);
      free(jpeg);
      return 0;
    }
  assert(argc == 4);
  const char *model = argv[2];
  const char good[] = "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\","
    "\"message\":{\"content\":\"  reply\\n\"}}]}";
  const char *bad[] = {
    "{}", "{\"choices\":[]}",
    "{\"choices\":[{\"index\":0,\"finish_reason\":\"length\",\"message\":{\"content\":\"x\"}}]}",
    "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\",\"message\":{\"content\":\"x\\u0000y\"}}]}",
    "{\"choices\":[{\"index\":0,\"finish_reason\":\"stop\",\"message\":{\"content\":\"x\",\"tool_calls\":[{}]}}]}"
  };
  char output[32];
  assert(bkcloud_text_parse(good, strlen(good), output, sizeof(output)) == 0);
  assert(strcmp(output, "reply") == 0);
  assert(bkcloud_text_parse(good, strlen(good), output, 5) == -ENOSPC);
  assert(output[0] == 0);
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
      memset(output, 'x', sizeof(output));
      assert(bkcloud_text_parse(bad[i], strlen(bad[i]), output, sizeof(output)) < 0);
      for (size_t n = 0; n < sizeof(output); n++) assert(output[n] == 0);
    }
  for (size_t n = 0; n < strlen(good); n++)
    assert(bkcloud_text_parse(good, n, output, sizeof(output)) < 0);
  char trailing[sizeof(good) + 4];
  snprintf(trailing, sizeof(trailing), "%sx", good);
  assert(bkcloud_text_parse(trailing, strlen(trailing), output, sizeof(output)) < 0);
  size_t expected, total = 0;
  assert(bkcloud_asr_size(model, 0, &expected) < 0);
  assert(bkcloud_asr_size(model, 3, &expected) < 0);
  assert(bkcloud_asr_size(model, BKCLOUD_PCM_MAX + 2, &expected) < 0);
  assert(bkcloud_asr_size("bad\"model", 2, &expected) < 0);
  size_t size = strtoul(argv[1], NULL, 10);
  assert(bkcloud_asr_size(model, size, &expected) == 0);
  uint8_t *pcm = malloc(size);
  assert(pcm != NULL);
  for (size_t i = 0; i < size; i++) pcm[i] = i % 251;
  unsigned int calls = 0;
  assert(bkcloud_asr_write(model, pcm, size, cancel, &calls) == -ECANCELED);
  assert(calls == 1);
  size_t chunk = strtoul(argv[3], NULL, 10);
  if (chunk == 0)
    assert(bkcloud_asr_write(model, pcm, size, emit, &total) == 0);
  else
    {
      struct bkcloud_asr_source_s source;
      assert(bkcloud_asr_source_init(&source, model, pcm, size) == 0);
      char buffer[1024];
      while (source.offset < source.length)
        {
          size_t count = sizeof(buffer);
          const void *data;
          assert(bkcloud_asr_body(buffer, &count, &data, chunk, &source) == 0);
          assert(count > 0 && count <= chunk);
          assert(emit(&total, data, count) == 0);
        }
      size_t count = sizeof(buffer);
      const void *data;
      assert(bkcloud_asr_body(buffer, &count, &data, chunk, &source) == 0);
      assert(count == 0);
      bkcloud_asr_source_clear(&source);
      for (size_t i = 0; i < sizeof(source); i++)
        assert(((unsigned char *)&source)[i] == 0);
    }
  assert(total == expected);
  free(pcm);
  return 0;
}
