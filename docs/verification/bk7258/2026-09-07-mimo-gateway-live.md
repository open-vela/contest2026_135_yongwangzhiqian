<!-- SPDX-License-Identifier: Apache-2.0 -->
# 2026-09-07 MiMo 与 Gateway 真实接口验证

范围：用户授权的 MiMo 账号、非私人内置音色测试短句、主机生产 Gateway 与本机 WSS 客户端。
本次不是实板 PTT 验收，不涉及 SDK/NuttX 修改、固件构建或刷机。
凭据只从用户指定文件读取，权限收紧为 0600；不记录文件路径、密钥、转写正文或原始音频。

## 接口与格式

- Base URL：`https://token-plan-cn.xiaomimimo.com/v1`，使用 Bearer 鉴权；
- 对话：`mimo-v2.5`，关闭 thinking，HTTP 200；一次短回复用时 687 ms，正常结束；
- TTS：`mimo-v2.5-tts` / `mimo_default`；非流式 WAV 头实测 24000 Hz、单声道、16 bit，
  首个样本时长 1600 ms；流式 `pcm16` 的 SSE/Base64、正常结束和 `[DONE]` 校验通过；
- 一次独立流式请求收到 7 个音频块、107520 字节，首块 728 ms；
- Gateway 按 WAV 的实测格式设置输入 rate=24000，通过 FFmpeg 转为 16000 Hz/mono/S16LE。
  原始 PCM 流没有 WAV 头；其与 WAV 采样率一致是当前接入假设，板端播放速度和听感仍待验收。

账号技术上能调用上述接口，不据此推断赛事订阅使用条款的额外豁免。

## 识别与端到端

| 检查 | 结果 |
|---|---|
| 第一条含技术用语的测试短句 | ASR 返回 8 字符，去标点后未完全匹配；不记为识别准确率通过 |
| 第二条普通短句，先转为 16 kHz WAV | 去标点后参考/识别均为 8 字符，编辑距离 0；仅证明该样本匹配 |
| 直接调用生产 `MiMoProvider.reply` | ASR→对话→流式 TTS→重采样完成；输出 225280 字节，首块 3161 ms，总处理 4379 ms |
| 本机 WSS 客户端→生产 Gateway→真实 MiMo | 输入 66560 字节，没有遗漏尾部；输出 352 个 640-byte 音频帧，共 225280 字节 |
| WSS 首音与结束 | 首音 2830 ms，整轮含播放节奏 10739 ms；最后音频帧 EOS 和 TTS_END 均通过 |
| Gateway 聚合结果 | turns_completed=1，protocol_errors=0 |

主机 WSS 使用即时生成的 loopback TLS 测试证书，结束后服务器停止、证书和私钥删除。
全部测试语音来自本次内置音色生成，音频与文本只在进程内存中使用，没有录制真实人物语音。
以上单轮延迟不是 P50/P95，不能外推噪声识别率、物理 MIC/DAC 或 50 轮稳定性。

## 实测修复与回归

真实服务返回 gzip 编码响应，原 provider 显式关闭 HTTP 解压，导致 WAV JSON 解析时
出现 UnicodeDecodeError。修复为使用 aiohttp 的响应解压；原有 JSON/SSE 大小、超时、
TLS/hostname 和不跟随重定向的限制保留。HTTPS 夹具同步覆盖 gzip JSON 与 gzip SSE。

`make -C gateway/shaniu test`：31 项通过，标记 `SHANIU_GATEWAY_TEST_PASS`。
既有窗口、取消、超时、尾帧、FFmpeg 重采样和退出测试同时通过。

## 实板边界

Windows 查询存在 COM8 和 COM16；只访问用户指定的 COM8。115200、RTS/DTR 关闭，
一次 CR 和一次 CRLF 的 `bkvoice status` 均未取得可识别状态；未访问 COM16、未复位。
因此尚不能确认当前板端运行镜像和控制台就绪状态，也未装载设备凭据或开启录音。
后续需取得当前启动/状态响应，再验证网络与 mTLS，完成物理 PTT→MIC→Gateway→DAC。
