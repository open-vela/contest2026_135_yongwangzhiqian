#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compile the patched upstream GATT notification path with bounded fake peers."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NUTTX = ROOT.parent / 'nuttx'
PATCHES = (
    ROOT / 'nuttx/patches/bluetooth/0001-gatt-report-notification-enqueue-result.patch',
    ROOT / 'nuttx/patches/bluetooth/0003-gatt-ccc-do-not-allocate-unbonded-key-slot.patch',
)
ATT_PATCH = ROOT / 'nuttx/patches/bluetooth/0004-att-cap-mtu-to-receive-buffer.patch'
BASELINE = '76354c637858ecb0aa4601629327acb6f44a26bb'


def function(source, marker, occurrence=0):
    start = -1
    for _ in range(occurrence + 1):
        start = source.index(marker, start + 1)
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

CCC_PREFIX = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define FAR
#define BT_KEYS_SLAVE_LTK 1
#define BT_KEYS_LTK 4
#define BT_LE162HOST(x) (x)
#define wlinfo(...) ((void)0)
#define wlwarn(...) ((void)0)
typedef struct { int value; } bt_addr_le_t;
struct _bt_gatt_ccc_cfg_s { bt_addr_le_t peer; uint16_t value; bool valid; };
struct _bt_gatt_ccc_s {
 struct _bt_gatt_ccc_cfg_s *cfg; size_t cfg_len; uint16_t value;
 uint16_t value_handle;
 void (*cfg_changed)(uint16_t value);
};
struct bt_gatt_attr_s { void *user_data; uint16_t handle; };
struct bt_conn_s { bt_addr_le_t dst; };
static int bonded_ltk_peer;
static int bonded_slave_ltk_peer;
static int changed;
static int bt_addr_le_cmp(const bt_addr_le_t *a, const bt_addr_le_t *b) {
 return a->value != b->value;
}
static void bt_addr_le_copy(bt_addr_le_t *dst, const bt_addr_le_t *src) {
 *dst = *src;
}
static void *bt_keys_find(int type, const bt_addr_le_t *addr) {
 if ((type == BT_KEYS_LTK && addr->value == bonded_ltk_peer) ||
     (type == BT_KEYS_SLAVE_LTK && addr->value == bonded_slave_ltk_peer))
   return (void *)addr;
 return NULL;
}
static void ccc_changed(uint16_t value) { (void)value; changed++; }
'''

CCC_TEST = r'''
int main(void) {
 struct _bt_gatt_ccc_cfg_s configs[1] = {{{0}, 0, false}};
 struct _bt_gatt_ccc_s ccc = {configs, 1, 0, 0, ccc_changed};
 struct bt_gatt_attr_s attr = {&ccc, 0x1234};
 struct bt_conn_s first = {{1}}, second = {{2}}, ltk_peer = {{3}};
 struct bt_conn_s slave_ltk_peer = {{4}}, unbonded = {{5}};
 uint16_t enable = 1;

 /* Two unbonded random peers reuse the transient CCC entry. */
 assert(bt_gatt_attr_write_ccc(&first, &attr, &enable, sizeof(enable), 0) == 2);
 assert(!configs[0].valid && configs[0].peer.value == 1);
 assert(bt_gatt_attr_write_ccc(&second, &attr, &enable, sizeof(enable), 0) == 2);
 assert(!configs[0].valid && configs[0].peer.value == 2);

 /* Either stored LTK form is a real bond and keeps the CCC entry. */
 bonded_ltk_peer = 3;
 assert(bt_gatt_attr_write_ccc(&ltk_peer, &attr, &enable, sizeof(enable), 0) == 2);
 assert(configs[0].valid && configs[0].peer.value == 3);
 assert(bt_gatt_attr_write_ccc(&unbonded, &attr, &enable, sizeof(enable), 0) < 0);
 assert(configs[0].valid && configs[0].peer.value == 3);
 memset(configs, 0, sizeof(configs));
 bonded_slave_ltk_peer = 4;
 assert(bt_gatt_attr_write_ccc(&slave_ltk_peer, &attr, &enable, sizeof(enable), 0) == 2);
 assert(configs[0].valid && configs[0].peer.value == 4);
 return changed == 1 ? 0 : 1;
}
'''

ATT_PREFIX = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define FAR
#define BT_ATT_DEFAULT_LE_MTU 23
#define BT_ATT_MAX_LE_MTU 517
#define BT_ATT_ERR_INVALID_PDU 4
#define BT_ATT_ERR_UNLIKELY 14
#define BT_ATT_OP_MTU_RSP 3
#define BT_L2CAP_CID_ATT 4
#define BLUETOOTH_MAX_MTU 70
#define BT_LE162HOST(x) (x)
#define BT_HOST2LE16(x) (x)
#define wlinfo(...) ((void)0)
struct bt_att_s { uint16_t mtu; };
struct bt_conn_s { struct bt_att_s *att; };
struct bt_buf_s { uint8_t data[8]; size_t len; };
struct bt_att_exchange_mtu_req_s { uint16_t mtu; };
struct bt_att_exchange_mtu_rsp_s { uint16_t mtu; };
static struct bt_buf_s output;
static int sent;
static struct bt_buf_s *bt_att_create_pdu(struct bt_conn_s *conn, int op,
                                           size_t len) {
 (void)conn; (void)op; (void)len; output.len = 1; return &output;
}
static size_t bt_buf_tailroom(struct bt_buf_s *buf) { (void)buf; return 70; }
static void *bt_buf_extend(struct bt_buf_s *buf, size_t len) {
 void *p = buf->data + buf->len; buf->len += len; return p;
}
static void bt_l2cap_send(struct bt_conn_s *conn, int cid,
                          struct bt_buf_s *buf) {
 (void)conn; (void)buf; assert(cid == BT_L2CAP_CID_ATT); sent++;
}
'''

