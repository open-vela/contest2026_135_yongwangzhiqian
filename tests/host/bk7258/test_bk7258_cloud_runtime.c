/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_cloud_runtime.h"
#include "bk7258_cloud_client.h"
#include "bk7258_voice_tls.h"
#include "bk7258_preferences.h"
#ifdef CONFIG_BK7258_VISION_SERVICE
#include "bk7258_vision_service.h"
#endif
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef CONFIG_BK7258_PROVISION_GATT
#include "bk7258_cloud_memory.h"
#include <mbedtls/entropy.h>
static unsigned memory_loads, memory_reads, memory_writes;
static bool memory_enabled, memory_policy_enabled, memory_blocked, memory_entered;
static int memory_policy_error;
static unsigned memory_mutations;
static pthread_t owner_thread;
static void pause_ms(void);
void mbedtls_entropy_init(mbedtls_entropy_context *e) { memset(e, 0, sizeof(*e)); }
void mbedtls_entropy_free(mbedtls_entropy_context *e) { (void)e; }
int mbedtls_entropy_func(void *e, unsigned char *out, size_t n)
{ (void)e; memset(out, 42, n); return 0; }
int bkcloud_memory_policy_load(const char *root, const uint8_t owner[32],
                               struct bkcloud_memory_policy_s *p)
{
 assert(!pthread_equal(owner_thread, pthread_self()));
 assert(!strcmp(root, "/cpdata/shaniu/memory-policy") && owner[0] == 42);
 memset(p, 0, sizeof(*p)); p->enabled = memory_policy_enabled; p->key[0] = 42;
 memory_loads++; return 0;
}
int bkcloud_memory_policy_set(const char *root, const uint8_t owner[32], bool enabled,
                              bool rotate, bkcloud_memory_random_t random, void *context)
{
 (void)root; (void)random; (void)context;
 assert(!pthread_equal(owner_thread, pthread_self()) && owner[0] == 42);
 assert(!rotate || !enabled);
 memory_mutations++;
 if (memory_policy_error) return memory_policy_error;
 memory_policy_enabled = enabled; return 0;
}
int bk7258_preferences_with_storage(int (*operation)(void *), void *context)
{ assert(!pthread_equal(owner_thread, pthread_self())); return operation(context); }
int bkcloud_memory_restore(const char *root, const struct bkcloud_memory_policy_s *p,
                           void *plain, size_t capacity, size_t *used)
{
 (void)root; (void)p;
 struct bkcloud_history_s *h = calloc(1, sizeof(*h)); assert(h);
 h->count = 1; strcpy(h->turns[0].user, "previous"); strcpy(h->turns[0].assistant, "remembered");
 int ret = bkcloud_history_encode(h, BK7258_PERSONA_QUIET, plain, capacity, used);
 free(h); memory_reads++; return ret;
}
int bkcloud_memory_save(const char *root, const struct bkcloud_memory_policy_s *p,
                        const void *plain, size_t size,
                        bkcloud_memory_random_t random, void *context)
{
 (void)root; (void)p; (void)random; (void)context;
 struct bkcloud_history_s *h = calloc(1, sizeof(*h)); assert(h);
 assert(bkcloud_history_decode(h, BK7258_PERSONA_QUIET, plain, size) == 0);
 assert(h->count == 2 && !strcmp(h->turns[0].user, "previous") && !strcmp(h->turns[1].user, "hello")); free(h);
 __atomic_store_n(&memory_entered, true, __ATOMIC_RELEASE);
 while (__atomic_load_n(&memory_blocked, __ATOMIC_ACQUIRE)) pause_ms();
 memory_writes++; return 0;
}
#endif
static const struct bkvoice_capture_sink_ops_s *sink;
static void *sink_context;
static int commits, asr_calls, chat_calls, tts_calls;
static int failed_stage, stage_error;
static int expected_history = -1;
static unsigned int cleared_nonempty_history;
static const char *recognized_text = "hello";
static unsigned auto_prefill_calls;
static int auto_down_prefill_error;
static bool auto_up_leave_worker;
static bool auto_cancel_leave_worker;
#ifdef CONFIG_BK7258_VISION_SERVICE
static int camera_calls, image_calls, camera_error;
static bool camera_blocked, camera_entered;
#endif
void bkcloud_history_clear(struct bkcloud_history_s *history)
{if(history->count)cleared_nonempty_history++;memset(history,0,sizeof(*history));}
static bool blocked, entered, interrupted, playback_ready;
static int dns_calls;
static bool dns_fail;
static bool preferences_fail;
static enum bk7258_persona_e desired_persona=BK7258_PERSONA_QUIET;
static const char *expected_style="安静克制";
int bk7258_preferences_get(struct bk7258_preferences_s *p)
{ if(preferences_fail)return -EBUSY;memset(p,0,sizeof(*p));p->persona=desired_persona;return 0; }
static bool dns_blocked, dns_entered;
static void pause_ms(void);
int getaddrinfo(const char *host,const char *service,const struct addrinfo *hints,
                struct addrinfo **result)
{
 assert(!strcmp(host,"cloud.example") && !service && hints->ai_family==AF_INET);
 dns_calls++;
 __atomic_store_n(&dns_entered,true,__ATOMIC_RELEASE);
 while(__atomic_load_n(&dns_blocked,__ATOMIC_ACQUIRE))pause_ms();
 if(dns_fail)return EAI_AGAIN;
 *result=calloc(1,sizeof(**result));assert(*result);
 (*result)->ai_family=AF_INET;(*result)->ai_addrlen=sizeof(struct sockaddr_in);
 (*result)->ai_addr=calloc(1,(*result)->ai_addrlen);assert((*result)->ai_addr);
 ((struct sockaddr_in *)(*result)->ai_addr)->sin_addr.s_addr=htonl(0xcb007100u+dns_calls);
 return 0;
}
void freeaddrinfo(struct addrinfo *a) {free(a->ai_addr);free(a);}
static void pause_ms(void) { struct timespec ts={0,1000000};nanosleep(&ts,NULL); }
uint64_t bkvoice_config_now_ms(void *p)
{ (void)p;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000ULL+t.tv_nsec/1000000; }
int bkvoice_config_trusted_time(void *p) { (void)p;return 0; }
int bkvoice_tls_initialize(struct bkvoice_tls_s *t,const struct bkvoice_tls_config_s *c)
{ assert(c->server_auth_only && !c->client_key);t->initialized=true;__atomic_store_n(&interrupted,false,__ATOMIC_RELEASE);return 0; }
int bkvoice_tls_uninitialize(struct bkvoice_tls_s *t) {t->initialized=false;return 0;}
static int interrupt(void *p) {(void)p;__atomic_store_n(&interrupted,true,__ATOMIC_RELEASE);return 0;}
static const struct bkvoice_wss_tls_ops_s tls_ops={.interrupt=interrupt};
const struct bkvoice_wss_tls_ops_s *bkvoice_tls_ops(void) {return &tls_ops;}
int bkvoice_ptt_session_open(struct bkvoice_ptt_s *p,uint32_t id,
 const struct bkvoice_capture_sink_ops_s *s,void *c)
{p->capture_ready=true;p->turn.last_session_id=id;p->turn.state=BKVOICE_TURN_IDLE;sink=s;sink_context=c;return 0;}
int bkvoice_ptt_session_close(struct bkvoice_ptt_s *p,int why)
{(void)why;p->capture_ready=false;p->worker_joinable=false;p->turn.state=BKVOICE_TURN_IDLE;return 0;}
int bkvoice_ptt_cancel(struct bkvoice_ptt_s *p,int why)
{
 if (auto_cancel_leave_worker && p->worker_joinable) return -EAGAIN;
 p->worker_joinable=false;p->turn.state=BKVOICE_TURN_IDLE;p->turn.last_error=why;
 return 0;
}
int bkvoice_ptt_down(struct bkvoice_ptt_s *p,uint64_t now,struct bkvoice_turn_token_s *token)
{
 memset(token,0,sizeof(*token));p->turn.state=BKVOICE_TURN_CAPTURING;
 p->turn.deadline_ms=now+30000;
 p->turn.last_error=0;p->worker_joinable=true;
 uint8_t pcm[640]={1,2};assert(sink->start(sink_context,token)==0);
 assert(sink->audio(sink_context,token,pcm,sizeof(pcm))==0);return 0;
}
int bkvoice_ptt_down_prefill(struct bkvoice_ptt_s *p,uint64_t now,
 bkvoice_capture_prefill_read_t read_frame,void *prefill_context,size_t prefill_frames,
 bkvoice_capture_live_observer_t live_observer,void *live_context,
 struct bkvoice_turn_token_s *token)
{
 uint8_t prefill[BKVOICE_CAPTURE_FRAME_BYTES];
 assert(read_frame && prefill_frames == 2 && !live_observer && !live_context);
 for (size_t i = 0; i < prefill_frames; i++) assert(read_frame(prefill_context, i, prefill) == 0);
 auto_prefill_calls += (unsigned)prefill_frames;
 int ret = bkvoice_ptt_down(p, now, token);
 return ret ? ret : auto_down_prefill_error;
}
int bkvoice_ptt_up(struct bkvoice_ptt_s *p,uint64_t now)
{
 (void)now;
 if (auto_up_leave_worker) return -EAGAIN;
 p->worker_joinable=false;p->turn.state=BKVOICE_TURN_WAITING_TTS;
 return sink->end(sink_context,&p->token);
}
int bkvoice_ptt_timeout(struct bkvoice_ptt_s *p,uint64_t now)
{
 if(now < p->turn.deadline_ms)return -EAGAIN;
 p->worker_joinable=false;p->turn.state=BKVOICE_TURN_IDLE;
 p->turn.last_error=-ETIMEDOUT;return 0;
}
int bkvoice_turn_poll(struct bkvoice_turn_s *t)
{if(playback_ready)t->state=BKVOICE_TURN_IDLE;return 0;}
int bkcloud_recognize(struct bkcloud_client_s *c,const struct bkcloud_config_s *cfg,
 const struct bkvoice_wss_tls_ops_s *ops,void *tls,uint64_t due,const uint8_t *pcm,size_t n,char *text,size_t cap)
{(void)c;(void)cfg;(void)ops;(void)tls;(void)due;assert(n==640 && pcm[0]==1 && cap>6);asr_calls++;if(failed_stage==1)return stage_error;strcpy(text,recognized_text);return 0;}
int bkcloud_chat(struct bkcloud_client_s *c,const struct bkcloud_config_s *cfg,
 const struct bkvoice_wss_tls_ops_s *ops,void *tls,uint64_t due,const char *persona,
 const struct bkcloud_history_s *h,const char *input,char *text,size_t cap)
{(void)c;(void)cfg;(void)ops;(void)due;(void)persona;(void)h;(void)input;chat_calls++;
 assert(ntohl(((struct bkvoice_tls_s *)tls)->config.peer_address.s_addr)==0xcb007100u+dns_calls);
 if(!strcmp(input,"hello")) {assert(strstr(persona,"虚构的 AI 伴侣"));assert(strstr(persona,expected_style));}
 if(!strcmp(input,"hello") && expected_history >= 0)
   assert(h->count == (unsigned)expected_history);
#ifdef CONFIG_BK7258_PROVISION_GATT
 if (memory_enabled && asr_calls == 1 && !strcmp(input, "hello"))
   assert(h->count == 1 && !strcmp(h->turns[0].assistant, "remembered"));
#endif
 if(failed_stage==2)return stage_error;
 assert(cap>6);strcpy(text,"reply");return 0;}
