# 傻妞下一阶段：单轮接话纵切计划

本文保留 `P2-A` 的 BKVoice/companion-v1 协议、清理和验收契约，以及 2026-09-03/04
的执行快照。项目当前顺序与 Agent 替代 profile 决策统一见
[主计划第 17.5 节](shaniu-master-plan.md#openvela-mimo-execution)；下文早期快照不代表当前缺口。
Agent 路线必须复用同等的半双工和故障门，但不能把 companion-v1 的固定帧协议硬套到
MiMo HTTP/SSE provider。

## 0A. 2026-09-04 WSS 协议适配切片

- App 私有 `bk7258_voice_wss.[ch]` 已完成有界 RFC 6455 客户端协议适配：
  每个 binary message 只承载一个完整 `companion-v1` frame，强制客户端 mask，
  严格校验 HTTP 101、Accept、subprotocol、分片和 control frame，并支持 partial I/O、
  deadline、interrupt 与可重试 close。
- host 测试已覆盖正常握手/收发、ping/pong、peer close、非法 close code/UTF-8、
  重复 Accept、非规范长度、越界、取消与 close 失败；WSS、Gateway、Session 和
  Companion 四组回归通过。AIDK direct CP/AP 构建与 layer gate 通过，但新对象
  尚未被产品运行路径引用，最终 ELF 仍会回收它。
- 当前 v173 是设备绑定的 8 MiB 全 Flash **direct** 诊断镜像，不是 MCUboot
  签名启动镜像。direct AP 中 `BK7258_MCUBOOT_IMAGE=n`，因此依赖它的
  `BK7258_OTA_RPMSG -> BK7258_OTA_MANAGER -> BK7258_OTA_SOURCE_HTTP` 选择链被裁掉，
  `CRYPTO_MBEDTLS` 和 `LIBC_NETDB` 也不会被 OTA 间接选中。全 Flash 写入范围
  不等于 MCUboot profile 已启用。
- AP 主配置已显式开启 `LIBC_NETDB`、`CRYPTO_MBEDTLS` 和
  `MBEDTLS_ENTROPY_HARDWARE_ALT`，使 mbedTLS 从 AP 硬件 TRNG 的 `/dev/random` 取熵，而不依赖
  未安装的 `/dev/urandom` 或软件 PRNG。clean direct CP/AP 构建通过；因尚无
  TLS provider 引用，mbedTLS 静态库与 WSS 对象被 linker GC，当前 AP ELF 仅增加
  16 bytes，这不是实际 TLS 运行内存预算。
- Voice WSS 目标 provider 不得借 OTA symbol 强制开启 MCUboot/CP Flash installer。
  该 provider 必须同时冻结 mbedTLS CA/hostname、可信时间、有界 DNS/socket 实现和
  opaque credential 来源后才能接入
  `bkvoice_session` 运行路径。在此之前保持 `transport=not-installed` 和
  `playback-only`。

## 0. 2026-09-03 执行快照

- S1 已增加 `bk7258_voice_session.[ch]`：由同一个串行 owner 完成
  `WELCOME -> PTT sink admission`、PTT control、下行 dispatcher、远端 terminal 延后清理和
  `interrupt -> join -> session close -> transport close`；capture worker 只与一个 receive owner
  并发使用 Gateway。
- `make -C tests/host/bk7258 run-voice-session` 通过，唯一标记为
  `BKVOICE_SESSION_HOST_PASS`；测试覆盖一个完整固定回复 turn 和第二个 turn 的远端取消。
- S2 的 loopback-only TLS/WSS 夹具 12 个测试通过，唯一标记为
  `SHANIU_GATEWAY_TEST_PASS`。它仍无鉴权、设备注册和 LAN/公网部署能力，不能作为板端产品
  endpoint。
- `aidk_ai_toy` direct 模式 CP/AP clean build 通过；AP 编译了 session owner 源文件。由于产品
  尚未安装 WSS provider，链接器会回收未引用的 session owner，当前镜像仍明确是
  `transport=not-installed`，不能表述为 A1 板端闭环。
- S3 当前按停止条件暂停：仓内尚无 AIDK 产品设备令牌的可信供应/安全存储实现，也没有已冻结
  的板端 endpoint、CA 和可信时间策略。不得用 Kconfig、普通文件或日志中的明文 token 绕过该门。
- S4 物理按键继续等待 S3 产品 session 可启动后接入；CP 仍只负责公共按键事件源，AP 仍是
  session/PTT/audio owner，不改变核心归属。

## 1. 阶段目标

目标产品同时支持物理 PTT 和本地唤醒词入口（比赛首版“你好，openvela”），共用同一
AP 会话、上行、下行和资源清理链路。当前实板验收切片先用 PTT 跑通；
KWS 模型、唤醒后的 VAD 结束判定及 pre-roll 在 Master Plan 的 M7-KWS 独立验收，
不会因入口变化新增一套网络或音频 owner。“小冰”保留为后续非比赛版本扩展，
不进入本切片或比赛词表。

当前 PTT 切片闭合一次半双工接话：

1. AIDK 按下 PTT，双眼进入监听态，板端采集 16 kHz/mono/S16 PCM；
2. AIDK 通过主动发起的 Wi-Fi TLS/WSS 会话发送 `companion-v1` 上行帧；
3. 松开 PTT 后先中断、等待并释放 MIC，再向 Gateway 完成 `TURN_END`；
4. Gateway 流式返回下行 PCM，AIDK 在收到首个合法音频帧后播放并进入说话态；
5. `TTS_END`、取消或故障都完成有界清理，最终回到空闲或明确错误态。

本阶段分两个退出门：

- `P2-A1 TRANSPORT_CLOSED`：Gateway 使用确定性的固定流式回复，证明板端传输、换向、播放和
  清理链路，不依赖模型效果；
- `P2-A2 SINGLE_TURN_CLOSED`：同一链路接入 ASR/LLM/TTS，完成一次真实单轮问答。

只有 A1、A2 和实板故障门都通过，才可以宣称“单轮接话完成”。固定回复成功不能表述为
AI 对话完成。

## 2. BKVoice 协议基线

当前已有：

- `companion-v1` 40-byte header、640-byte/20 ms PCM frame、HELLO/WELCOME、turn、audio、
  cancel、window、heartbeat 和 error 编解码/会话约束；
- 半双工 turn arbiter、task-neutral capture pump 和 joinable PTT worker；
- 基于公共 media ABI 的 MIC/DAC adapter，以及 host 故障注入测试；
- 监听、思考、说话、空闲和错误到双眼状态的 best-effort 反馈桥；
- CP 上的 Beken Wi-Fi 控制器/RF/VNET 数据面，以及 AP 上的 NuttX `wlan0`/socket 使用边界。

接线和实板状态统一引用 [主计划第 17 节](shaniu-master-plan.md#17-立即执行队列)，
不再维护另一份“尚未安装”清单。构建通过不替代完整物理 PTT、真实模型回复和故障验收。

## 3. 冻结范围

### 3.1 产品串口功能明确排除

- 不实现产品 UART 收发、串口命令、串口协议、串口到 Gateway 桥接或串口配置项；
- 不创建 UART transport backend、UART 专属 enum、UART worker 或 UART host fake；
- 串口不参与产品运行路径、测试前置条件或阶段退出证据，也不能作为 TLS/WSS 失败时的回退；
- 只预留**传输介质无关**的 provider 接口，使未来替换传输实现时不改动 session/音频状态机；
- 现有启动、恢复、平台 console 或板载外设占用的 UART 保持现状，属于本阶段之外；不删除、
  不扩展，也不把它声明成傻妞产品能力。

这里的“预留接口”指通用 transport seam，不代表已经实现或承诺 UART 能力。

### 3.2 本阶段包含

- 单设备、单连接、单 session、单 turn 的半双工链路；
- AP 主动发起的 Wi-Fi TLS/WSS 连接；
- 确定性 Gateway 固定流式回复作为传输夹具，真实 ASR/LLM/TTS 按主计划接入；
- 物理 PTT、MIC 上行、DAC 下行、双眼反馈及有界故障恢复；
- host、build、Gateway integration 和 AIDK 实板四层证据。

### 3.3 本阶段不包含

- 连续追问、多轮上下文、长期记忆和主动搭话；
- KWS、pre-roll、Camera/VLM、姿态/NFC；
- Android 配网、控制台、BLE provisioning；
- 全双工、边播边录、多设备并发和公网入站服务；
- 最终人格、私有音色质量和模型效果调优。

## 4. 核心归属与数据流

| 层 | 本阶段唯一职责 | 禁止事项 |
|---|---|---|
| CP | Beken Wi-Fi controller、RF、supplicant/VNET 数据面和已存在的公共按键事件源 | 不承载产品 session、TLS/WSS、PCM 编解码或阻塞音频 worker |
| AP | NuttX socket、TLS/WSS provider、`companion-v1` session、PTT、MIC/DAC 和双眼反馈 | 不直接访问 SDK 私有 Wi-Fi/audio API，不在 callback 中阻塞 |
| Gateway | 鉴权、协议接入、窗口控制、固定回复，随后串接 ASR/LLM/TTS | 不向板端暴露模型私有数据，不等待整句 WAV 才下发 |
| Android | 本阶段无依赖 | 不作为接话链路代理或验收前置条件 |

```text
physical button
  -> bounded control queue
  -> AP session owner
  -> PTT / capture pump
  -> companion capture sink
  -> TURN_START / AUDIO_UP / TURN_END
  -> outbound TLS/WSS
  -> Gateway fixed stream, then ASR / LLM / TTS
  -> TTS_START / AUDIO_DOWN / TTS_END
  -> turn arbiter -> DAC
  -> voice-eye feedback -> IDLE
```

数据流中没有 UART 分支。

## 5. 预留接口约束

计划新增一个 App 私有、传输介质无关的 provider seam，暂定为
`struct bkvoice_transport_ops_s`。最终命名在实现评审时冻结，语义必须覆盖：

- `open/start`：由 session owner 启动出站连接，带有界 deadline；
- `send`：支持 partial write、背压和明确的可重试/致命错误；
- `recv/poll`：可被 interrupt 唤醒，不无限阻塞 worker join；
- `interrupt/cancel`：只负责打断 I/O，不越权释放 session/audio owner；
- `close`：幂等、有界，返回值能区分已关闭与仍可重试；
- link-state callback：只投递小型控制事件，不在 callback 中做协议或音频工作。

凭据不进入该接口参数、普通日志或持久明文配置；provider 只接收 credential handle/provider。
首个实现顺序固定为 host fake 后 WSS，UART 不在 provider 列表中。

## 6. 执行切片

### S0：契约、配置和资源门

交付：

- 冻结 transport provider、session owner、capture sink 和 downlink dispatcher 的 ownership；
- 选定 NuttX 可用的 DNS/TLS/WSS 客户端组合，记录证书、hostname、时间和凭据来源；
- 生成 AP resolved-config、ELF/map 基线并设定 heap、stack、frame queue 上限；
- 明确失败分类：DNS、TCP、TLS、HTTP upgrade、协议、deadline、取消和对端关闭。

退出门：没有依赖 UART；组件缺失或预算不成立时停止并报告，不先改 Board/Chip 层硬凑。

### S1：host 假传输纵切

允许写入范围仅为 `app/bk7258/**` 和 `tests/host/bk7258/**`：

- 增加通用 transport provider 接口和 fake provider；
- 增加 companion capture sink，把 PTT 上行映射成合法 `companion-v1` 帧；
- 增加 session orchestrator/downlink dispatcher，把下行控制与 PCM 交给现有 turn/audio owner；
- 用 fake Gateway 验证一个完整 turn 和所有清理顺序。

退出门：新 host suite 通过，现有 voice suites 不回归，`git diff --check` 通过；不得修改
Chip/Board、Kconfig、TLS、Gateway、Android 或 UART。

### S2：最小 Gateway 固定回复

- 实现一个独立、可复现的 `companion-v1` WSS endpoint；
- 严格校验 header、session/turn/sequence、payload length、window 和状态转换；
- 收到完整 `TURN_END` 后立即发送合法的 `TTS_START`、多个 20 ms `AUDIO_DOWN` 和
  `TTS_END`，不缓存整句 WAV；
- 只输出 aggregate count、状态、延迟和错误码，不保存或打印源 PCM/文本。

退出门：桌面 client 与 Gateway 完成正常、取消、乱序、断连和背压用例，获得
`P2-A1` 的服务端证据基础。

### S3：AP 出站 TLS/WSS provider

- 在 AP 使用 NuttX 原生 socket 接入 S0 选定的 TLS/WSS 组件；
- 强制证书链和 hostname 校验，禁止明文降级、静默跳过校验或无限 redirect；
- 所有 connect/read/write/close 均有 deadline，并能被 PTT/session owner 中断和 join；
- 复核 resolved config、ELF/map、heap/stack 峰值和 frame queue 上限。

退出门：clean CP/AP product build、静态层级检查和受控 Gateway integration 通过。构建成功仍不
等于实板完成。

### S4：物理按键接线

- 先确认已有公共按键事件 API 和真实板级映射，不猜 GPIO/中断号；
- CP callback 只投递按下/松开/取消事件到有界队列；
- AP session owner 消费事件并调用现有 PTT owner，callback 不读 MIC、不发网络、不操作 DAC；
- 重复边沿、长按、抖动、队列满和 session busy 都有确定策略。

退出门：按键 callback 的最坏执行时间有界；假事件和真实事件使用同一 AP control path。

### S5：实板 A1 与故障门

- 在 AIDK 上完成固定流式回复的监听、思考、说话、空闲视觉闭环；
- 验证 MIC 完全 release 后才允许 DAC reserve；
- 注入断网、Gateway 重启、握手失败、cancel、deadline、partial frame 和 session close；
- 连续执行 50 个单轮 turn，记录成功率、首包/首音延迟、heap/stack 水位和资源回收计数。

退出门：Gateway 机器可读聚合 trace、构建/ELF/map、双眼状态录像和可听见回复共同证明 A1；
串口输出不是必需证据。

### S6：BKVoice 路线的真实单轮 ASR/LLM/TTS

仅主计划 R0 继续选择 `BKVoice → Python Gateway` 时执行本节；Agent-AP 路线按其
provider 契约接入，不执行此处的 Gateway 对话编排。

- 保持板端协议与状态机不变，只在 Gateway 内替换固定回复 handler；
- 首轮 MiMo ASR 在 `TURN_END` 后将有界 PCM 封装为 WAV 并提交；SSE 识别结果流不代表
  实时麦克风上传。LLM 不获得外设、Shell、裸 Flash 或密钥能力；
- TTS 首个生成 chunk 立即转换为 16 kHz/mono/S16/20 ms，下游按 window 流式发送；
- 本阶段不保存原始音频、完整 transcript、embedding 或长期记忆。

退出门：同一 AIDK 完成真实一问一答，并重复通过 S5 的取消、断网和资源回收门，达到
`P2-A2 SINGLE_TURN_CLOSED`。

## 7. 验收矩阵

| 层级 | 必测项 | 可接受证据 |
|---|---|---|
| Host | frame/sequence/window、partial I/O、stale/replay/gap、cancel/timeout/disconnect、interrupt → join → detach → drain/release → `TURN_END` | 测试命令与唯一 PASS marker |
| Build | CP/AP clean product profile、resolved config、ELF symbol、map、manifest、层级边界 | 构建身份、hash 和检查结果 |
| Gateway | WSS upgrade、证书/hostname 失败、固定回复首 chunk、背压、重连、真实模型切换 | 聚合 trace、延迟分位数和错误码 |
| AIDK | 物理按键、非零 MIC、MIC→DAC 换向、两眼状态、可听回复、50 turns、故障恢复 | 视频/音频观察、Gateway trace、网络状态和资源摘要 |

所有公开证据只保存 aggregate count、hash、状态和质量/延迟指标，不进入 Git 的内容包括原始
PCM、私人文本、音色源数据、embedding、checkpoint 和推理输出。

## 8. 停止条件

- DNS/TLS/WSS 无受支持实现、证书/时间/凭据来源不可信或资源预算超限：停在 S0/S3，不能改走
  UART 或关闭校验；
- MIC 没有稳定非零数据：退回物理音频 baseline，不能用 Gateway 成功掩盖硬件缺口；
- 找不到已验证的产品按键 API：保留 fake control 接口并报告，不猜 GPIO；
- interrupt/join/release 任一步无法有界完成：不得进入 DAC 或 50-turn；
- Gateway 固定回复尚未稳定：先隔离传输故障；独立 provider 适配可以继续，但不能将其
  接口通过标为板端接话通过。

## 9. 2026-09-03 S1 任务范围（历史）

当时 S1 的目标是不依赖硬件、网络和串口的单轮协议纵切，以下保留其范围供追溯：

1. 冻结 `bkvoice_transport_ops_s` 和 session owner 的最小 public-to-App-private 边界；
2. 实现 fake transport、companion capture sink 和 downlink dispatcher；
3. 覆盖正常 turn、背压、partial I/O、取消、deadline、断连、乱序和 join retry；
4. 运行新增 suite、现有 BKVoice host suites 和 `git diff --check`；
5. 输出精确变更、PASS marker 和剩余风险，不提交、不推送。

该任务包不得顺手加入 UART、TLS 配置、真实 Gateway、Board/Chip 修改、Android 或模型集成。