ATT_TEST = r'''
int main(void) {
 struct bt_att_s att = {0}; struct bt_conn_s conn = {&att};
 struct bt_buf_s incoming = {{0}, 0};
 struct bt_att_exchange_mtu_req_s *request = (void *)incoming.data;
 struct bt_att_exchange_mtu_rsp_s *response;
 request->mtu = 185;
 assert(att_mtu_req(&conn, &incoming) == 0);
 response = (void *)(output.data + 1);
 assert(att.mtu == 70 && response->mtu == 70 && sent == 1);
 request->mtu = 23;
 assert(att_mtu_req(&conn, &incoming) == 0);
 response = (void *)(output.data + 1);
 assert(att.mtu == 23 && response->mtu == 23 && sent == 2);
 return 0;
}
'''


def test_cmake_generated_source():
    if shutil.which('cmake') is None:
        return
    with tempfile.TemporaryDirectory() as directory:
        tree = Path(directory)
        (tree / 'CMakeLists.txt').write_text(f'''\
cmake_minimum_required(VERSION 3.19)
project(gatt_ccc_patch_integration C)
set(NUTTX_DIR "{NUTTX}")
add_library(wireless STATIC "${{NUTTX_DIR}}/wireless/bluetooth/bt_gatt.c"
                            "${{NUTTX_DIR}}/wireless/bluetooth/bt_att.c")
include("{ROOT / 'frameworks/cmake/gatt_ccc_patch.cmake'}")
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/sources.txt"
  CONTENT "$<TARGET_PROPERTY:wireless,SOURCES>")
file(GENERATE OUTPUT "${{CMAKE_BINARY_DIR}}/includes.txt"
  CONTENT "$<TARGET_PROPERTY:wireless,INCLUDE_DIRECTORIES>")
''')
        build = tree / 'build'
        original = (NUTTX / 'wireless/bluetooth/bt_gatt.c').read_bytes()
        outputs = [build / 'bk7258-bluetooth/wireless/bluetooth' / name
                   for name in ('bt_gatt.c', 'bt_att.c')]
        mtimes = None
        for _ in range(2):
            subprocess.run(['cmake', '-S', str(tree), '-B', str(build)], check=True)
            sources = (build / 'sources.txt').read_text().split(';')
            assert sources == [str(output) for output in outputs]
            assert str(NUTTX / 'wireless/bluetooth') in (
                build / 'includes.txt').read_text().split(';')
            if mtimes is None:
                mtimes = [output.stat().st_mtime_ns for output in outputs]
            else:
                assert [output.stat().st_mtime_ns for output in outputs] == mtimes
        generated = outputs[0].read_text()
        assert 'bt_keys_get_addr(&conn->dst)' not in function(generated,
                                                               'int bt_gatt_attr_write_ccc(')
        assert 'maxmtu = BLUETOOTH_MAX_MTU;' in outputs[1].read_text()
        assert (NUTTX / 'wireless/bluetooth/bt_gatt.c').read_bytes() == original


class NotifyResultTest(unittest.TestCase):
    def test_cmake_generated_source(self):
        test_cmake_generated_source()

    def test_actual_patched_functions(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            for name in ('wireless/bluetooth/bt_gatt.c', 'include/nuttx/wireless/bluetooth/bt_gatt.h'):
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(subprocess.check_output(['git', '-C', str(NUTTX), 'show', f'{BASELINE}:{name}']))
            for patch in PATCHES:
                subprocess.run(['git', 'apply', str(patch)], cwd=tree, check=True)
            source = (tree / 'wireless/bluetooth/bt_gatt.c').read_text()
            struct = re.search(r'struct notify_data_s\s*\{.*?\};', source, re.S).group()
            bodies = '\n'.join(function(source, marker) for marker in (
                'static uint8_t notify_cb(', 'static int gatt_notify_common(',
                'int bt_gatt_notify_checked(', 'int bt_gatt_notify_peer(', 'void bt_gatt_notify('))
            test = tree / 'test.c'
            test.write_text(PREFIX + struct + bodies + TEST)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(test), '-o', str(tree / 'test')], check=True)
            subprocess.run([str(tree / 'test')], check=True)

            ccc_bodies = '\n'.join(function(source, marker) for marker in (
                'static void gatt_ccc_changed(', 'int bt_gatt_attr_write_ccc('))
            ccc_test = tree / 'ccc_test.c'
            ccc_test.write_text(CCC_PREFIX + ccc_bodies + CCC_TEST)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wno-sign-compare', str(ccc_test),
                            '-o', str(tree / 'ccc_test')], check=True)
            subprocess.run([str(tree / 'ccc_test')], check=True)

            att_source = tree / 'wireless/bluetooth/bt_att.c'
            att_source.write_bytes(subprocess.check_output(
                ['git', '-C', str(NUTTX), 'show', f'{BASELINE}:wireless/bluetooth/bt_att.c']))
            subprocess.run(['git', 'apply', str(ATT_PATCH)], cwd=tree, check=True)
            att_test = tree / 'att_test.c'
            att_test.write_text(ATT_PREFIX + function(att_source.read_text(),
                                                       'static uint8_t att_mtu_req(', 1) + ATT_TEST)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(att_test), '-o', str(tree / 'att_test')], check=True)
            subprocess.run([str(tree / 'att_test')], check=True)


if __name__ == '__main__':
    unittest.main()
