# SPDX-License-Identifier: Apache-2.0
"""Exercise the journaled KVDB patch against the real workspace UnQLite."""
import os
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
UPSTREAM = ROOT.parent / 'frameworks/system/utils'
ENGINE = ROOT.parent / 'external/unqlite/unqlite'
PATCH = ROOT / 'frameworks/patches/kvdb/0002-unqlite-explicit-journaled-commit.patch'

HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <unqlite.h>
#include "internal.h"
int property_set_binary(const char *, const void *, size_t, bool);
int property_delete(const char *);
static int fail_commit, fail_rollback, fail_cursor, fail_sync, cut_sync, sync_count, fail_dirsync, fail_delete, fail_after_delete, journal_deleted;
int __real_fsync(int);
int __real_fdatasync(int);
static int sync_file(int fd, int (*operation)(int))
{
    struct stat st;
    assert(fstat(fd, &st) == 0);
    if (fail_after_delete && journal_deleted && S_ISDIR(st.st_mode)) { fail_after_delete = 0; errno = EIO; return -1; }
    if (fail_dirsync && S_ISDIR(st.st_mode) && --fail_dirsync == 0) { errno = EIO; return -1; }
    if (fail_sync && S_ISREG(st.st_mode) && --fail_sync == 0) { errno = EIO; return -1; }
    int ret = operation(fd);
    if (cut_sync && ++sync_count == cut_sync) _exit(42);
    return ret;
}
int __wrap_fsync(int fd) { return sync_file(fd, __real_fsync); }
int __wrap_fdatasync(int fd) { return sync_file(fd, __real_fdatasync); }
int __real_unlink(const char *);
int __wrap_unlink(const char *path)
{
    if (fail_delete && strstr(path, "_unqlite_journal")) {
        fail_delete = 0; errno = EACCES; return -1;
    }
    int ret = __real_unlink(path);
    if (ret == 0 && strstr(path, "_unqlite_journal")) journal_deleted = 1;
    return ret;
}
int __real_unqlite_commit(unqlite *);
int __real_unqlite_rollback(unqlite *);
int __real_unqlite_kv_cursor_init(unqlite *, unqlite_kv_cursor **);
int __wrap_unqlite_commit(unqlite *db)
{ return fail_commit ? UNQLITE_IOERR : __real_unqlite_commit(db); }
int __wrap_unqlite_rollback(unqlite *db)
{ return fail_rollback ? UNQLITE_IOERR : __real_unqlite_rollback(db); }
int __wrap_unqlite_kv_cursor_init(unqlite *db, unqlite_kv_cursor **cur)
{ return fail_cursor ? UNQLITE_NOMEM : __real_unqlite_kv_cursor_init(db, cur); }
static void consume(const char *key, const void *data, size_t size, void *cookie)
{ (void)key; (void)data; (void)size; (*(int *)cookie)++; }
static void expect_old(struct kvdb *db)
{
    char value[8] = {0};
    assert(kvdb_persist_get(db, "persist.test", 13, value, sizeof(value)) == 4);
    assert(!memcmp(value, "old", 4));
}
int main(int argc, char **argv)
{
    struct kvdb *db = NULL;
    char value[16];
    int count = 0;
    assert(argc == 2);
    assert(kvdb_persist_init(&db) == 0);
    if (!strcmp(argv[1], "seed")) {
        assert(kvdb_persist_get(db, "missing", 8, value, sizeof(value)) == -ENOENT);
        assert(kvdb_persist_list(db, consume, &count) == 0 && count == 0);
        assert(kvdb_persist_set(db, "persist.test", 13, "old", 4, false) == 0);
        assert(kvdb_persist_set(db, "persist.pair", 13, "old", 4, false) == 0);
        assert(kvdb_persist_commit(db) == 0);
    } else if (!strcmp(argv[1], "read")) {
        expect_old(db);
        assert(kvdb_persist_get(db, "bulk.0", 7, value, sizeof(value)) == -ENOENT);
    } else if (!strcmp(argv[1], "read-pair")) {
        char pair[8] = {0};
        assert(kvdb_persist_get(db, "persist.test", 13, value, sizeof(value)) == 4);
        assert(kvdb_persist_get(db, "persist.pair", 13, pair, sizeof(pair)) == 4);
        assert(!memcmp(value, pair, 4));
        assert(!memcmp(value, "old", 4) || !memcmp(value, "new", 4));
    } else if (!strcmp(argv[1], "cut-sync")) {
        assert(kvdb_persist_set(db, "persist.test", 13, "new", 4, false) == 0);
        assert(kvdb_persist_set(db, "persist.pair", 13, "new", 4, false) == 0);
        cut_sync = atoi(getenv("KVDB_CUT_SYNC"));
        assert(kvdb_persist_commit(db) == 0);
    } else if (!strncmp(argv[1], "sync-failure-", 13)) {
        assert(kvdb_persist_set(db, "persist.test", 13, "new", 4, false) == 0);
        fail_sync = atoi(argv[1] + 13);
        assert(kvdb_persist_commit(db) == -EIO);
        expect_old(db);
    } else if (!strcmp(argv[1], "directory-failure") || !strcmp(argv[1], "delete-failure")) {
        assert(kvdb_persist_set(db, "persist.test", 13, "new", 4, false) == 0);
        fail_dirsync = !strcmp(argv[1], "directory-failure");
        fail_delete = !strcmp(argv[1], "delete-failure");
        assert(kvdb_persist_commit(db) == -EIO);
        expect_old(db);
    } else if (!strcmp(argv[1], "postdelete-sync-failure")) {
        assert(kvdb_persist_set(db, "persist.test", 13, "new", 4, false) == 0);
        assert(kvdb_persist_set(db, "persist.pair", 13, "new", 4, false) == 0);
        journal_deleted = 0;
        fail_after_delete = 1; /* Fail only after successful journal unlink. */
        assert(kvdb_persist_commit(db) == -EIO);
        assert(kvdb_persist_get(db, "persist.test", 13, value, sizeof(value)) == -EIO);
        kvdb_persist_uninit(db); db = NULL;
        assert(kvdb_persist_init(&db) == 0);
        assert(kvdb_persist_get(db, "persist.test", 13, value, sizeof(value)) == 4);
        assert(!memcmp(value, "new", 4));
        assert(kvdb_persist_get(db, "persist.pair", 13, value, sizeof(value)) == 4);
        assert(!memcmp(value, "new", 4));
        /* Reconcile the observed state, then restore the fixture explicitly. */
        assert(kvdb_persist_set(db, "persist.test", 13, "old", 4, false) == 0);
        assert(kvdb_persist_set(db, "persist.pair", 13, "old", 4, false) == 0);
        assert(kvdb_persist_commit(db) == 0);
    } else if (!strcmp(argv[1], "abort")) {
        assert(kvdb_persist_set(db, "persist.test", 13, "new", 4, false) == 0);
        /* Closing must roll back an uncommitted change. */
    } else if (!strcmp(argv[1], "commit-failure") || !strcmp(argv[1], "rollback-failure")) {
        fail_commit = 1;
        fail_rollback = !strcmp(argv[1], "rollback-failure");
        assert(kvdb_persist_set(db, "persist.test", 13, "new", 4, false) == 0);
        assert(kvdb_persist_commit(db) == -EIO);
        if (fail_rollback) {
            assert(kvdb_persist_get(db, "persist.test", 13, value, sizeof(value)) == -EIO);
            assert(kvdb_persist_set(db, "other", 6, "x", 2, false) == -EIO);
            assert(kvdb_persist_delete(db, "persist.test", 13) == -EIO);
        } else expect_old(db);
        fail_commit = fail_rollback = 0;
    } else if (!strcmp(argv[1], "cursor-failure")) {
        fail_cursor = 1;
        assert(kvdb_persist_list(db, consume, &count) == -ENOMEM);
    } else if (!strcmp(argv[1], "direct-failure")) {
        kvdb_persist_uninit(db); db = NULL;
        fail_commit = 1;
        assert(property_set_binary("persist.test", "new", 4, false) == -EIO);
        assert(property_delete("persist.test") == -EIO);
        fail_commit = 0;
    } else if (!strcmp(argv[1], "direct-success")) {
        kvdb_persist_uninit(db); db = NULL;
        assert(property_set_binary("persist.other", "value", 6, false) == 0);
        assert(property_delete("persist.other") == 0);
    } else if (!strcmp(argv[1], "crash")) {
        char payload[4096];
        char key[32];
        char journal[1024];
        struct stat st;
        memset(payload, 0x5a, sizeof(payload));
        assert(kvdb_persist_set(db, "persist.test", 13, "new", 4, false) == 0);
        for (int i = 0; i < 512; i++) {
            snprintf(key, sizeof(key), "bulk.%d", i);
            assert(kvdb_persist_set(db, key, strlen(key) + 1, payload, sizeof(payload), false) == 0);
        }
        snprintf(journal, sizeof(journal), "%s_unqlite_journal", getenv("KVDB_TEST_PATH"));
        assert(stat(journal, &st) == 0 && st.st_size > 0);
        _exit(0); /* No library cleanup: reopen must recover using its journal. */
    } else assert(0);
    kvdb_persist_uninit(db);
    return 0;
}
'''


class JournaledKvdbTest(unittest.TestCase):
    def test_cmake_build_tree_patch_integration(self):
        if shutil.which('cmake') is None:
            self.skipTest('cmake is required for build integration')
        originals = [UPSTREAM / 'kvdb' / name for name in
                     ('unqlite.c', 'direct.c', 'file.c')]
        originals.append(ENGINE / 'unqlite.c')
        before = [hashlib.sha256(path.read_bytes()).hexdigest() for path in originals]
        with tempfile.TemporaryDirectory(prefix='kvdb-cmake-') as tmp:
            directory = Path(tmp)
            # Use the same manifest-discovered paths as the firmware build.
            (directory / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.19)
project(kvdb_patch_integration C)
set(NUTTX_DIR "{ROOT.parent / 'nuttx'}")
set(NUTTX_APPS_DIR "{ROOT.parent / 'nuttx/../apps'}")
set(CONFIG_KVDB_DIRECT ON)
set(CONFIG_KVDB_UNQLITE ON)
set(CONFIG_KVDB_TEMPORARY_STORAGE ON)
set(CONFIG_KVDB_PERSIST_PATH "/mnt/sdnand/shaniu.db")
include("{ROOT / 'frameworks/cmake/kvdb_patches.cmake'}")
foreach(name unqlite.c direct.c file.c)
 get_filename_component(absolute "${{NUTTX_APPS_DIR}}/frameworks/system/utils/kvdb/${{name}}" ABSOLUTE)
 file(RELATIVE_PATH relative "${{CMAKE_CURRENT_SOURCE_DIR}}" "${{absolute}}")
 list(APPEND framework_sources "${{relative}}")
endforeach()
add_library(framework_utils STATIC ${{framework_sources}})
add_library(unqlite STATIC "${{NUTTX_APPS_DIR}}/external/unqlite/unqlite/unqlite.c")
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/sources.txt"
 CONTENT "$<TARGET_PROPERTY:framework_utils,SOURCES>;$<TARGET_PROPERTY:unqlite,SOURCES>")
''')
            build = directory / 'build'
            for _ in range(2):
                result = subprocess.run(['cmake', '-S', str(directory), '-B', str(build)],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                sources = (build / 'sources.txt').read_text().split(';')
                self.assertEqual(len(sources), 4)
                for source in sources:
                    self.assertTrue(Path(source).is_relative_to(build / 'bk7258-kvdb'))
                    self.assertTrue(Path(source).is_file())
            self.assertNotEqual((build / 'bk7258-kvdb/engine/unqlite.c').read_bytes(),
                                (ENGINE / 'unqlite.c').read_bytes())
        self.assertEqual(before, [hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in originals])

    def test_real_engine_recovery(self):
        with tempfile.TemporaryDirectory(prefix='kvdb-journal-') as tmp:
            directory = Path(tmp)
            (directory / 'kvdb').mkdir()
            for name in ('unqlite.c', 'direct.c', 'backend.c', 'internal.h'):
                shutil.copyfile(UPSTREAM / 'kvdb' / name, directory / 'kvdb' / name)
            subprocess.run(['git', 'apply', '--check', str(PATCH)], cwd=directory, check=True)
            subprocess.run(['git', 'apply', str(PATCH)], cwd=directory, check=True)
            (directory / 'port.h').write_text(
                '#include <stdbool.h>\n#include <stdlib.h>\n#include <errno.h>\n'
                '#include <string.h>\n#include <sys/types.h>\n'
                '#define zalloc(n) calloc(1, (n))\n#define PROP_NAME_MAX 32\n'
                '#define CONFIG_KVDB_PERSIST_PATH getenv("KVDB_TEST_PATH")\n')
            engine = directory / 'engine'
            engine.mkdir()
            for name in ('unqlite.c', 'unqlite.h'):
                shutil.copyfile(ENGINE / name, engine / name)
            engine_patch = ROOT / 'external/patches/unqlite/0001-propagate-commit-sync-errors.patch'
            subprocess.run(['git', 'apply', '--check', str(engine_patch)], cwd=engine, check=True)
            subprocess.run(['git', 'apply', str(engine_patch)], cwd=engine, check=True)
            harness = directory / 'recovery.c'
            harness.write_text(HARNESS)
            binary = directory / 'recovery'
            subprocess.run([
                os.environ.get('CC', 'cc'), '-std=gnu11', '-O1',
                '-include', str(directory / 'port.h'),
                '-I', str(ROOT / 'tests/host/bk7258/mocks'),
                '-I', str(ENGINE), '-I', str(directory / 'kvdb'),
                str(directory / 'kvdb/unqlite.c'), str(directory / 'kvdb/direct.c'),
                str(directory / 'kvdb/backend.c'), str(engine / 'unqlite.c'), str(harness),
                '-Wl,--wrap=unqlite_commit', '-Wl,--wrap=unqlite_rollback',
                '-Wl,--wrap=unqlite_kv_cursor_init', '-Wl,--wrap=fsync', '-Wl,--wrap=fdatasync', '-Wl,--wrap=unlink', '-pthread', '-lm', '-o', str(binary),
            ], check=True)
            env = dict(os.environ, KVDB_TEST_PATH=str(directory / 'persist.db'))
            for mode in ('seed', 'read', 'abort', 'read', 'commit-failure', 'read',
                         'rollback-failure', 'read', 'cursor-failure', 'direct-failure',
                         'read', 'direct-success', 'read', 'sync-failure-1', 'read', 'sync-failure-2', 'read', 'directory-failure', 'read', 'delete-failure', 'read', 'postdelete-sync-failure', 'read', 'crash', 'read'):
                with self.subTest(mode=mode):
                    subprocess.run([str(binary), mode], env=env, check=True, timeout=20)
            for cut in range(1, 5):
                with self.subTest(sync_cut=cut):
                    env = dict(os.environ, KVDB_TEST_PATH=str(directory / f'cut-{cut}.db'),
                               KVDB_CUT_SYNC=str(cut))
                    subprocess.run([str(binary), 'seed'], env=env, check=True, timeout=20)
                    result = subprocess.run([str(binary), 'cut-sync'], env=env, timeout=20)
                    self.assertIn(result.returncode, (0, 42))
                    subprocess.run([str(binary), 'read-pair'], env=env, check=True, timeout=20)



if __name__ == '__main__':
    unittest.main()
