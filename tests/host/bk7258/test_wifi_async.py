#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise actual AP submit/poll/cancel entry points without the SDK worker."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PREFIX = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define BK7258_WIFI_SSID_MAX_LEN 32
#define BK7258_WIFI_PASSWORD_MAX_LEN 64
#define BK7258_WIFI_PASSWORD_MIN_LEN 8
#define BK7258_WIFI_CONNECT_MIN_MS 5000
#define BK7258_WIFI_CONNECT_MAX_MS 60000
#define BK7258_WIFI_OPERATION_CONNECT 1
#define BK7258_WIFI_OPERATION_SCAN 10
#define BK7258_WIFI_SCAN_MIN_MS 1000
#define BK7258_WIFI_CHANNEL_STATS_MAX 13u
#define OK 0
struct bk7258_wifi_monitor_result_s {int status;unsigned channel,frame_count;};
struct bk7258_wifi_channel_stats_s {int status;unsigned returned,dwell_ms;struct bk7258_wifi_monitor_result_s channels[13];};
struct bk7258_wifi_scan_result_s { int status; unsigned returned; };
struct bk7258_wifi_result_s {int status;uint32_t link_state;};
#define BK7258_WIFI_LINK_CONNECTED 2
struct bk7258_wifi_control_wire_s {int operation;uint32_t timeout_ms;size_t ssid_len,password_len;
 char ssid[33],password[65];};
