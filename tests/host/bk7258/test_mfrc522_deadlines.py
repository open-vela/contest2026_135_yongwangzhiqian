#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise actual driver polling with an injected clock and missing IRQs."""
import re
import subprocess
import tempfile
from pathlib import Path
from test_mfrc522_read_errors import REPOSITORY, extract_function

source = (REPOSITORY / 'nuttx/drivers/contactless/mfrc522.c').read_text()
header = (REPOSITORY / 'nuttx/drivers/contactless/mfrc522.h').read_text()
bodies = '\n'.join(extract_function(source, 'int ' + name + '(')
                   for name in ('mfrc522_calc_crc', 'mfrc522_comm_picc'))
constants = set(re.findall(r'\bMFRC522_[A-Z_]+\b', bodies))
defines = '\n'.join(line for line in re.sub(r'# +define', '#define', header).splitlines()
                    if line.startswith('#define ') and line.split()[1] in constants)
prefix = r'''
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#define FAR
#define OK 0
struct mfrc522_dev_s { uint32_t frame_timeout_ms; };
static int nxsig_usleep(int us) { (void)us;return 0; }
static long long now_ns, initial_ns;
static int ticks;
static void clock_systime_timespec(struct timespec *t) {
 assert(ticks++ < 31000);
 t->tv_sec=now_ns/1000000000; t->tv_nsec=now_ns%1000000000;
 now_ns+=10000000;
}
static void mfrc522_writeu8(struct mfrc522_dev_s *d,int r,int v)
{ (void)d;(void)r;(void)v; }
static uint8_t mfrc522_readu8(struct mfrc522_dev_s *d,int r)
{ (void)d;(void)r;return 0; }
static void mfrc522_writeblk(struct mfrc522_dev_s *d,int r,uint8_t *b,int n)
{ (void)d;(void)r;(void)b;(void)n; }
static void mfrc522_readblk(struct mfrc522_dev_s *d,int r,uint8_t *b,int n,int a)
{ (void)d;(void)r;(void)b;(void)n;(void)a; }
'''
main = r'''
int main(void) {
 struct mfrc522_dev_s d={0}; uint8_t b[2]={0};
 long long starts[]={1000000000LL,1850000000LL,1990000000LL};
 for(unsigned i=0;i<3;i++) for(int radio=0;radio<2;radio++) {
  now_ns=initial_ns=starts[i]; ticks=0;
  int ret=radio ? mfrc522_comm_picc(&d,MFRC522_TRANSCV_CMD,0x30,b,1,0,0,0,0,false)
                : mfrc522_calc_crc(&d,b,1,b);
  assert(ret==-ETIMEDOUT);
  assert(now_ns-initial_ns==210000000);
 }
 uint32_t waits[]={1,200,5000,39000,300000};
 for(unsigned i=0;i<sizeof(waits)/sizeof(waits[0]);i++) {
  d.frame_timeout_ms=waits[i];now_ns=initial_ns=1990000000LL;ticks=0;
  assert(mfrc522_comm_picc(&d,MFRC522_TRANSCV_CMD,0x30,b,1,0,0,0,0,false)==-ETIMEDOUT);
  long long elapsed=now_ns-initial_ns-10000000;
  long long deadline=((long long)waits[i]+20)*1000000;
  assert(elapsed>=deadline&&elapsed<deadline+10000000);
 }
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='mfrc522-deadlines-') as directory:
 root=Path(directory); c=root/'test.c'; binary=root/'test'
 c.write_text(prefix+defines+'\n'+bodies+main)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('PASS: default CRC/radio deadlines and 1..300000 ms custom waits, including rollover')
