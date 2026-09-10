<!-- SPDX-License-Identifier: Apache-2.0 -->
# KVDB AIDK AP 目标端预检

日期：2026-09-07。此阶段是源码核对和对象编译，没有修改板级配置、链接固件或刷机。

## 编译证据

复用已存在的 AIDK AP `bk7258-role-5b54cde5639a3771` 编译数据库，取
`bk7258_voice_turn_audio.c` 的工具链及 include/架构参数；移除原输出和依赖文件参数。
在自动清理的临时目录复制官方源码并应用团队 KVDB/UnQLite 维护补丁。

现有 `.config` 的 `CONFIG_TLS_TASK_NELEM=0`、`CONFIG_FS_LOCK_BUCKET_SIZE=0`。
直接编译引擎虽然返回 0，但存在 `task_tls_*` 隐式声明警告，不算有效目标支持。
外部 UnQLite Kconfig 本身要求任务 TLS 或 kernel build，并要求文件锁桶大于 0。

第二次仅通过临时头文件将这两项设为 4，提供未访问的候选数据库路径宏，启用
UnQLite pthread 支持并关闭 Jx9 builtins/disk I/O；增加
`-Werror=implicit-function-declaration`。结果无编译诊断：

| 源码 | 对象字节数 |
| --- | ---: |
| patched engine `unqlite.c` | 300864 |
| patched KVDB `unqlite.c` | 4160 |
| patched KVDB `direct.c` | 2572 |

对象大小不是链接后 Flash/RAM 占用。临时配置不能替代 Kconfig 解析、系统 TLS
重新编译和完整链接；4 是编译预检取值，尚未定为产品资源预算。

## 文件系统边界

- `nuttx/fs/vfs/fs_fcntl.c` 把 F_SETLK 等交给 ioctl；`fs_ioctl.c` 对 ENOTTY
  回退到 NuttX `file_setlk`。无需仅因为外部 `UNQLITE_LOCK_BY_SEM` 选项而另写锁。
  当前引擎源码没有消费该宏，目标应核实实际 fcntl 配置及运行行为。
- `nuttx/fs/vfs/fs_open.c` 将只读打开目录交给 `dir_allocate`；`fs_fsync.c`
  对目录驱动发 BIOC_FLUSH；`fs_dir.c:dir_ioctl` 对该命令直接返回成功。
  因而启用 UnQLite dirsync 不会自动获得 Linux 目录刷新的效果。
- FAT `fat_remove` 会调用 `fat_updatefsinfo`，后者刷新文件系统缓存并返回错误。
  不能由目录 fsync 为空操作直接推出 FAT 删除必定未落盘；也不能据此证明介质掉电安全。

下一实施边界是维护补丁的目标构建接入、有效 Kconfig 和完整链接，再验证真实
文件系统上的提交/重开及掉电恢复。已完成的 128MB SDIO 一致性基线不在本次重测范围。
