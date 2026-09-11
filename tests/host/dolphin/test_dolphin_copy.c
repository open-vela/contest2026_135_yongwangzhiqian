/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DOLPHIN_HOST_TEST 1
#define DOLPHIN_COPY_TEST_DELAY_US 1000
#define CONFIG_DOLPHIN_STORAGE_ROOT "/tmp/dolphin-copy-sd"
#define CONFIG_DOLPHIN_USB_ROOT "/tmp/dolphin-copy-usb"
#include "../../../app/dolphin/dolphin_copy.c"

static void wait_for_state(enum dolphin_copy_state_e state)
{
  struct dolphin_copy_snapshot_s snapshot;

  for (int i = 0; i < 5000; i++)
    {
      assert(dolphin_copy_snapshot(&snapshot) == 0);
      if (snapshot.state == state)
        {
          return;
        }
      usleep(1000);
    }
  assert(!"copy did not finish");
}

static void write_file(const char *path, const void *data, size_t length)
{
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  assert(fd >= 0);
  assert(write(fd, data, length) == (ssize_t)length);
  assert(close(fd) == 0);
}

int main(void)
{
  static const char source[] = "dolphin copy source\n";
  static const char replacement[] = "old destination\n";
  struct dolphin_copy_snapshot_s snapshot;
  char copied[sizeof(source)];
  int fd;

  unlink("/tmp/dolphin-copy-sd/source.txt");
  unlink("/tmp/dolphin-copy-sd/large.bin");
  unlink("/tmp/dolphin-copy-sd/source-link");
  unlink("/tmp/dolphin-copy-usb/copy.txt");
  unlink("/tmp/dolphin-copy-usb/existing.txt");
  unlink("/tmp/dolphin-copy-usb/canceled.bin");
  unlink("/tmp/dolphin-copy-usb/destination-link");
  rmdir("/tmp/dolphin-copy-sd");
  rmdir("/tmp/dolphin-copy-usb");
  assert(mkdir("/tmp/dolphin-copy-sd", 0700) == 0);
  assert(mkdir("/tmp/dolphin-copy-usb", 0700) == 0);
  write_file("/tmp/dolphin-copy-sd/source.txt", source, sizeof(source));
  assert(dolphin_copy_start("/tmp/dolphin-copy-sd/source.txt",
                            "/tmp/dolphin-copy-usb/copy.txt") == 0);
  wait_for_state(DOLPHIN_COPY_SUCCEEDED);
  assert(dolphin_copy_snapshot(&snapshot) == 0);
  assert(snapshot.copied == sizeof(source) && snapshot.total == sizeof(source));
  fd = open("/tmp/dolphin-copy-usb/copy.txt", O_RDONLY);
  assert(fd >= 0 && read(fd, copied, sizeof(copied)) == (ssize_t)sizeof(copied));
  assert(close(fd) == 0 && memcmp(copied, source, sizeof(source)) == 0);

  write_file("/tmp/dolphin-copy-usb/existing.txt", replacement,
             sizeof(replacement));
  assert(dolphin_copy_start("/tmp/dolphin-copy-sd/source.txt",
                            "/tmp/dolphin-copy-usb/existing.txt") == 0);
  wait_for_state(DOLPHIN_COPY_FAILED);
  assert(dolphin_copy_snapshot(&snapshot) == 0 && snapshot.error == -EEXIST &&
         !snapshot.partial);
  fd = open("/tmp/dolphin-copy-usb/existing.txt", O_RDONLY);
  assert(fd >= 0 && read(fd, copied, sizeof(replacement)) ==
         (ssize_t)sizeof(replacement));
  assert(close(fd) == 0 && memcmp(copied, replacement, sizeof(replacement)) == 0);

  assert(dolphin_copy_start("/tmp/dolphin-copy-sd/../source.txt",
                            "/tmp/dolphin-copy-usb/bad.txt") == 0);
  wait_for_state(DOLPHIN_COPY_FAILED);
  assert(dolphin_copy_snapshot(&snapshot) == 0 && snapshot.error == -EINVAL &&
         !snapshot.partial);
  assert(symlink("source.txt", "/tmp/dolphin-copy-sd/source-link") == 0);
  assert(dolphin_copy_start("/tmp/dolphin-copy-sd/source-link",
                            "/tmp/dolphin-copy-usb/link-copy.txt") == 0);
  wait_for_state(DOLPHIN_COPY_FAILED);
  assert(dolphin_copy_snapshot(&snapshot) == 0 && snapshot.error == -ELOOP &&
         !snapshot.partial);
  assert(symlink("existing.txt", "/tmp/dolphin-copy-usb/destination-link") == 0);
  assert(dolphin_copy_start("/tmp/dolphin-copy-sd/source.txt",
                            "/tmp/dolphin-copy-usb/destination-link") == 0);
  wait_for_state(DOLPHIN_COPY_FAILED);
  assert(dolphin_copy_snapshot(&snapshot) == 0 && snapshot.error == -ELOOP &&
         !snapshot.partial);

  fd = open("/tmp/dolphin-copy-sd/large.bin", O_WRONLY | O_CREAT | O_TRUNC,
            0600);
  assert(fd >= 0 && ftruncate(fd, 128 * 1024 * 1024) == 0);
  assert(close(fd) == 0);
  assert(dolphin_copy_start("/tmp/dolphin-copy-sd/large.bin",
                            "/tmp/dolphin-copy-usb/canceled.bin") == 0);
  for (int i = 0; i < 5000; i++)
    {
      assert(dolphin_copy_snapshot(&snapshot) == 0);
      if (snapshot.copied > 0)
      {
        break;
      }
      usleep(1000);
    }
  assert(dolphin_copy_start("/tmp/dolphin-copy-sd/source.txt",
                            "/tmp/dolphin-copy-usb/reentry.txt") == -EBUSY);
  assert(dolphin_copy_cancel() == 0);
  wait_for_state(DOLPHIN_COPY_CANCELED);
  assert(dolphin_copy_snapshot(&snapshot) == 0 && snapshot.error == -ECANCELED &&
         snapshot.partial);

  unlink("/tmp/dolphin-copy-sd/source.txt");
  unlink("/tmp/dolphin-copy-sd/large.bin");
  unlink("/tmp/dolphin-copy-sd/source-link");
  unlink("/tmp/dolphin-copy-usb/copy.txt");
  unlink("/tmp/dolphin-copy-usb/existing.txt");
  unlink("/tmp/dolphin-copy-usb/canceled.bin");
  unlink("/tmp/dolphin-copy-usb/destination-link");
  assert(rmdir("/tmp/dolphin-copy-sd") == 0);
  assert(rmdir("/tmp/dolphin-copy-usb") == 0);

  puts("DOLPHIN_COPY_HOST_PASS: real files, exclusive destination, cancel/error");
  return 0;
}