#ifdef CONFIG_BK7258_VISION_SERVICE
int bk7258_vision_capture_jpeg(uint8_t *destination, size_t capacity,
                                size_t *size)
{
 static const uint8_t jpeg[] = {0xff, 0xd8, 0x11, 0x22, 0xff, 0xd9};
 camera_calls++; *size = 0;
 __atomic_store_n(&camera_entered, true, __ATOMIC_RELEASE);
 while (__atomic_load_n(&camera_blocked, __ATOMIC_ACQUIRE)) pause_ms();
 if (camera_error) return camera_error;
 if (capacity < sizeof(jpeg)) return -ENOSPC;
 memcpy(destination, jpeg, sizeof(jpeg)); *size = sizeof(jpeg); return 0;
}
int bkcloud_understand_jpeg(struct bkcloud_client_s *c,
 const struct bkcloud_config_s *cfg,const struct bkvoice_wss_tls_ops_s *ops,
 void *tls,uint64_t due,const char *persona,const struct bkcloud_history_s *h,
 const char *prompt,const uint8_t *jpeg,size_t jpeg_size,char *text,size_t cap)
{(void)c;(void)cfg;(void)ops;(void)tls;(void)due;(void)persona;(void)h;
 assert(!strcmp(prompt,recognized_text) && jpeg_size == 6 && jpeg[0] == 0xff &&
        jpeg[1] == 0xd8 && jpeg[4] == 0xff && jpeg[5] == 0xd9 && cap > 6);
 image_calls++; strcpy(text,"reply"); return 0;}
