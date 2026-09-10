#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Apply maintenance patch to pinned source and exercise its frame boundary."""
import subprocess
import tempfile
from pathlib import Path
from test_mfrc522_read_errors import extract_function, NUTTX, BASELINE, REPOSITORY

PREFIX = r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define FAR
#define OK 0
struct mfrc522_dev_s { uint32_t frame_timeout_ms; };
struct mfrc522_exchange_s { uint32_t timeout_ms; uint8_t tx_length, rx_length, tx[62], rx[62]; };
#define MFRC522_TMODE_REG 0x54
#define MFRC522_CONTROL_REG 0x18
#define MFRC522_COMMAND_REG 0x02
#define MFRC522_IDLE_CMD 0
#define MFRC522_TSTOP_NOW 0x80
static uint8_t registers[256];
static uint8_t mfrc522_readu8(struct mfrc522_dev_s *d,int reg)
{ (void)d;return registers[reg]; }
static void mfrc522_writeu8(struct mfrc522_dev_s *d,int reg,uint8_t value)
{ (void)d;registers[reg]=value; }
static int crc_result, radio_result, calls;
static uint8_t reply_length=4, reply_bits;
static int mfrc522_calc_crc(struct mfrc522_dev_s *d, uint8_t *tx, uint8_t n, uint8_t *out)
{ (void)d; (void)tx; assert(n>=1 && n<=62); out[0]=0x12;out[1]=0x34;return crc_result; }
static int mfrc522_transcv_data(struct mfrc522_dev_s *d,uint8_t *tx,uint8_t n,uint8_t *rx,uint8_t *len,uint8_t *bits,uint8_t align,bool crc)
{ assert(d->frame_timeout_ms<=300000); if(d->frame_timeout_ms)assert(registers[MFRC522_TMODE_REG]==0x09); calls++;assert(n>=3&&n<=64);assert(tx[n-2]==0x12&&tx[n-1]==0x34);assert(*len==64&&*bits==0&&align==0&&crc);memset(rx,0xa5,64);*len=reply_length;*bits=reply_bits;return radio_result; }
'''
MAIN = r'''
int main(void) {
 struct mfrc522_dev_s d={0}; struct mfrc522_exchange_s x;
 assert(mfrc522_exchange(&d,0)==-EINVAL);
 for(int n=0;n<256;n++) {
  memset(&x,0xff,sizeof(x));x.tx_length=n;x.timeout_ms=0;calls=0;
  int ret=mfrc522_exchange(&d,&x);
  if(n>=1&&n<=62){assert(ret==0&&calls==1&&x.rx_length==2&&x.rx[0]==0xa5);}
  else {assert(ret==-EINVAL&&calls==0&&x.rx_length==0);for(int i=0;i<62;i++)assert(x.rx[i]==0);}
 }
 x.tx_length=1;
 int errors[]={-ETIMEDOUT,-EFAULT,-ENOMEM,-EACCES};
 for(unsigned n=0;n<sizeof(errors)/sizeof(errors[0]);n++) {
  radio_result=errors[n];memset(x.rx,0xff,62);x.rx_length=62;
  assert(mfrc522_exchange(&d,&x)==errors[n]&&x.rx_length==0);
  for(int i=0;i<62;i++)assert(x.rx[i]==0);
 }
 radio_result=0;crc_result=-ETIMEDOUT;calls=0;
 assert(mfrc522_exchange(&d,&x)==-ETIMEDOUT&&calls==0);crc_result=0;
 for(int n=0;n<256;n++) {reply_length=n;int ret=mfrc522_exchange(&d,&x);
  if(n>=2&&n<=64)assert(ret==0&&x.rx_length==n-2);else assert(ret==-EPROTO&&x.rx_length==0);
 }
 reply_length=4;reply_bits=1;assert(mfrc522_exchange(&d,&x)==-EPROTO&&x.rx_length==0);
 reply_bits=0;registers[MFRC522_TMODE_REG]=0x89;
 uint32_t waits[]={1,200,5000,39000,300000,300001,UINT32_MAX};
 for(unsigned i=0;i<sizeof(waits)/sizeof(waits[0]);i++) {
  x.timeout_ms=waits[i];calls=0;
  int ret=mfrc522_exchange(&d,&x);
  assert(ret==(waits[i]<=300000?0:-EINVAL));
  assert(calls==(waits[i]<=300000?1:0));
  assert(registers[MFRC522_TMODE_REG]==0x89&&d.frame_timeout_ms==0);
 }
 x.timeout_ms=5000;radio_result=-ETIMEDOUT;
 assert(mfrc522_exchange(&d,&x)==-ETIMEDOUT);
 assert(registers[MFRC522_TMODE_REG]==0x89&&d.frame_timeout_ms==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='mfrc522-exchange-') as tmp:
 root=Path(tmp)
 for name in ('drivers/contactless/mfrc522.c','include/nuttx/contactless/ioctl.h','drivers/contactless/mfrc522.h'):
  p=root/name;p.parent.mkdir(parents=True,exist_ok=True)
  p.write_bytes(subprocess.check_output(['git','-C',str(NUTTX),'show',BASELINE+':'+name]))
 for name in ('0001-mfrc522-propagate-card-selection-errors.patch','0002-mfrc522-crca-frame-exchange.patch','0003-mfrc522-correct-polling-deadlines.patch','0004-mfrc522-per-frame-wait.patch','0005-mfrc522-rf-field-control.patch'):
  subprocess.run(['git','apply','--unsafe-paths',str(REPOSITORY/'nuttx/patches/contactless'/name)],cwd=root,check=True)
 source=(root/'drivers/contactless/mfrc522.c').read_text()
 expected=source.replace('#include <nuttx/contactless/mfrc522.h>',
     '#include <nuttx/contactless/mfrc522.h>\n#include <nuttx/contactless/mfrc522_frame.h>')
 expected=expected.replace('CONFIG_MFRC522_SPI_FREQ', 'CONFIG_CL_MFRC522_FRAME_SPI_FREQ')
 assert (REPOSITORY/'nuttx/drivers/contactless/mfrc522.c').read_text()==expected
 overlay_header=(REPOSITORY/'nuttx/include/nuttx/contactless/mfrc522_frame.h').read_text()
 patched_header=(root/'include/nuttx/contactless/ioctl.h').read_text()
 assert patched_header[patched_header.index('/* Selected-card,'):patched_header.index('struct picc_uid_s')] in overlay_header
 c=root/'test.c';c.write_text(PREFIX+extract_function(source,'static int mfrc522_exchange(')+MAIN)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-o',str(root/'test'),str(c)],check=True)
 subprocess.run([str(root/'test')],check=True)
 print('PASS: pinned patches compose; CRC transport sizes, failure wiping, malformed replies')
