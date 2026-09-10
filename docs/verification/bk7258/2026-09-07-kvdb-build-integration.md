<!-- SPDX-License-Identifier: Apache-2.0 -->
# KVDB 维护补丁构建消费验证

2026-09-07：为默认关闭的 BKVoice 偏好功能接入团队维护补丁，没有启用板级配置或刷机。

`frameworks/cmake/kvdb_patches.cmake` 在 NuttX 顶层配置结束前延后执行，此时
`framework_utils`、`unqlite` 已创建。原始源码复制到构建目录，校验并应用补丁，
严格匹配并替换库的源文件；原始文件及补丁均作为重新配置依赖。
若选用 KVDB 临时存储，一并替换修复后的 FILE 后端。

manifest 增加 `frameworks`、`external` 两个目录级 linkfile，当前工作区目标链接
均已核对解析至团队仓对应目录。未增加公共构建命令。偏好配置限定 DIRECT + UnQLite；
Classic Make 尚无同等补丁消费，启用此功能时明确失败，避免编入未修复的事务后端。

验证入口：

```sh
make -C tests/host/bk7258 run-kvdb-unqlite
make -C tests/host/bk7258 run-preferences run-voice-turn-volume
```

结果：CMake 集成与真实引擎恢复两项测试通过；偏好 API、CLI 及播放前音量测试通过。
CMake 测试使用真实上游文件和维护补丁，覆盖延后创建目标、相对源文件、含 `..` 的
应用根路径、连续两次配置，检查四个库源文件均指向构建目录及官方源码哈希不变。

边界：该 CMake fixture 不是完整 AIDK 固件。目标配置解析、完整链接、数据库挂载与
SD NAND/MSC 所有权接线、实板配置读回及掉电恢复尚未完成。
