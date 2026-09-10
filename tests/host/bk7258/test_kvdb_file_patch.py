# SPDX-License-Identifier: Apache-2.0
"""Compile the upstream KVDB file backend in a temporary patched tree.

The Makefile run-kvdb-file target owns this fixture. No official checkout is
modified. POSIX wrappers inject faults while actual files verify byte content.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
UPSTREAM = ROOT.parent / 'frameworks/system/utils'
PATCH = ROOT / 'frameworks/patches/kvdb/0001-file-handle-partial-interrupted-io.patch'

HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
static int fault, calls, closes;
ssize_t __real_write(int, const void *, size_t);
ssize_t __real_read(int, void *, size_t);
int __real_close(int);
int kvdb_file_set(const char *, const char *, const void *, size_t);
ssize_t kvdb_file_get(const char *, const char *, void *, size_t);
int kvdb_file_delete(const char *, const char *);
int kvdb_file_list(const char *, void (*)(const char *, const void *, size_t, void *), void *);
static void consume(const char *key, const void *value, size_t size, void *cookie)
{
    (void)key; (void)value; (void)size; (void)cookie;
    assert(0); /* Failed reads must not publish a value. */
}
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
    assert(++calls < 32); /* A zero-write bug must not hang the test. */
    if (fault == 1 && size > 2) size = 2;
    if (fault == 2 && calls <= 2) { errno = EINTR; return -1; }
    if (fault == 3) return 0;
    if (fault == 4) { errno = ENOSPC; return -1; }
    return __real_write(fd, data, size);
}
ssize_t __wrap_read(int fd, void *data, size_t size)
{
    assert(++calls < 32);
    if (fault == 5 && calls == 1) { errno = EINTR; return -1; }
    if (fault == 6) {
        if (calls == 2) { errno = EINTR; return -1; }
        if (size > 2) size = 2;
    }
    if (fault == 7 || fault == 11) { errno = EIO; return -1; }
    return __real_read(fd, data, size);
}
int __wrap_close(int fd)
{
    int ret = __real_close(fd);
    closes++;
    errno = EBADF; /* Closing must not replace an earlier I/O error. */
    if (fault == 8) { errno = EIO; return -1; }
    return ret;
}
int main(int argc, char **argv)
{
    char data[32] = {0};
    const char expected[] = "abcdefghi";
    int ret;
    assert(argc == 3);
    fault = atoi(argv[2]);
    if (fault >= 5 && fault <= 7) {
        int selected = fault;
        fault = 0;
        assert(kvdb_file_set(argv[1], "persist.test", expected, sizeof(expected)) == 0);
        fault = selected; calls = closes = 0;
        ret = kvdb_file_get(argv[1], "persist.test", data, sizeof(data));
        if (fault == 7) assert(ret == -EIO);
        else { assert(ret == sizeof(expected)); assert(!memcmp(data, expected, sizeof(expected))); }
        assert(closes == 1);
    } else if (fault == 11) {
        assert(kvdb_file_list(argv[1], consume, NULL) == -EIO);
    } else if (fault == 9) {
        assert(kvdb_file_delete(argv[1], "missing") == -ENOENT);
    } else {
        ret = kvdb_file_set(argv[1], "persist.test", expected,
            fault == 10 ? 0 : sizeof(expected));
        assert(closes == 1);
        if (fault == 3 || fault == 8) assert(ret == -EIO);
        else if (fault == 4) assert(ret == -ENOSPC);
        else {
            assert(ret == 0);
            fault = calls = 0;
            ret = kvdb_file_get(argv[1], "persist.test", data, sizeof(data));
            if (!strcmp(argv[2], "10")) assert(ret == 0);
            else { assert(ret == sizeof(expected)); assert(!memcmp(data, expected, sizeof(expected))); }
        }
    }
    return 0;
}
'''


class KvdbFilePatchTest(unittest.TestCase):
    def test_real_backend_faults(self):
        with tempfile.TemporaryDirectory(prefix='kvdb-file-') as tmp:
            directory = Path(tmp)
            (directory / 'kvdb').mkdir()
            for name in ('file.c', 'internal.h'):
                shutil.copyfile(UPSTREAM / 'kvdb' / name, directory / 'kvdb' / name)
            subprocess.run(['git', 'apply', '--check', str(PATCH)], cwd=directory, check=True)
            subprocess.run(['git', 'apply', str(PATCH)], cwd=directory, check=True)
            harness = directory / 'faults.c'
            harness.write_text(HARNESS)
            binary = directory / 'faults'
            subprocess.run([
                os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-include', 'stdbool.h', '-include', 'stdlib.h',
                '-include', 'limits.h', '-include', 'dirent.h',
                '-I', str(ROOT / 'tests/host/bk7258/mocks'),
                str(directory / 'kvdb/file.c'), str(harness),
                '-Wl,--wrap=write', '-Wl,--wrap=read', '-Wl,--wrap=close',
                '-o', str(binary),
            ], check=True)
            for scenario in range(12):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(binary), str(directory), str(scenario)],
                                   check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
