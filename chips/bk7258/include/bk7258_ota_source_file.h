/****************************************************************************
 * chips/bk7258/include/bk7258_ota_source_file.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_FILE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_FILE_H

#include <nuttx/config.h>

#include <stdbool.h>

#include <arch/chip/bk7258_ota.h>
#include <arch/chip/bk7258_ota_catalog.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define BK7258_OTA_FILE_ROOT_SIZE 128u
#define BK7258_OTA_FILE_PATH_SIZE \
  (BK7258_OTA_FILE_ROOT_SIZE + BK7258_OTA_CATALOG_URI_SIZE)

struct bk7258_ota_file_source_s
{
  char root[BK7258_OTA_FILE_ROOT_SIZE];
  char path[2][BK7258_OTA_FILE_PATH_SIZE];
  int fd[2];
  volatile bool canceled;
  bool prepared;
  struct bk7258_ota_catalog_s catalog;
};

#ifdef CONFIG_BK7258_OTA_SOURCE_FILE
typedef int (*bk7258_ota_file_source_prepare_t)(const char *root);
typedef int (*bk7258_ota_file_source_release_t)(void);

/* A product registers one optional, process-lifetime filesystem pair during
 * AP startup.  The OTA manager serializes each source open through close;
 * release must also tolerate a failed prepare.
 */
int bk7258_ota_file_source_register(
  bk7258_ota_file_source_prepare_t prepare,
  bk7258_ota_file_source_release_t release);
int bk7258_ota_file_source_initialize(
  struct bk7258_ota_file_source_s *source, const char *root);
const struct bk7258_ota_source_ops_s *bk7258_ota_file_source_ops(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_OTA_SOURCE_FILE_H */
