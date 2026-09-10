/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include <arch/chip/bk7258_active_image.h>
#include <arch/chip/bk7258_image_layout.h>

static struct bk7258_mcuboot_image_header_s *map_image(void)
{
  void *mapped = mmap((void *)(uintptr_t)BK7258_AP_FLASH_ADDR,
                      BK7258_AP_FLASH_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

  assert(mapped != MAP_FAILED);
  memset(mapped, 0xff, BK7258_AP_FLASH_SIZE);
  return mapped;
}

static void valid_header(struct bk7258_mcuboot_image_header_s *header)
{
  memset(header, 0, sizeof(*header));
  header->magic = BK7258_MCUBOOT_IMAGE_MAGIC;
  header->header_size = 0x200;
  header->image_size = 0x1000;
  header->version.major = 18;
  header->version.minor = 6;
  header->version.revision = 391;
  header->version.build = 451;
}

int main(void)
{
  struct bk7258_mcuboot_image_header_s *header = map_image();
  struct bk7258_mcuboot_version_s version = {0};

  assert(bk7258_active_ap_image_version(NULL) == -EINVAL);
  assert(bk7258_active_ap_image_version(&version) == -EILSEQ);

  valid_header(header);
  assert(bk7258_active_ap_image_version(&version) == 0);
  assert(version.major == 18 && version.minor == 6 &&
         version.revision == 391 && version.build == 451);

  header->magic = 0;
  assert(bk7258_active_ap_image_version(&version) == -EILSEQ);
  valid_header(header);
  header->header_size = BK7258_MCUBOOT_IMAGE_HEADER_SIZE - 1;
  assert(bk7258_active_ap_image_version(&version) == -EILSEQ);
  valid_header(header);
  header->header_size = UINT16_MAX;
  header->image_size = BK7258_AP_FLASH_SIZE - UINT16_MAX + 1u;
  assert(bk7258_active_ap_image_version(&version) == -EILSEQ);
  valid_header(header);
  header->image_size = BK7258_AP_FLASH_SIZE;
  assert(bk7258_active_ap_image_version(&version) == -EILSEQ);

  assert(munmap(header, BK7258_AP_FLASH_SIZE) == 0);
  puts("BK7258_ACTIVE_IMAGE_HOST_PASS");
  return 0;
}
