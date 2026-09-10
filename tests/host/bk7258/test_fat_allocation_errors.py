#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise patched FAT allocation functions with full/corrupt/error media."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_camera_control_patches import function

REPO = Path(__file__).resolve().parents[3]

class FatAllocationErrors(unittest.TestCase):
    def test_actual_allocation_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('fs_fat32.c', 'fs_fat32util.c'):
                p = root / 'fs/fat' / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes((REPO.parent / 'nuttx/fs/fat' / name).read_bytes())
            subprocess.run(['git', 'apply', str(REPO / 'nuttx/patches/fs/0001-preserve-fat-allocation-errors.patch')], cwd=root, check=True)
            source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#define FAR
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define ROUND_DOWN(a,b) ((a)/(b)*(b))
struct fat_mountpt_s { int fs_fatsecperclus, fs_hwsectorsize; uint32_t fs_nclusters; uint32_t fs_fsinextfree, fs_fsifreecount; bool fs_fsidirty; };
struct fat_file_s { int ff_size, ff_startcluster, ff_currentcluster, ff_pos; };
struct inode { void *i_private; };
struct file { struct inode *f_inode; void *f_priv; int f_pos; };
static int allocation_result, media_mode;
static int32_t fat_extendchain(struct fat_mountpt_s *fs, uint32_t c) { (void)fs; (void)c; return allocation_result; }
static int fat_getcluster(struct fat_mountpt_s *fs, uint32_t c) { (void)fs; (void)c; return media_mode == 1 ? 1 : media_mode == 2 ? -ETIMEDOUT : media_mode == 3 ? 0 : 0x0fffffff; }
static int fat_putcluster(struct fat_mountpt_s *fs, uint32_t c, uint32_t v) { (void)fs; (void)c; (void)v; return 0; }
static void fat_ffcacheinvalidate(struct fat_mountpt_s *fs, struct fat_file_s *ff) { (void)fs; (void)ff; }
static int fat_zero_cluster(struct fat_mountpt_s *fs, int c, int s, int e) { (void)fs; (void)c; (void)s; (void)e; return 0; }
static int fat_currentsector(struct fat_mountpt_s *fs, struct fat_file_s *ff, int p) { (void)fs; (void)ff; (void)p; return 0; }
'''
            source += function((root / 'fs/fat/fs_fat32.c').read_text(), 'static int fat_get_sectors(')
            source += function((root / 'fs/fat/fs_fat32util.c').read_text(), 'int32_t fat_extendchain(').replace('int32_t fat_extendchain(', 'int32_t actual_extendchain(')
            source += r'''
int main(void) {
  struct fat_mountpt_s fs = {.fs_fatsecperclus=4, .fs_hwsectorsize=512, .fs_nclusters=8, .fs_fsinextfree=2, .fs_fsifreecount=8};
  struct inode inode = {&fs};
  int results[] = {0, -ETIMEDOUT, -EIO, 1, 10};
  int expected[] = {-ENOSPC, -ETIMEDOUT, -EIO, -EIO, -EIO};
  for (unsigned i=0; i<5; i++) for (unsigned branch=0; branch<2; branch++) {
    struct fat_file_s ff = {0}; struct file f = {&inode, &ff, branch ? 4096 : 0};
    allocation_result=results[i];
    assert(fat_get_sectors(&f, false)==expected[i]);
    assert(ff.ff_startcluster==0);
  }
  media_mode=0; assert(actual_extendchain(&fs, 0)==0);
  media_mode=1; assert(actual_extendchain(&fs, 2)==-EIO);
  media_mode=2; assert(actual_extendchain(&fs, 2)==-ETIMEDOUT);
  media_mode=3; assert(actual_extendchain(&fs, 0)>=2);
  return 0;
}
'''
            p = root / 'test.c'; p.write_text(source)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare', str(p), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)

if __name__ == '__main__':
    unittest.main()