struct bk7258_wifi_control_dev_s {
 bool initialized,busy,local_pending,local_done,local_cancel,request_local,local_scan,local_channels,local_monitor_cleanup;
 struct bk7258_wifi_channel_stats_s local_channel_result;
 struct bk7258_wifi_scan_result_s local_scan_result;
 bool trial_active,trial_touched,trial_connected,trial_restore_failed,saved_valid,trial_was_connected;
 uint32_t trial_lease;
 struct bk7258_wifi_control_wire_s saved_connection,trial_connection;
 uint32_t local_ticket;int request_sem;struct bk7258_wifi_control_wire_s request;
 struct bk7258_wifi_result_s local_result;
};
static struct bk7258_wifi_control_dev_s g_bk7258_wifi_control;
static int g_bk7258_wifi_local_lock, post_result, posts;
static int nxmutex_lock(int *p) {(void)p;return 0;}
static void nxmutex_unlock(int *p) {(void)p;}
static int nxsem_post(int *p) {(void)p;posts++;return post_result;}
static int connect_result, current_link, connect_calls;
static char observed_ssid[33];
static int bk7258_wifi_read_link(struct bk7258_wifi_result_s *r) { r->link_state=current_link;return 0; }
static int bk7258_wifi_connect(const struct bk7258_wifi_control_wire_s *q,struct bk7258_wifi_result_s *r) {
 connect_calls++;strcpy(observed_ssid,q->ssid);r->link_state=connect_result?0:2;return connect_result;
}
static int bk7258_wifi_stop_sta(void) { current_link=0;return 0; }
static int bk7258_wifi_sync_native_link(struct bk7258_wifi_result_s *r) { r->link_state=current_link;return 0; }
static struct {unsigned active;} g_bk7258_wifi_monitor;
static int monitor_starts,monitor_stops,monitor_stop_error,cancel_dwell;
static int bk7258_wifi_monitor_start_capture(unsigned channel) {(void)channel;monitor_starts++;return 0;}
static int bk7258_wifi_monitor_stop_capture(void) {monitor_stops++;return monitor_stop_error;}
static void bk7258_wifi_monitor_snapshot(struct bk7258_wifi_monitor_result_s *r) {r->frame_count=3;}
static void nxsig_usleep(unsigned t) {(void)t;if(cancel_dwell)g_bk7258_wifi_control.local_cancel=true;}
'''
TEST = r'''
static void finish_worker(void) {
 struct bk7258_wifi_control_dev_s *p=&g_bk7258_wifi_control;
 p->local_result.status=bk7258_wifi_trial_run(p,&p->request,&p->local_result);
 p->busy=p->trial_active;p->local_done=true;
}
int main(void) {
 struct bk7258_wifi_control_dev_s *p=&g_bk7258_wifi_control;
 struct bk7258_wifi_result_s result;
 char password[]="test-only-pass";uint32_t first, second;
 assert(bk7258_wifi_connect_async("lab",password,5000,&first)==-EAGAIN);
 p->initialized=true;
 assert(bk7258_wifi_connect_async("lab",password,4999,&first)==-EINVAL);
 struct bk7258_wifi_channel_stats_s channels;
 assert(bk7258_wifi_channels_async(99,&first)==-EINVAL);
 assert(bk7258_wifi_channels_async(300,&first)==0);
 assert(bk7258_wifi_channels_poll(first,&channels)==-EAGAIN);
 assert(bk7258_wifi_channels_run(300)==0);
 assert(monitor_starts==13 && monitor_stops==13);
 p->local_done=true;p->busy=false;
 assert(bk7258_wifi_channels_poll(first,&channels)==0 && channels.returned==13);
 assert(bk7258_wifi_channels_async(300,&first)==0);
 cancel_dwell=1;
 assert(bk7258_wifi_channels_run(300)==-ECANCELED);
 assert(monitor_starts==14 && monitor_stops==14);
 p->local_done=true;p->busy=false;
 assert(bk7258_wifi_channels_poll(first,&channels)==0);
 cancel_dwell=0;current_link=2;
 assert(bk7258_wifi_channels_run(300)==-EBUSY);
 current_link=0;posts=0;
 p->local_cancel=false;monitor_stop_error=-EIO;
 assert(bk7258_wifi_channels_run(300)==-EIO && p->local_monitor_cleanup);
 monitor_stop_error=0;
 assert(bk7258_wifi_channels_run(300)==0 && !p->local_monitor_cleanup);
 struct bk7258_wifi_scan_result_s scan;
 assert(bk7258_wifi_scan_async(0,&first)==-EINVAL);
 assert(bk7258_wifi_scan_async(15000,&first)==0);
 assert(p->busy && p->local_scan && posts==1);
 assert(bk7258_wifi_scan_poll(first,&scan)==-EAGAIN);
 assert(bk7258_wifi_connect_poll(first,&result)==-ESTALE);
 assert(bk7258_wifi_connect_async("lab",password,5000,&second)==-EBUSY);
 p->local_scan_result.returned=3;p->local_done=true;p->busy=false;
 assert(bk7258_wifi_scan_poll(first+1,&scan)==-ESTALE);
 assert(bk7258_wifi_scan_poll(first,&scan)==0 && scan.returned==3);
 assert(bk7258_wifi_scan_poll(first,&scan)==-ESTALE);
 post_result=-EIO;
 assert(bk7258_wifi_scan_async(15000,&first)==-EIO);
 assert(!p->busy && !p->local_pending);
 post_result=0;posts=0;
 p->busy=true; // The existing CP request currently owns the same worker.
 assert(bk7258_wifi_connect_async("lab",password,5000,&first)==-EBUSY);
 p->busy=false;
 assert(bk7258_wifi_connect_async("lab",password,5000,&first)==0);
 assert(posts==1 && first!=0 && p->busy && p->request_local);
 memset(password,0,sizeof(password));
 assert(strcmp(p->request.password,"test-only-pass")==0);
 assert(bk7258_wifi_connect_async("other","",5000,&second)==-EBUSY);
 assert(bk7258_wifi_connect_poll(first+1,&result)==-ESTALE);
 assert(bk7258_wifi_connect_poll(first,&result)==-EAGAIN);
 assert(bk7258_wifi_connect_cancel(first+1)==-ESTALE && !p->local_cancel);
 assert(bk7258_wifi_connect_cancel(first)==0 && bk7258_wifi_local_cancelled());
 p->request_local=false;assert(!bk7258_wifi_local_cancelled());
 // Publish a worker completion; stale clients must not consume it.
 p->local_result.status=-ECANCELED;p->local_done=true;p->busy=false;
 assert(bk7258_wifi_connect_cancel(first)==-EALREADY);
 assert(bk7258_wifi_connect_poll(first+1,&result)==-ESTALE);
 assert(bk7258_wifi_connect_poll(first,&result)==0 && result.status==-ECANCELED);
 assert(!p->local_pending && p->local_result.status==0);
 assert(bk7258_wifi_connect_poll(first,&result)==-ESTALE);
 post_result=-EIO;
 assert(bk7258_wifi_connect_async("next","",5000,&second)==-EIO);
 assert(!p->busy && !p->local_pending);
 for(size_t i=0;i<sizeof(p->request);i++)assert(((unsigned char *)&p->request)[i]==0);
 post_result=0;
 assert(bk7258_wifi_connect_async("next","",5000,&second)==0 && second>first);
 assert(bk7258_wifi_connect_poll(first,&result)==-ESTALE);
 p->local_pending=false;p->busy=false;p->local_ticket=UINT32_MAX;
 assert(bk7258_wifi_connect_async("next","",5000,&second)==-EOVERFLOW);
 p->local_ticket=0;
 uint32_t lease, done;
 assert(bk7258_wifi_trial_start("original","",5000,&lease)==0);
 assert(p->trial_active && p->busy);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==-EBUSY);
 finish_worker();assert(p->busy);
 assert(bk7258_wifi_connect_poll(lease,&result)==0 && result.status==0);
 assert(bk7258_wifi_connect_async("interfere","",5000,&second)==-EBUSY);
 assert(bk7258_wifi_trial_finish(lease+1,true,&done)==-ESTALE);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==0);
 finish_worker();assert(!p->busy && p->saved_valid);
 assert(bk7258_wifi_connect_poll(done,&result)==0 && result.status==0);
 assert(!strcmp(p->saved_connection.ssid,"original"));

 current_link=2;connect_result=-EACCES;
 assert(bk7258_wifi_trial_start("wrong-password","",5000,&lease)==0);
 finish_worker();assert(p->busy && !p->trial_connected);
 assert(bk7258_wifi_connect_poll(lease,&result)==0 && result.status==-EACCES);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==-EPERM);
 assert(bk7258_wifi_trial_finish(lease,false,&done)==0);
 connect_result=-EIO;finish_worker();
 assert(!strcmp(observed_ssid,"original") && p->busy && p->trial_restore_failed);
 assert(bk7258_wifi_connect_poll(done,&result)==0 && result.status==-EIO);
 assert(bk7258_wifi_trial_finish(lease,true,&done)==-EPERM);
 assert(bk7258_wifi_trial_finish(lease,false,&done)==0);
 connect_result=0;finish_worker();
 assert(bk7258_wifi_connect_poll(done,&result)==0 && result.status==0 && !p->busy);

 p->saved_valid=false;int previous_calls=connect_calls;
 assert(bk7258_wifi_trial_start("unknown-old-network","",5000,&lease)==0);
 finish_worker();assert(connect_calls==previous_calls);
 assert(bk7258_wifi_connect_poll(lease,&result)==0 && result.status==-ENOKEY);
 assert(bk7258_wifi_trial_finish(lease,false,&done)==0);
 finish_worker();assert(bk7258_wifi_connect_poll(done,&result)==0 && !p->busy && current_link==2);
 return 0;
}
'''


class WifiAsyncTest(unittest.TestCase):
    def test_scan_cleanup_retains_lease_on_stop_or_unregister_failure(self):
        source = (ROOT / 'chips/bk7258/common/bk7258_wifi_control.c').read_text()
        cleanup = source[source.index('static int bk7258_wifi_scan_cleanup(void)'):
                         source.index('static int bk7258_wifi_scan(uint32_t')]
        harness = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#define OK 0
#define BK7258_WIFI_EVENT_MOD_WIFI 1
#define BK7258_WIFI_EVENT_SCAN_DONE 2
#define BK7258_RADIO_MODE_WIFI_SCAN 3
struct bk7258_wifi_control_dev_s {
 bool scan_cleanup_pending,scan_started,scan_callback_registered;
};
static struct bk7258_wifi_control_dev_s g_bk7258_wifi_control;
static int stop_error,unregister_error,releases,stops,unregisters;
static int bk7258_wifi_vendor_result(int ret){return ret;}
static int bk_wifi_scan_stop(void){stops++;return stop_error;}
static void bk7258_wifi_scan_event(void){}
static int bk_event_unregister_cb(int m,int e,void (*f)(void))
{(void)m;(void)e;(void)f;unregisters++;return unregister_error;}
static int bk7258_radio_mode_release(int m){(void)m;releases++;return 0;}
'''
        test = r'''
int main(void) {
 struct bk7258_wifi_control_dev_s *p=&g_bk7258_wifi_control;
 p->scan_cleanup_pending=p->scan_started=p->scan_callback_registered=true;
 stop_error=-EIO;
 assert(bk7258_wifi_scan_cleanup()==-EIO && p->scan_cleanup_pending);
 assert(releases==0 && unregisters==0);
 stop_error=0;unregister_error=-EIO;
 assert(bk7258_wifi_scan_cleanup()==-EIO && p->scan_cleanup_pending);
 assert(!p->scan_started && p->scan_callback_registered && releases==0);
 int old_stops=stops;unregister_error=0;
 assert(bk7258_wifi_scan_cleanup()==0 && releases==1);
 assert(!p->scan_cleanup_pending && !p->scan_callback_registered && stops==old_stops);
 assert(bk7258_wifi_scan_cleanup()==0 && releases==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(harness + cleanup + test)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_owner_tickets_cancellation_and_failed_enqueue(self):
        source = (ROOT / 'chips/bk7258/common/bk7258_wifi_control.c').read_text()
        helper = source[source.index('static bool bk7258_wifi_password_length_valid'):]
        helper = helper[:helper.index('\n}\n') + 3]
        functions = source[source.index('/* Private worker operations:'):
                           source.index('static int bk7258_wifi_connect(')]
        functions = functions.replace('"dmb sy"', '""')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            trial = source[source.index('static int bk7258_wifi_trial_run('):
                           source.index('static void bk7258_wifi_control_report_immediate(')]
            (path / 'test.c').write_text(PREFIX + helper + functions + trial + TEST)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
