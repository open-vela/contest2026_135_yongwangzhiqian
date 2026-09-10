<!-- SPDX-License-Identifier: Apache-2.0 -->
# 偏好数据库共享介质接线验证

2026-09-07：将非秘密偏好读写接入 AP 现有 media volume 排他占用，未启用板级配置。

`bk7258_preferences_storage` 用 mutex 串行化完整读写范围，获取 PREFERENCES
占用后短时挂载 FAT；DIRECT KVDB 每次调用关闭句柄后卸载，再释放占用。
同一个占用接口排斥显示、录像及 USB MSC。挂载失败不会接管现有挂载；卸载失败保留
挂载和占用，释放失败保留占用，下次调用先重试清理。没有格式化或擦除路径。

数据库配置路径必须是 `/mnt/sdnand/` 的直接子文件，由 CMake 检查。
`BK7258_PREFERENCES_BLOCKDEV` 默认 `/dev/mmcsd0`，必须对应既有共享介质。
读配置仅在后端读取和清理均成功后更新输出结构；设置失败或清理失败返回错误，
不承诺写入一定没有发生。提交结果不确定时仍需重新读取协调。

验证命令：

```sh
make -C tests/host/bk7258 run-preferences run-media-volume run-kvdb-unqlite
```

通过偏好 API/CLI、挂载生命周期、三类 AP 媒体占用排他、CMake 消费及真实引擎恢复测试。
模拟覆盖 EBUSY 介质、mkdir 失败、外来挂载、后端失败清理、卸载/释放失败及重试。
测试替身不执行实际 mount，不接触 SD NAND。

另外使用 AIDK AP `bk7258-role-5b54cde5639a3771` 编译数据库的工具链及架构参数，
在临时输出目录编译当前偏好、挂载和共享占用源码；仅额外提供公共 KVDB include、
候选 BLOCKDEV 宏及隐式声明错误检查，三个对象均无诊断通过：3648、3196、1536 字节。
对象大小不是最终 Flash/RAM 占用。

限制：完整配置解析/链接、实板挂载、配置读回和掉电仍未验收。播放前读配置在媒体忙时
会返回错误，连续录像/MSC 与语音的组合体验仍需另行设计和验收；不能由排他测试宣称
全部并发体验已完成。主机 Make 提示短暂文件时间偏差，各新二进制均有编译和运行输出。
