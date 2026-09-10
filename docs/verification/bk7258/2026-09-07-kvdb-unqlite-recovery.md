# KVDB UnQLite 恢复验证（2026-09-07）

本轮继续复用 OpenVela KVDB 与 UnQLite 原生事务日志；所有差分保存在团队仓。
Framework 基线 `5e582301ffa1401d1e48d49cea46edeba97b822e`，引擎基线
`25731ab0e2a4aa119df1329f799cd571a794720c`。官方 checkout 未修改。

## 修复

- framework：移除 OMIT_JOURNALING、禁用关闭时隐式提交；DIRECT set/delete 显式调用
  commit。UnQLite 错误映射到 POSIX errno；提交失败回滚，回滚失败的句柄拒绝再使用。
  初始化失败清空句柄，游标初始化失败不再继续使用空游标。
- 引擎：真实同步故障注入发现 finalize journal 和 commit phase1 吞掉同步错误，导致
  底层 EIO 后仍返回成功。修复同步/关闭/截断错误传播，数据库同步失败标记需回滚；
  恢复阶段同步失败同样返回错误，防止误删恢复日志。

## 主机证据

`make -C tests/host/bk7258 run-kvdb-unqlite` 退出 0。
测试编译真实 framework 后端、DIRECT 路径和真实 UnQLite；只在临时副本应用补丁。
覆盖不存在项、空库列举、持久读回、未提交关闭、提交失败、回滚失败、游标分配失败、
DIRECT set/delete 提交失败反馈、DIRECT 成功，以及日志/数据库两阶段的真实同步 EIO。

512 个 4096-byte 值写入事务并确认磁盘上存在非空 journal 后，直接 _exit，重开恢复旧值，
未提交 bulk 项不存在。另在 4 个同步序号设置退出点，重开验证同一事务两键始终一致，
不能出现一个旧值一个新值。进程中断不等于切断存储电源。

引擎补丁加入前，真实同步 EIO 测试失败：commit 返回成功且新值可读。
初始夹具只拦截 fsync，后按实际 full_fsync 源码同时拦截 fdatasync；引擎错误仍复现。
修复后同步失败能返回 -EIO 并恢复旧值。

## 目标端限制

本轮未构建/刷写目标固件或开启 KVDB。现有 UnQLite 外层配置提供 LOCK_BY_SEM 和
DIRSYNC 开关，但当前引擎源码未发现 LOCK_BY_SEM 实现，目录同步错误亦有忽略路径。
需核对目标文件锁、目录元数据落盘、底层 flush 和资源预算，不能仅复制 Linux 参数启用。
需要硬件条件才能验证实板断电；此证据不改变先前 SDIO 读回一致性验收结论。
