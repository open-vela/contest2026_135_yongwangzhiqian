#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the patched upstream GATT notification path with bounded fake peers."""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NUTTX = ROOT.parent / 'nuttx'
PATCH = ROOT / 'nuttx/patches/bluetooth/0001-gatt-report-notification-enqueue-result.patch'
BASELINE = '76354c637858ecb0aa4601629327acb6f44a26bb'


def function(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


PREFIX = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define FAR
#define BT_UUID_16 16
#define BT_UUID_GATT_CCC 0x2902
#define BT_UUID_GATT_CHRC 0x2803
#define BT_GATT_ITER_STOP 0
#define BT_GATT_ITER_CONTINUE 1
#define BT_GATT_CCC_NOTIFY 1
#define BT_CONN_CONNECTED 1
#define BT_ATT_OP_NOTIFY 0x1b
#define BT_L2CAP_CID_ATT 4
#define BT_HOST2LE16(x) (x)
#define wlwarn(...) ((void)0)
#define wlinfo(...) ((void)0)
struct bt_uuid_s { int type; union { int value; }; };
struct cfg { int peer; int value; };
struct _bt_gatt_ccc_s { struct cfg *cfg; size_t cfg_len; int value; };
struct bt_gatt_attr_s { struct bt_uuid_s *uuid; int (*write)(void); void *user_data; };
struct bt_conn_s { int state; };
struct bt_buf_s { uint8_t data[64]; size_t len; };
struct bt_att_notify_s { uint16_t handle; uint8_t value[]; };
static struct cfg configs[2] = {{0,1},{1,1}};
static struct _bt_gatt_ccc_s ccc = {configs, 2, 1};
static struct bt_uuid_s uuid = {16, {BT_UUID_GATT_CCC}};
static int bt_gatt_attr_write_ccc(void) { return 0; }
static struct bt_gatt_attr_s attr = {&uuid, bt_gatt_attr_write_ccc, &ccc};
static struct bt_conn_s connections[2] = {{1},{1}};
static struct bt_buf_s buffer;
static int sent, released, allocations, fail_at;
static uint16_t observed_handle;
static int bt_uuid_cmp(const struct bt_uuid_s *a,const struct bt_uuid_s *b) { return a->value != b->value; }
static struct bt_conn_s *bt_conn_lookup_addr_le(const int *peer) { return &connections[*peer]; }
static void bt_conn_release(struct bt_conn_s *conn) { (void)conn; released++; }
static struct bt_buf_s *bt_att_create_pdu(struct bt_conn_s *conn, int op, size_t len) {
 (void)conn; assert(op == BT_ATT_OP_NOTIFY); assert(len <= sizeof(buffer.data));
 if (++allocations == fail_at) return NULL;
 memset(&buffer,0,sizeof(buffer)); return &buffer;
}
static void *bt_buf_extend(struct bt_buf_s *buf,size_t length) {
 assert(buf->len+length <= sizeof(buf->data)); void *p=buf->data+buf->len; buf->len+=length; return p;
}
static void bt_l2cap_send(struct bt_conn_s *conn,int cid,struct bt_buf_s *buf) {
 (void)conn; assert(cid == BT_L2CAP_CID_ATT); sent++;
 observed_handle=((struct bt_att_notify_s *)buf->data)->handle;
 if (buf->len > 2) assert(buf->data[2] == 42);
}
static void bt_gatt_foreach_attr(uint16_t first,uint16_t last,
 uint8_t (*callback)(const struct bt_gatt_attr_s *,void *),void *context) {
 assert(first == 0x1234 && last == 0xffff); callback(&attr, context);
}
'''

TEST = r'''
int main(void) {
 uint8_t value=42;
 assert(bt_gatt_notify_checked(0,NULL,0) == -EINVAL);
 assert(bt_gatt_notify_checked(0x1234,&value,1) == 2);
 assert(sent == 2 && released == 2 && observed_handle == 0x1234);
 sent=released=allocations=0; fail_at=1;
 assert(bt_gatt_notify_checked(0x1234,&value,1) == -ENOMEM);
 assert(sent == 0 && released == 1);
 sent=released=allocations=0; fail_at=2;
 assert(bt_gatt_notify_checked(0x1234,&value,1) == 1);
 assert(sent == 1 && released == 2);
 sent=released=allocations=0; fail_at=0;
 configs[0].value=0; connections[1].state=0;
 assert(bt_gatt_notify_checked(0x1234,&value,1) == -ENOTCONN);
 assert(sent == 0 && allocations == 0 && released == 1);
 connections[1].state=1;
 assert(bt_gatt_notify_checked(0x1234,&value,1) == 1);
 assert(sent == 1);
 bt_gatt_notify(0x1234,&value,1);
 assert(sent == 2);
 assert(bt_gatt_notify_checked(0x1234,NULL,0) == 1);
 assert(sent == 3);
 assert(bt_gatt_notify_peer(NULL,0x1234,&value,1) == -EINVAL);
 assert(bt_gatt_notify_peer(&connections[0],0x1234,&value,1) == -ENOTCONN);
 assert(sent == 3);
 assert(bt_gatt_notify_peer(&connections[1],0x1234,&value,1) == 1);
 assert(sent == 4);
 /* Old retained connection must not broadcast to its live replacement. */
 connections[1].state=0; connections[0].state=1; configs[0].value=1;
 assert(bt_gatt_notify_peer(&connections[1],0x1234,&value,1) == -ENOTCONN);
 assert(sent == 4);
 return 0;
}
'''


class NotifyResultTest(unittest.TestCase):
    def test_actual_patched_functions(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            for name in ('wireless/bluetooth/bt_gatt.c', 'include/nuttx/wireless/bluetooth/bt_gatt.h'):
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(subprocess.check_output(['git', '-C', str(NUTTX), 'show', f'{BASELINE}:{name}']))
            subprocess.run(['git', 'apply', str(PATCH)], cwd=tree, check=True)
            source = (tree / 'wireless/bluetooth/bt_gatt.c').read_text()
            struct = re.search(r'struct notify_data_s\s*\{.*?\};', source, re.S).group()
            bodies = '\n'.join(function(source, marker) for marker in (
                'static uint8_t notify_cb(', 'static int gatt_notify_common(',
                'int bt_gatt_notify_checked(', 'int bt_gatt_notify_peer(', 'void bt_gatt_notify('))
            test = tree / 'test.c'
            test.write_text(PREFIX + struct + bodies + TEST)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(test), '-o', str(tree / 'test')], check=True)
            subprocess.run([str(tree / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
