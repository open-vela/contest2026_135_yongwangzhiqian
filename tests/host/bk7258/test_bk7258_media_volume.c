/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_media_volume.h"
#include <arch/chip/bk7258_ota_catalog.h>
#include <arch/chip/bk7258_ota_source_file.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int acquire_error;
static int release_error;
static int leases;
static int mount_error;
static int unmount_error;
static int mount_calls;
static int unmount_calls;
static int mounted;
static int source_prepare_error;
static int source_prepare_calls;
static int source_release_calls;
int bk7258_usbmode_blockdev_acquire(void)
{
  if (acquire_error)
    {
      return acquire_error;
    }
  leases++;
  return 0;
}
int bk7258_usbmode_blockdev_release(void)
{
  if (release_error)
    {
      return release_error;
    }
  assert(leases == 1);
  leases--;
  return 0;
}
int mkdir(const char *path, mode_t mode)
{
  assert(mode == 0777);
  assert(!strcmp(path, "/mnt") || !strcmp(path, "/mnt/sdnand"));
  return 0;
}
int mount(const char *source, const char *target, const char *filesystemtype,
          unsigned long mountflags, const void *data)
{
  assert(leases == 1);
  assert(!strcmp(source, "/dev/mmcsd0"));
  assert(!strcmp(target, "/mnt/sdnand"));
  assert(!strcmp(filesystemtype, "vfat"));
  assert(mountflags == MS_RDONLY);
  assert(data == NULL);
  mount_calls++;
  if (mount_error)
    {
      errno = -mount_error;
      return -1;
    }
  mounted = 1;
  return 0;
}
int umount(const char *target)
{
  assert(!strcmp(target, "/mnt/sdnand"));
  assert(mounted);
  unmount_calls++;
  if (unmount_error)
    {
      errno = -unmount_error;
      return -1;
    }
  mounted = 0;
  return 0;
}
int bk7258_ota_catalog_verify(const uint8_t *catalog, size_t catalog_size,
                              const uint8_t *signature,
                              size_t signature_size,
                              struct bk7258_ota_catalog_s *result)
{
  (void)catalog;
  (void)catalog_size;
  (void)signature;
  (void)signature_size;
  (void)result;
  return -EPERM;
}
static int source_prepare(const char *root)
{
  assert(root != NULL);
  source_prepare_calls++;
  return source_prepare_error;
}
static int source_release(void)
{
  source_release_calls++;
  return 0;
}
static void write_small_file(const char *path)
{
  static const char data[] = "x";
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

  assert(fd >= 0);
  assert(write(fd, data, sizeof(data)) == (ssize_t)sizeof(data));
  assert(close(fd) == 0);
}
static void source_callback_lifecycle_test(void)
{
  const struct bk7258_ota_source_ops_s *ops;
  struct bk7258_ota_file_source_s source;
  struct bk7258_ota_manifest_s manifest;
  char root[] = "/tmp/bkota-file-source-XXXXXX";
  char catalog[sizeof(root) + 16u];
  char signature[sizeof(root) + 16u];

  assert(bk7258_ota_file_source_register(source_prepare, source_release) == 0);
  ops = bk7258_ota_file_source_ops();
  assert(ops != NULL);
  source_prepare_error = -EAGAIN;
  assert(bk7258_ota_file_source_initialize(&source, "/tmp/package") == 0);
  assert(ops->open(&source, &manifest) == -EAGAIN);
  assert(source_prepare_calls == 1 && source_release_calls == 1);
  ops->close(&source);
  assert(source_release_calls == 1);

  assert(mkdtemp(root) == root);
  snprintf(catalog, sizeof(catalog), "%s/catalog.json", root);
  snprintf(signature, sizeof(signature), "%s/catalog.sig", root);
  write_small_file(catalog);
  write_small_file(signature);
  source_prepare_error = 0;
  assert(bk7258_ota_file_source_initialize(&source, root) == 0);
  /* This is a lifecycle test: the catalog verifier is a fixed failing stub,
   * so it neither generates keys nor claims package signature coverage.
   */
  assert(ops->open(&source, &manifest) == -EPERM);
  assert(source_prepare_calls == 2 && source_release_calls == 2);
  ops->close(&source);
  assert(source_release_calls == 2);

  assert(bk7258_ota_file_source_initialize(&source, "/tmp/package") == 0);
  source.prepared = true;
  assert(ops->cancel(&source) == 0);
  assert(ops->checkpoint(&source, NULL) == -ECANCELED);
  ops->close(&source);
  ops->close(&source);
  assert(source_release_calls == 3);
  assert(unlink(catalog) == 0 && unlink(signature) == 0 && rmdir(root) == 0);
  assert(bk7258_ota_file_source_initialize(&source, root) == 0);
  assert(ops->open(&source, &manifest) == -ENOENT);
  assert(source_prepare_calls == 3 && source_release_calls == 4);
  ops->close(&source);
  assert(source_release_calls == 4);
}
int main(void)
{
  assert(bk7258_media_volume_acquire(0) == -EINVAL);
  acquire_error = -EBUSY;
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
  acquire_error = 0;
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == 0);
  assert(leases == 1);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == -EPERM);
  release_error = -EIO;
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_VISION) == -EIO);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
  release_error = 0;
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_VISION) == 0);
  assert(leases == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_DISPLAY) == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_PREFERENCES) == 0);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_DISPLAY) == -EBUSY);
  assert(bk7258_media_volume_acquire(BK7258_MEDIA_VOLUME_VISION) == -EBUSY);
  assert(bk7258_media_volume_release(BK7258_MEDIA_VOLUME_PREFERENCES) == 0);
  assert(leases == 0);
  assert(bk7258_media_volume_ota_prepare("/mnt/sdnand/../escape") ==
         -EINVAL);
  assert(bk7258_media_volume_ota_prepare("/tmp/shaniu-wifi420") == 0);
  assert(mount_calls == 0 && leases == 0);
  assert(bk7258_media_volume_ota_prepare("/mnt/sdnand/shaniu-wifi420") == 0);
  assert(mount_calls == 1 && mounted && leases == 1);
  unmount_error = -EIO;
  assert(bk7258_media_volume_ota_release() == -EIO);
  assert(unmount_calls == 1 && mounted && leases == 1);
  unmount_error = 0;
  assert(bk7258_media_volume_ota_release() == 0);
  assert(unmount_calls == 2 && !mounted && leases == 0);
  mount_error = -EBUSY;
  assert(bk7258_media_volume_ota_prepare("/mnt/sdnand/shaniu-wifi420") ==
         -EBUSY);
  assert(!mounted && leases == 0);
  mount_error = 0;
  source_callback_lifecycle_test();
  puts("BKVISION_VOLUME_HOST_TEST_PASS");
  return 0;
}