#endif
int bkcloud_synthesize_turn(struct bkcloud_client_s *c,struct bkcloud_tts_s *d,
 struct bkcloud_playback_s *p,struct bkvoice_turn_s *turn,const struct bkcloud_config_s *cfg,
 const struct bkvoice_wss_tls_ops_s *ops,void *tls,uint64_t due,uint64_t (*now)(void *),void *clock,const char *text)
{
 (void)c;(void)d;(void)p;(void)cfg;(void)ops;(void)tls;(void)due;(void)now;(void)clock;
 assert(!strcmp(text,"reply"));tts_calls++;__atomic_store_n(&entered,true,__ATOMIC_RELEASE);
 while(__atomic_load_n(&blocked,__ATOMIC_ACQUIRE))pause_ms();
 if(__atomic_load_n(&interrupted,__ATOMIC_ACQUIRE))return -ECANCELED;
 if(failed_stage==3)return stage_error;
 turn->state=BKVOICE_TURN_DRAINING;turn->deadline_ms=bkvoice_config_now_ms(NULL)+30000;return 0;
}
void bkcloud_tts_clear(struct bkcloud_tts_s *p) {memset(p,0,sizeof(*p));}
int bkcloud_history_commit(struct bkcloud_history_s *h,const char *u,const char *a)
{assert(!strcmp(u,recognized_text) && !strcmp(a,"reply"));strcpy(h->turns[h->count].user,u);strcpy(h->turns[h->count].assistant,a);h->count++;commits++;return 0;}
static int auto_prefill_read(void *context, size_t frame_index,
                             uint8_t pcm[BKVOICE_CAPTURE_FRAME_BYTES])
{
 unsigned int *calls = context;
 assert(frame_index < 2);
 (*calls)++;
 memset(pcm, (int)(frame_index + 1), BKVOICE_CAPTURE_FRAME_BYTES);
 return 0;
}
int main(void)
{
 struct bkcloud_runtime_status_s absent;
 bkcloud_runtime_status(NULL, &absent);
 assert(!absent.ready && !absent.busy && absent.turn_state == UINT32_MAX);
#ifdef CONFIG_BK7258_PROVISION_GATT
 owner_thread = pthread_self(); memory_enabled = getenv("SHANIU_MEMORY_DISABLED") == NULL;
 memory_policy_enabled = memory_enabled;
 __atomic_store_n(&memory_blocked, true, __ATOMIC_RELEASE);
#endif
 uint8_t record[256]={0};memcpy(record,"CCF1",4);record[4]=2;record[6]=1;record[7]=187;
 const char *fields[]={"cloud.example","/v1","test-only-key","asr","chat","tts"};size_t n=24;
 for(int i=0;i<6;i++){size_t k=strlen(fields[i]);record[9+2*i]=k;memcpy(record+n,fields[i],k);n+=k;}
 struct bkvoice_config_s trust={.initialized=true,.port=443};strcpy(trust.host,"cloud.example");
 struct bkvoice_ptt_s ptt={0};struct bkcloud_runtime_s *r=NULL;
 assert(bkcloud_runtime_create(&r,record,n,&trust,&ptt,NULL)==0);
 dns_fail=true;
 assert(bkcloud_runtime_connect(r)==0);
 int ready=0;for(int i=0;i<1000 && !ready;i++){ready=bkcloud_runtime_ready(r);pause_ms();}
 assert(ready==-EAGAIN && !ptt.capture_ready && asr_calls==0);
 dns_fail=false;
 assert(bkcloud_runtime_connect(r)==0);
 ready=0;for(int i=0;i<1000 && !ready;i++){ready=bkcloud_runtime_ready(r);pause_ms();}
 assert(ready==1 && ptt.capture_ready);
#ifdef CONFIG_BK7258_PROVISION_GATT
 uint8_t memory_owner[32] = {42}, different_owner[32] = {43};
 assert(memory_loads == 0 && memory_reads == 0); /* Probe cannot access memories. */
 assert(bkcloud_runtime_memory_owner(r, memory_owner) == 0);
 for (int i = 0; i < 1000 && bkcloud_runtime_busy(r); i++)
   { bkcloud_runtime_step(r, true, false, 1); pause_ms(); }
 assert(!bkcloud_runtime_busy(r) && memory_loads == 1 && memory_reads == 0);
#endif
 struct bkcloud_runtime_status_s snapshot;
 bkcloud_runtime_status(r,&snapshot);
 assert(snapshot.ready && !snapshot.busy && !snapshot.worker_active);
 bkcloud_runtime_step(r,true,false,1);bkcloud_runtime_step(r,true,false,1);
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<50;i++){bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(asr_calls==1 && commits==0 && bkcloud_runtime_busy(r));
 assert(bkcloud_runtime_clear_history(r)==-EBUSY);
 playback_ready=true;bkcloud_runtime_step(r,true,false,1);
#ifdef CONFIG_BK7258_PROVISION_GATT
 if (memory_enabled)
   {
     for (int i = 0; i < 1000 && !__atomic_load_n(&memory_entered, __ATOMIC_ACQUIRE); i++) pause_ms();
     assert(__atomic_load_n(&memory_entered, __ATOMIC_ACQUIRE));
     assert(bkcloud_runtime_busy(r) && bkcloud_runtime_clear_history(r) == -EBUSY);
     assert(bkcloud_runtime_memory_owner(r, memory_owner) == 0);
     assert(bkcloud_runtime_memory_owner(r, different_owner) == -EBUSY);
     assert(bkcloud_runtime_clear(&r) == -EAGAIN && r != NULL);
     __atomic_store_n(&memory_blocked, false, __ATOMIC_RELEASE);
     for (int i = 0; i < 1000 && bkcloud_runtime_busy(r); i++)
       { bkcloud_runtime_step(r, true, false, 1); pause_ms(); }
     assert(memory_reads == 1 && memory_writes == 1);
   }
 else assert(memory_loads == 1 && memory_reads == 0 && memory_writes == 0);
#endif
 assert(commits==1 && !bkcloud_runtime_busy(r));
 assert(bkcloud_runtime_clear_history(r)==0 && cleared_nonempty_history==1);
 assert(bkcloud_runtime_clear_history(r)==0 && cleared_nonempty_history==1);
 /* App cancellation while the real PTT path owns capture must not upload
  * that buffer or retrigger while the physical key is still held. */
 bkcloud_runtime_step(r,true,true,1);
 assert(bkcloud_runtime_cancel_drain(r)==0);
 assert(!bkcloud_runtime_busy(r) && ptt.turn.state==BKVOICE_TURN_IDLE);
 bkcloud_runtime_step(r,true,true,1);
 assert(!bkcloud_runtime_busy(r) && asr_calls==1 && commits==1);
 bkcloud_runtime_step(r,true,false,1);
 playback_ready=false;
 preferences_fail=true;
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<50;i++){bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(bkcloud_runtime_busy(r));
 ptt.turn.deadline_ms=0;
 bkcloud_runtime_step(r,true,false,1);
 assert(commits==1 && !bkcloud_runtime_busy(r) && ptt.turn.last_error==-ETIMEDOUT);
 bkcloud_runtime_step(r,true,false,1);
 __atomic_store_n(&blocked,true,__ATOMIC_RELEASE);__atomic_store_n(&entered,false,__ATOMIC_RELEASE);
 preferences_fail=false;desired_persona=BK7258_PERSONA_PLAYFUL;expected_style="语气活泼";
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<1000 && !__atomic_load_n(&entered,__ATOMIC_ACQUIRE);i++)pause_ms();
 assert(__atomic_load_n(&entered,__ATOMIC_ACQUIRE));
 bkcloud_runtime_status(r,&snapshot);
 assert(snapshot.ready && snapshot.busy && snapshot.worker_active &&
        snapshot.turn_state==UINT32_MAX);
 assert(bkcloud_runtime_clear_history(r)==-EBUSY);
 assert(bkcloud_runtime_cancel(r)==0);
 bkcloud_runtime_step(r,true,false,1);
 assert(bkcloud_runtime_busy(r) && r!=NULL);
 __atomic_store_n(&blocked,false,__ATOMIC_RELEASE);
 for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++){bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(!bkcloud_runtime_busy(r) && ptt.capture_ready && commits==1);
#ifdef CONFIG_BK7258_PROVISION_GATT
 for (unsigned op = 0; op < 3; op++)
   {
     assert(bkcloud_runtime_memory_set(r, op == 1, op == 2) == 0);
     bkcloud_runtime_status(r, &snapshot);
     assert(snapshot.memory_supported && snapshot.memory_pending && !snapshot.memory_known);
     for (int i = 0; i < 1000 && bkcloud_runtime_busy(r); i++)
       { bkcloud_runtime_step(r, true, false, 1); pause_ms(); }
     bkcloud_runtime_status(r, &snapshot);
     assert(!snapshot.memory_pending && snapshot.memory_known && !snapshot.memory_failed);
     assert(snapshot.memory_enabled == (op == 1));
   }
 memory_policy_error = -EINPROGRESS;
 assert(bkcloud_runtime_memory_set(r, true, false) == 0);
 for (int i = 0; i < 1000 && bkcloud_runtime_busy(r); i++)
   { bkcloud_runtime_step(r, true, false, 1); pause_ms(); }
 bkcloud_runtime_status(r, &snapshot);
 assert(snapshot.memory_failed && !snapshot.memory_known && !snapshot.memory_pending);
 assert(bkcloud_runtime_memory_set(r, false, false) == -EINPROGRESS && memory_mutations == 4);
 /* Rebinding must not turn same-process readback into durable confirmation. */
 unsigned loads_before_rebind = memory_loads;
 assert(bkcloud_runtime_memory_owner(r, different_owner) == 0);
 for (int i = 0; i < 1000 && bkcloud_runtime_busy(r); i++)
   { bkcloud_runtime_step(r, true, false, 1); pause_ms(); }
 bkcloud_runtime_status(r, &snapshot);
 assert(snapshot.memory_failed && !snapshot.memory_known && memory_loads == loads_before_rebind);
 assert(bkcloud_runtime_memory_set(r, true, false) == -EINPROGRESS);
#endif
 int ret=-EAGAIN;for(int i=0;i<1000 && ret==-EAGAIN;i++){ret=bkcloud_runtime_clear(&r);pause_ms();}
 assert(ret==0 && r==NULL && commits==1 && !ptt.capture_ready);
 assert(dns_calls==5);
 __atomic_store_n(&dns_blocked,true,__ATOMIC_RELEASE);
 __atomic_store_n(&dns_entered,false,__ATOMIC_RELEASE);
 assert(bkcloud_runtime_create(&r,record,n,&trust,&ptt,NULL)==0);
 assert(bkcloud_runtime_connect(r)==0);
 for(int i=0;i<1000 && !__atomic_load_n(&dns_entered,__ATOMIC_ACQUIRE);i++)pause_ms();
 assert(__atomic_load_n(&dns_entered,__ATOMIC_ACQUIRE));
 assert(bkcloud_runtime_clear(&r)==-EAGAIN && r!=NULL);
 __atomic_store_n(&dns_blocked,false,__ATOMIC_RELEASE);
 ret=-EAGAIN;for(int i=0;i<1000 && ret==-EAGAIN;i++){ret=bkcloud_runtime_clear(&r);pause_ms();}
 assert(ret==0 && !r && dns_calls==6 && commits==1 && asr_calls==3);
 assert(cleared_nonempty_history==1);
 /* Inject only the cloud stage error: production runtime owns worker
  * lifetime, cancellation, status and the decision to commit history.
  */
 const int failures[] = {-EACCES, -ETIMEDOUT};
 playback_ready=true;
 for(unsigned e=0;e<sizeof(failures)/sizeof(failures[0]);e++)
   for(int stage=1;stage<=3;stage++)
     {
       assert(bkcloud_runtime_create(&r,record,n,&trust,&ptt,NULL)==0);
       assert(bkcloud_runtime_connect(r)==0);
       ready=0;
       for(int i=0;i<1000 && !ready;i++){ready=bkcloud_runtime_ready(r);pause_ms();}
       assert(ready==1);
       bkcloud_runtime_step(r,true,false,1);
       bkcloud_runtime_step(r,true,false,1);
       expected_history=0;
       bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
       for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
         {bkcloud_runtime_step(r,true,false,1);pause_ms();}
       assert(!bkcloud_runtime_busy(r) && ptt.turn.last_error==0);
       expected_history=1;
       int before=commits;
       failed_stage=stage;stage_error=failures[e];
       bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
       for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
         {bkcloud_runtime_step(r,true,false,1);pause_ms();}
       bkcloud_runtime_status(r,&snapshot);
       assert(snapshot.ready && !snapshot.busy && !snapshot.worker_active);
       assert(snapshot.last_error==failures[e] && commits==before);
       assert(ptt.turn.state==BKVOICE_TURN_IDLE && ptt.capture_ready);
       failed_stage=0;
       bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
       for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
         {bkcloud_runtime_step(r,true,false,1);pause_ms();}
       bkcloud_runtime_status(r,&snapshot);
       assert(snapshot.ready && !snapshot.busy && snapshot.last_error==0);
       assert(commits==before+1 && ptt.turn.state==BKVOICE_TURN_IDLE);
       expected_history=-1;
       assert(bkcloud_runtime_clear(&r)==0 && r==NULL);
     }
#ifdef CONFIG_BK7258_VISION_SERVICE
 /* The ASR text must be an exact consent phrase before a camera frame can
  * reach the image client. History still waits for the normal TTS drain. */
 assert(bkcloud_runtime_create(&r,record,n,&trust,&ptt,NULL)==0);
 assert(bkcloud_runtime_connect(r)==0);
 ready=0;
 for(int i=0;i<1000 && !ready;i++){ready=bkcloud_runtime_ready(r);pause_ms();}
 assert(ready==1);
 int before_capture=camera_calls, before_image=image_calls, before_chat=chat_calls;
 int before_tts=tts_calls, before_commit=commits;
 recognized_text=" 拍照看看！ "; playback_ready=false;
 __atomic_store_n(&blocked,true,__ATOMIC_RELEASE);
 __atomic_store_n(&entered,false,__ATOMIC_RELEASE);
 bkcloud_runtime_step(r,true,false,1);
 bkcloud_runtime_step(r,true,false,1);
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<1000 && !__atomic_load_n(&entered,__ATOMIC_ACQUIRE);i++)pause_ms();
 assert(__atomic_load_n(&entered,__ATOMIC_ACQUIRE));
 assert(camera_calls==before_capture+1 && image_calls==before_image+1 &&
        chat_calls==before_chat && tts_calls==before_tts+1 && commits==before_commit);
 __atomic_store_n(&blocked,false,__ATOMIC_RELEASE);
 for(int i=0;i<100;i++){bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(commits==before_commit); /* Playback remains the history gate. */
 playback_ready=true;
 for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
   {bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(commits==before_commit+1);

 before_capture=camera_calls; before_image=image_calls; before_chat=chat_calls;
 before_tts=tts_calls; before_commit=commits;
 recognized_text="普通问题";
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
   {bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(camera_calls==before_capture && image_calls==before_image &&
        chat_calls==before_chat+1 && tts_calls==before_tts+1 &&
        commits==before_commit+1);

 before_capture=camera_calls; before_image=image_calls; before_chat=chat_calls;
 before_tts=tts_calls; before_commit=commits;
 recognized_text="不要拍照看看";
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
   {bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(camera_calls==before_capture && image_calls==before_image &&
        chat_calls==before_chat+1 && tts_calls==before_tts+1 &&
        commits==before_commit+1);

 before_image=image_calls; before_chat=chat_calls; before_tts=tts_calls;
 before_commit=commits; recognized_text="看看眼前"; camera_error=-EIO;
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
   {bkcloud_runtime_step(r,true,false,1);pause_ms();}
 assert(image_calls==before_image && chat_calls==before_chat &&
        tts_calls==before_tts && commits==before_commit);
 camera_error=0;

 before_image=image_calls; before_tts=tts_calls; before_commit=commits;
 recognized_text="拍照看看";
 __atomic_store_n(&camera_entered,false,__ATOMIC_RELEASE);
 __atomic_store_n(&camera_blocked,true,__ATOMIC_RELEASE);
 bkcloud_runtime_step(r,true,true,1);bkcloud_runtime_step(r,true,false,1);
 for(int i=0;i<1000 && !__atomic_load_n(&camera_entered,__ATOMIC_ACQUIRE);i++)pause_ms();
 assert(__atomic_load_n(&camera_entered,__ATOMIC_ACQUIRE));
 assert(bkcloud_runtime_cancel_drain(r)==-EAGAIN);
 __atomic_store_n(&camera_blocked,false,__ATOMIC_RELEASE);
 for(int i=0;i<1000 && bkcloud_runtime_busy(r);i++)
   {
     int drain = bkcloud_runtime_cancel_drain(r);
     assert(drain == 0 || drain == -EAGAIN);
     pause_ms();
   }
 assert(image_calls==before_image && tts_calls==before_tts && commits==before_commit);
 assert(bkcloud_runtime_clear(&r)==0 && r==NULL);
#endif
 /* Automatic capture begins through prefill rather than a synthesized PTT
  * level.  A neutral button step must leave that capture running. */
 assert(bkcloud_runtime_create(&r,record,n,&trust,&ptt,NULL)==0);
 assert(bkcloud_runtime_connect(r)==0);
 ready=0;
 for (int i = 0; i < 1000 && !ready; i++)
   { ready=bkcloud_runtime_ready(r); pause_ms(); }
 assert(ready==1);
 unsigned int prefill_reads = 0;
 int asr_before_auto = asr_calls;
 int commits_before_auto = commits;
 bkcloud_runtime_step(r, true, false, 1);
 assert(bkcloud_runtime_auto_begin(r, auto_prefill_read, &prefill_reads, 2,
                                   NULL, NULL) == 0);
 assert(prefill_reads == 2 && auto_prefill_calls == 2 &&
        ptt.worker_joinable && bkcloud_runtime_busy(r));
 bkcloud_runtime_step(r, true, false, 1);
 assert(ptt.worker_joinable && asr_calls == asr_before_auto);
 assert(bkcloud_runtime_auto_end(r) == 0);
 assert(bkcloud_runtime_auto_end(r) == -EPERM);
 for (int i = 0; i < 1000 && bkcloud_runtime_busy(r); i++)
   { bkcloud_runtime_step(r, true, false, 1); pause_ms(); }
 assert(asr_calls == asr_before_auto + 1 && commits == commits_before_auto + 1);
 /* Cancellation uses the ordinary stop/join cleanup even for auto capture. */
 assert(bkcloud_runtime_auto_begin(r, auto_prefill_read, &prefill_reads, 2,
                                   NULL, NULL) == 0);
 assert(bkcloud_runtime_cancel(r) == 0);
 bkcloud_runtime_step(r, true, false, 1);
 assert(!bkcloud_runtime_busy(r) && ptt.turn.state == BKVOICE_TURN_IDLE &&
        asr_calls == asr_before_auto + 1);
 /* A failed prefill can still leave the capture worker borrowing PCM. It is
  * fail-closed: end cannot upload it, and neutral steps retry cancel/join. */
 auto_down_prefill_error = -EIO;
 assert(bkcloud_runtime_auto_begin(r, auto_prefill_read, &prefill_reads, 2,
                                   NULL, NULL) == -EIO);
 assert(ptt.worker_joinable && bkcloud_runtime_busy(r));
 int asr_before_prefill_failure = asr_calls;
 assert(bkcloud_runtime_auto_end(r) == -ECANCELED);
 auto_cancel_leave_worker = true;
 bkcloud_runtime_step(r, true, false, 1);
 assert(ptt.worker_joinable && bkcloud_runtime_busy(r) &&
        asr_calls == asr_before_prefill_failure);
 auto_cancel_leave_worker = false;
 bkcloud_runtime_step(r, true, false, 1);
 assert(!bkcloud_runtime_busy(r) && ptt.turn.state == BKVOICE_TURN_IDLE &&
        asr_calls == asr_before_prefill_failure);
 auto_down_prefill_error = 0;
 /* A pending capture join leaves automatic state active; retrying end() after
  * the join completes launches exactly once. */
 assert(bkcloud_runtime_auto_begin(r, auto_prefill_read, &prefill_reads, 2,
                                   NULL, NULL) == 0);
 int asr_before_join_retry = asr_calls;
 auto_up_leave_worker = true;
 assert(bkcloud_runtime_auto_end(r) == -EAGAIN);
 assert(ptt.worker_joinable && bkcloud_runtime_busy(r));
 bkcloud_runtime_step(r, true, false, 1);
 assert(ptt.worker_joinable && asr_calls == asr_before_join_retry);
 auto_up_leave_worker = false;
 assert(bkcloud_runtime_auto_end(r) == 0);
 assert(bkcloud_runtime_auto_end(r) == -EPERM);
 for (int i = 0; i < 1000 && bkcloud_runtime_busy(r); i++)
   { bkcloud_runtime_step(r, true, false, 1); pause_ms(); }
 assert(asr_calls == asr_before_join_retry + 1);
 /* Link loss and a new input epoch cancel automatic capture before the
  * regular button-level path can release it. */
 int asr_before_link_cancel = asr_calls;
 assert(bkcloud_runtime_auto_begin(r, auto_prefill_read, &prefill_reads, 2,
                                   NULL, NULL) == 0);
 bkcloud_runtime_step(r, false, false, 1);
 assert(!bkcloud_runtime_busy(r) && ptt.turn.state == BKVOICE_TURN_IDLE &&
        asr_calls == asr_before_link_cancel);
 bkcloud_runtime_step(r, true, false, 1);
 assert(bkcloud_runtime_auto_begin(r, auto_prefill_read, &prefill_reads, 2,
                                   NULL, NULL) == 0);
 bkcloud_runtime_step(r, true, false, 2);
 assert(!bkcloud_runtime_busy(r) && ptt.turn.state == BKVOICE_TURN_IDLE &&
        asr_calls == asr_before_link_cancel);
 assert(bkcloud_runtime_clear(&r)==0 && r==NULL);
 puts("PASS: ASR/chat/TTS auth and timeout failures preserve history and allow the next turn");
 puts("PASS: DNS failure/retry/address refresh, simulated PTT, deferred history, playback timeout, join-before-clear");
 return 0;
}
