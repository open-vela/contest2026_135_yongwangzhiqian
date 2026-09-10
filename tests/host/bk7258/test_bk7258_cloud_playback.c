/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_playback.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct audio { bool mic, dac, ready; size_t bytes; int fail; };
static int ok(void *p) { (void)p; return 0; }
static int mic_acquire(void *p)
{ struct audio *a=p; assert(!a->dac && !a->mic); a->mic=true; return 0; }
static int mic_release(void *p)
{ ((struct audio *)p)->mic=false; return 0; }
static int dac_acquire(void *p)
{ struct audio *a=p; assert(!a->dac && !a->mic); a->dac=true; return 0; }
static int dac_release(void *p)
{ ((struct audio *)p)->dac=false; return 0; }
static ssize_t write_pcm(void *p, const uint8_t *data, size_t size)
{
  struct audio *a=p;
  assert(a->dac && !a->mic && size==640);
  if (a->fail) return a->fail;
  assert(fwrite(data,1,size,stdout)==size); a->bytes+=size; return size;
}
static int drain(void *p) { (void)p; return -EINPROGRESS; }
static int result(void *p) { return ((struct audio *)p)->ready ? 0 : -EAGAIN; }
static const struct bkvoice_turn_audio_ops_s ops = {
  .mic_acquire=mic_acquire,.mic_prepare=ok,.mic_start=ok,.mic_stop=ok,
  .mic_drain=ok,.mic_release=mic_release,.dac_acquire=dac_acquire,
  .dac_prepare=ok,.dac_start=ok,.dac_write=write_pcm,.dac_drain=drain,
  .dac_result=result,.dac_stop=ok,.dac_release=dac_release};
static uint64_t now(void *p) { (void)p; return 300; }
int main(int argc, char **argv)
{
  assert(argc==5);
  size_t samples=strtoul(argv[1],NULL,10), chunk=strtoul(argv[2],NULL,10);
  int hz=atoi(argv[3]), failure=atoi(argv[4]);
  struct audio a={.fail=failure == 1 ? -ECANCELED : 0};
  struct bkvoice_turn_s turn;
  struct bkvoice_turn_token_s token;
  struct bkvoice_turn_limits_s limits={10000,10000,10000,640};
  assert(bkvoice_turn_initialize(&turn,&ops,&a,&limits,7)==0);
  assert(bkvoice_turn_session_open(&turn,1)==0);
  assert(bkvoice_turn_ptt_down(&turn,1,1,100,&token)==0);
  token.sequence=2;
  assert(bkvoice_turn_ptt_up(&turn,&token,200)==0);
  struct bkcloud_playback_s play;
  assert(bkcloud_playback_begin(&play,&turn,now,NULL)==0);
  assert(bkcloud_playback_begin(&play,&turn,now,NULL)==-EBUSY);
  if (failure == 3) {
    const uint8_t partial[] = {0xaa, 0xbb};
    play.input_samples = 90u * 24000u - 1;
    assert(bkcloud_playback_feed(&play, partial, 1) == 0 && play.partial_sample);
    assert(bkcloud_playback_feed(&play, partial, 2) == -EINVAL);
    assert(!play.filter && !play.partial_sample && play.low_byte == 0 && !a.dac);
    return 0;
  }
  uint8_t *input=malloc(samples*2);
  for(size_t i=0;i<samples;i++) {
    int16_t value=(int16_t)(10000*sin(2*3.141592653589793*hz*i/24000));
    input[2*i]=(uint16_t)value; input[2*i+1]=(uint16_t)value>>8;
  }
  size_t total = samples*2 - (failure == 2 ? 1 : 0);
  int ret=0;
  for(size_t offset=0;offset<total && !ret;offset+=chunk) {
    size_t size=total-offset; if(size>chunk) size=chunk;
    ret=bkcloud_playback_feed(&play,input+offset,size);
  }
  free(input);
  if(!ret) ret=bkcloud_playback_end(&play);
  if(failure) {
    assert(ret==(failure == 2 ? -EBADMSG : -ECANCELED) && !a.dac && !play.filter);
    assert(!play.partial_sample && play.low_byte == 0);
    assert(turn.state==BKVOICE_TURN_IDLE); return 0;
  }
  assert(ret==0 && !play.filter);
  assert(play.output_samples==(samples*2+2)/3);
  assert(a.bytes==((play.output_samples+319)/320)*640);
  assert(turn.state==BKVOICE_TURN_DRAINING && a.dac);
  assert(bkvoice_turn_poll(&turn)==0 && a.dac);
  a.ready=true;
  assert(bkvoice_turn_poll(&turn)==0);
  assert(turn.state==BKVOICE_TURN_IDLE && !a.dac);
  return 0;
}
