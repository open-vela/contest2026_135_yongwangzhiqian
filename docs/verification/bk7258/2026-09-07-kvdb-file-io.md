# KVDB FILE I/O 修复验证（2026-09-07）

目标为 OpenVela framework 公共后端，修复保存在团队仓维护补丁中；没有改 SDK、NuttX
或官方 framework checkout，没有开启板级配置后端、刷机或进行掉电测试。

## 实现与验证

基线：`open-vela/frameworks_system_utils` 提交
`5e582301ffa1401d1e48d49cea46edeba97b822e`，文件 `kvdb/file.c`。

`make -C tests/host/bk7258 run-kvdb-file run-preferences` 退出 0。
真实上游后端在临时副本应用补丁并编译，12 个子场景全部通过：
正常读写、短写、连续写 EINTR、零写、ENOSPC、初始读 EINTR、部分读取后 EINTR、
读 EIO、close 失败、删除不存在项、空值、列举时读取失败。
写入结果通过真实文件字节比较；close 故意改变 errno，验证原始错误不被吞掉。
零写设调用上限和进程超时，避免旧实现导致测试无限等待。

同一夹具编译未打补丁上游时，子场景 1/2/3/4/5/6/8/9/11 共 9 项失败（编号从 0 起）；
补丁应用检查通过，临时副本自动删除。
产品测试标记 `BK7258_PREFERENCES_HOST_PASS` 和 `BK7258_VOICE_PREFERENCES_CLI_HOST_PASS`。

## 持久化边界

现有 FILE 仍以 O_TRUNC 覆盖、persist_commit 返回 0，没有事务或掉电保证。
当前 `nuttx/fs/vfs/fs_rename.c` 的 mountptrename 在覆盖普通文件时先调用 unlink，
之后才调用文件系统 rename；`nuttx/fs/fat/fs_fat32.c` 的 fat_rename 要求目标不存在。
因此不能直接使用临时文件覆盖 rename 声称旧值安全，需要恢复协议或经验证的事务后端。

此轮只闭合可重复验证的公共 I/O 缺陷，目标构建集成、后端选定、同步落盘、掉电恢复和
App 设置生效仍待实现/验收。与既有 SDIO 数据读回一致性是不同问题。
