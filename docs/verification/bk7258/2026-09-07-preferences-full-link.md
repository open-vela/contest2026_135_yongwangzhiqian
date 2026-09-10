<!-- SPDX-License-Identifier: Apache-2.0 -->
# AIDK 偏好配置完整链接验证

2026-09-07。通过唯一 BK7258 CLI，在隔离验证工作区构建 `drivercheck_cp` +
`drivercheck_ap`、MCUboot、板级 `bk7258_ab_fixed_block_full_release.csv`。
诊断 AP 配置启用 journaled UnQLite、DIRECT、任务 TLS 4 槽、文件锁 4 桶、
目录同步和偏好功能；关闭临时 KVDB 存储。生产 `openvela_ap` 未在本阶段启用偏好。

构建使用临时生成的两套独立 P-256 公钥；私钥在导出公钥后即删除，只进行链接验证。
计数输入 389。没有签名完整镜像，没有可发布/刷机包，不能用该裸二进制绕过下载流程。

## 结果

完整 BL1、BL2、CP、AP 构建通过；修复维护补丁中局部 `ret` 遮蔽警告后增量构建通过。
最终解析配置确认：

```text
CONFIG_BK7258_PREFERENCES=y
CONFIG_BK7258_PREFERENCES_BLOCKDEV="/dev/mmcsd0"
CONFIG_KVDB=y
CONFIG_KVDB_DIRECT=y
CONFIG_KVDB_UNQLITE=y
CONFIG_KVDB_PERSIST_PATH="/mnt/sdnand/shaniu.db"
CONFIG_UNQLITE=y
CONFIG_UNQLITE_ENABLE_DIRSYNC=y
CONFIG_TLS_TASK_NELEM=4
CONFIG_FS_LOCK_BUCKET_SIZE=4
```

编译数据库中引擎、KVDB DIRECT 和 UnQLite 后端三个文件均指向构建目录
`bk7258-kvdb/` 的维护补丁副本。最终 AP ELF 的符号表确认包含
`bk7258_preferences_get`、`bk7258_preferences_storage_begin/end`、
`unqlite_commit`、`task_tls_alloc` 和 `file_setlk`，并非仅有配置开关。

| 证据 | 值 |
| --- | --- |
| AP build identity | `bk7258-role-097d69b36b7a3863` |
| CP build identity | `bk7258-role-ccfa45200c74a051` |
| AP raw binary | 856424 bytes |
| AP SHA-256 | `1b3ec8c8e88dc785471d6e49c80088f53c6a066d5ee173f95a78f4bd18d1041f` |
| ELF size text/data/bss | 828536 / 27696 / 88888 bytes |
| Manifest SHA-256 | `26ed238254512a2454241f28f7fee1ff37f7c5235adbef1d17e88e44af9be779` |

输出位于隔离工作区 `out/bk7258-plan-validation/` 下的
`out/bk7258/aidk_ai_toy/drivercheck_cp__drivercheck_ap/bk7258-e66ee7b7206dc724/`；
manifest 为 `releases/mcuboot/build-manifest.json`。后续增量构建可以更新此路径，
复用时必须核对上述哈希。ELF 静态段大小不是运行时内存峰值。

## 实板边界

本阶段 COM8 115200、DTR/RTS 关闭，发送一次 `bkvoice status` 并观察 5 秒，收到
0 字节。未触发复位、下载或其他串口访问。因此配置读回、PTT 播放及掉电恢复仍未验收。
下一软件问题为播放前读取偏好对介质可用性的依赖：并发录像或 MSC 时，需要明确的
已加载配置缓存及失效策略，不能把存储占用错误直接当作用户改变音量。
