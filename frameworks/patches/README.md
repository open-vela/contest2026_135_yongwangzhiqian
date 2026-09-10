<!-- SPDX-License-Identifier: Apache-2.0 -->
# OpenVela framework 维护补丁

`kvdb/0001-file-handle-partial-interrupted-io.patch` 基于
`open-vela/frameworks_system_utils` 提交
`5e582301ffa1401d1e48d49cea46edeba97b822e` 的 `kvdb/file.c`，保留上游 Apache-2.0。

修复短写偏移/剩余长度、读写 EINTR、零写、写入失败后 errno 被 close 覆盖、
关闭失败、列举与删除错误传播。公共 API 和现有后端选择不变。

验证入口（团队仓根目录）：

```sh
make -C tests/host/bk7258 run-kvdb-file run-preferences
```

测试在临时目录复制当前上游文件、校验并应用补丁，然后编译真实后端做 POSIX
故障注入；官方 checkout 不变，临时目录自动清理。未接入目标构建或开启板级 KVDB。

补丁只修复 I/O 语义：现有 O_TRUNC 仍可能破坏旧值，commit 仍为空操作，
不能用于声明掉电安全。当前 NuttX VFS 覆盖 rename 会先 unlink 目标；不能直接以
临时文件 rename 宣称事务性。持久化需另行验证带恢复的提交方案及真实文件系统/介质。

## UnQLite 事务候选

`kvdb/0002-unqlite-explicit-journaled-commit.patch` 同样基于上述 framework 提交。
恢复 UnQLite 原生日志，关闭隐式 close 提交，DIRECT set/delete 显式提交并传播错误；
转换 UnQLite 错误为 POSIX errno，失败回滚，回滚失败后的句柄拒绝继续操作。

同时依赖团队 `external/patches/unqlite/0001-propagate-commit-sync-errors.patch`：
引擎基线为 `open-vela/external_unqlite` 提交
`25731ab0e2a4aa119df1329f799cd571a794720c` 的 `unqlite.c`（Symisc BSD-2-Clause）。
修复日志同步/关闭、数据库同步/截断以及恢复同步错误传播，数据同步失败时保留恢复日志。

```sh
make -C tests/host/bk7258 run-kvdb-unqlite
```

测试用当前真实 UnQLite 引擎编译两个临时补丁副本；覆盖显式提交、失败回滚、DIRECT
错误反馈、真实同步失败及同步点中断恢复。它是 Linux 主机证据，不是实板掉电保证。
目标端仍需验证文件锁、目录同步、底层 flush 和 RAM/Flash 预算，板级当前不启用。

当前引擎补丁还检查启用 dirsync 时的目录同步错误以及提交阶段的 journal 删除错误。
日志已成功删除后的同步失败属于结果不确定：返回错误并隔离失败句柄，重新打开读回
可能得到新值。上层必须重新协调状态，不能保证每种提交错误都恢复旧值。

## 目标构建消费

`frameworks/cmake/kvdb_patches.cmake` 经 manifest 的目录映射供产品应用消费。
启用 `BK7258_PREFERENCES` 时，CMake 在所有库创建后将上述补丁应用到输出目录
`bk7258-kvdb/`，只替换 `framework_utils` 和 `unqlite` 的对应源文件。
原始文件和补丁变化会触发重新配置；补丁漂移、库/源码缺失会终止配置。
若启用 KVDB 临时存储，还消费 FILE I/O 补丁。官方 checkout 不修改。

当前配置限定 DIRECT + UnQLite；Classic Make 在启用该功能时明确报错，防止静默
编入未修复的后端。普通偏好关闭的构建不使用此入口。完整 AIDK 固件链接、持久路径与
介质所有权、实板重开/掉电恢复仍待验证；构建消费本身不构成持久性保证。
