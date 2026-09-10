<!-- SPDX-License-Identifier: Apache-2.0 -->
# 2026-09-07 语音传输验证

本次在上游 `25da215` 合并基线继续开发。Gateway 的独立 mTLS 改动提交为
`8b9139b`；设备端 TLS、收包分发和相关测试仍是工作区候选，未构建或刷入本次设备镜像。

## 已验证

- `make -C gateway/shaniu test`：16 项通过。默认 loopback 行为保留；显式网络绑定
  同时检查客户端 CA 参数和实际 SSLContext 的 `CERT_REQUIRED`，最低 TLS 1.2。
  可信客户端成功，缺证书/不可信证书拒绝；只有配置检查涉及非 loopback 地址。
- `make -C tests/host/bk7258 run-voice-tls`：5 项通过。以工作区已有 mbedTLS 3.4
  源码构建主机库，实际 C provider、WSS adapter、companion codec 连接生产 Gateway。
  一轮发送 640 B 合成测试 PCM，收到两帧共 1280 B 固定回复；检查窗口、序列、turn、
  synthetic/EOS 和 TTS_END，关闭后可重连再执行。另验证错 hostname/CA、过期证书、
  未可信时间、读截止时间、空闲接收时并发发送、interrupt，以及故障后双向拒绝。
- `run-voice-tls-concurrency`：9 个可控 SSL I/O 场景通过。测试包含实际 provider，
  验证写重试保持独占，防止读生成的 alert 刷出 pending write 后重复发送；空闲读
  允许上行，fatal/EOF/close-notify/交叉 WANT/超时/中断使连接失效。此组使用 I/O
  替身，密码及证书互操作由上一组实际库测试覆盖。
- `run-voice-gateway`、`run-voice-session`、`run-voice-wss` 及完整 `make run` 通过。
  新增收帧/分发分离测试确认：RX 不调用下行或 PTT，WELCOME 分发前不开 sink；
  旧连接帧和错误不会改变新会话，连接代次耗尽时拒绝新连接。
- `git diff --check`、manifest XML 和 Gateway 文档链接检查通过；官方 NuttX/apps
  checkout 没有已跟踪改动。未改本次 SDK 或 NuttX 实现。

测试仅使用 loopback。临时证书/私钥退出后删除，未安装部署凭据或启动局域网服务。
主机库输出留在 host `build/`，不进入源码提交。

## 实板范围与限制

COM8 两次读取确认当前仍为 `18.6.327+387`，AP/CPU2/RPMsg 健康，最后
faults/recoveries/consecutive 为 `0/0/0`。未刷固件、未复位或改变 AEC 配置。

对旧录像发出一次只读 `bkvision check-record 280101d0 00000001`；有限捕获窗口只
取得命令回显，未取得完成结果。首个自动检查还使用了与真实命令不符的预期字符串，
因此明确不计通过；后续没有重复读盘。最后控制台健康查询正常。日志分别保留在
`out/shaniu-plan-20260907/merged-baseline-health-v327/`、
`storage-readonly-v327-*/`，本次未取得新的文件哈希，也不能认定读盘或 RPC 的根因。

v321 原有 10 秒/301 帧、重复读取和完整断电后的保留性验收继续有效。v323 的内容变化
与单帧解码异常另案保留；当前资料不足以证明新增代码回归。

本次没有物理 CP PTT 到 AP owner 的事件入口/队列验收，也没有部署端点、凭据和
可信时间安装。测试 PCM 不来自板载 MIC，不代表 AEC 效果或完整 MIC/DAC 物理验收。
