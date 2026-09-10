# BK7258 board diagnostic built-ins

映射到 openvela `apps/system/bk7258`，由官方 `apps/system/`
CMake、Kconfig 和 Make 递归机制自动发现。
本目录只承载由 App Kconfig 显式选择的 BK7258 NSH 维护与诊断命令；初始化模板
`app/hello_app` 保持独立且不承载产品功能。每个命令都有独立的
`CONFIG_BK7258_APP_*` 开关；底层 Driver/Test
符号只负责能力/端点，不再自动注册应用。

可用 App 开关：

```text
CONFIG_BK7258_APP_BKVALIDATE
CONFIG_BK7258_APP_APCTL
CONFIG_BK7258_APP_RPMSG_TEST
CONFIG_BK7258_APP_RPMSGFS_TEST
CONFIG_BK7258_APP_BT_IPC_TEST
CONFIG_BK7258_APP_WIFI
CONFIG_BK7258_APP_PSRAM_TEST
CONFIG_BK7258_APP_GPIO_TEST
CONFIG_BK7258_APP_GPIO_IRQ_TEST
CONFIG_BK7258_APP_IRQ_TIMER_TEST
CONFIG_BK7258_APP_TIMER_SELFTEST
CONFIG_BK7258_APP_VOICE
CONFIG_BK7258_APP_DISPLAY
CONFIG_BK7258_APP_HEALTH
CONFIG_BK7258_APP_NFC
CONFIG_BK7258_APP_VISION
CONFIG_BK7258_VOICE_SERVICE
CONFIG_BK7258_DISPLAY_SERVICE
CONFIG_BK7258_HEALTH_SERVICE
CONFIG_BK7258_NFC_SERVICE
CONFIG_BK7258_VISION_SERVICE
```

每个开关还带有 `_PROGNAME`、`_PRIORITY`、`_STACKSIZE` 子配置，可在
`menuconfig` 中调整。App 的 `depends on` 保证底层能力不满足时命令不可选。

N14 `cp_nsh_psram + ap_smp_psram`新增：

```text
bkpsramtest info
bkpsramtest heap [iterations=16]
bkpsramtest all  [iterations=16]
bktimertest [iterations=64]
```

`bkpsramtest info`同时核对CP容量/heap/MPU、boot-only raw gate和AP双核启动门禁；`heap/all`
只测试当前CP private heap。全容量破坏性PSRAM测试只在启动时、建立heap和释放AP之前执行，
不存在运行时raw命令。`bktimertest`验证SDK software timer callback的task context、callback内
self-delete及queued final-free。

完整范围、源码约束和实板证据见：

- [N14 board verification](../../docs/verification/bk7258/2026-08-03-n14-psram-board-verification.md)
- [N14 source verification](../../docs/platforms/bk7258/nuttx-port/n14-psram-source-verification.md)
- [N14 evidence index](../../docs/platforms/bk7258/nuttx-port/n14-evidence-index.md)

P5 validation skeleton (opt-in with `CONFIG_BK7258_APP_BKVALIDATE=y`) exposes:

```text
bkvalidate list
bkvalidate run <descriptor-id>
bkvalidate all-compatible
```

The target-side table in `bkvalidate_main.c` is the sole descriptor source.
`all-compatible` serializes global resource claims and emits `SKIP` for interactive, fixture,
destructive-fault, planned, or unavailable requirements.  The dispatcher does
not call vendor SDK functions directly.  Individual descriptors may invoke
explicitly selected BK7258 diagnostic endpoints; they are never started merely
because the dispatcher is enabled.

## 测试分层约定

- 纯逻辑（无硬件依赖）的 C host 用例放 `tests/host/bk7258/`，唯一完整入口是
  `make -C tests/host/bk7258 check`。旧的维护工具 Python 测试目录已经退役。
- 命令壳（NSH 内置命令）留在 `app/bk7258/`，由 `CONFIG_BK7258_APP_*`
  门控；每个命令只有 enable/PROGNAME/PRIORITY/STACKSIZE 和依赖声明。
- CP/AP 公共生命周期契约通过 `app/testing/bk7258/` 的官方格式 CMocka 板上应用执行；
  三块板的 UART0 自动化由 `tests/pytest/test_bk7258/` 链入官方 pytest。需要显式操作
  硬件的 rpmsg / gpio / psram / bt / irq / timer 命令仍保留
`CONFIG_BK7258_APP_*` 形态，再由 pytest 调用，不在测试中复制产品实现。

## AIDK 本地授权语音 App

产品伴侣名为“傻妞”，稳定 machine ID 是 `shaniu`。它的关系叙事可以采用
`fictional-ex-girlfriend` 风格，但机器状态始终声明 `role=ai-companion` 和
`disclosure=synthetic-ai`；不得把人物设定、真实身份或授权音色混为一件事。

`CONFIG_BK7258_APP_VOICE=y` 在 CP 注册 `bkvoice`；AIDK 产品配置同时启用 AP
`VOICE_SERVICE/VOICE_TLS` 和 CP GPIO1 低有效 PTT。`bkvoice status` 分开报告服务、配置、
连接、Gateway、TLS、PTT 链路、按键与 turn 状态。另保留显式 `capture-test` 麦克风诊断、
16 kHz/单声道/S16 PCM WAV 流式播放，以及固定 1 kHz、500 ms 的 `tone-test` 喇叭诊断。
前者只返回帧数、字节数、非零样本数和峰值，不保存或回传原始 PCM；后者生成低幅度、
首尾渐变的非语音 PCM，不依赖 SD NAND 或 voice pack。
产品 PTT、mTLS/WSS 和 Gateway transport 已进入配对配置及 AIDK AP 构建；当前缺的是
实板 PTT→真实 PCM→MiMo→扬声器验收，不能把构建或诊断结果冒充为物理闭环。
语音包必须声明说话者明确授权，并在每次播放前输出 `BKVOICE SYNTHETIC` 标识。

AP 同时编译 transport-neutral 的 `companion-v1` 帧编解码、Gateway adapter 和串行 session
owner。host 纵切已覆盖 `HELLO/WELCOME`、PTT/MIC 上行、MIC release 后的 `TURN_END`、
synthetic PCM 下行/DAC、远端取消、network byte order、严格 sequence、window credit、重连和
旧 session/turn。安全 WSS provider 和 session owner 已进入当前 AIDK AP ELF；主机
TLS/WSS 互操作与交叉编译通过仍不代表物理 PTT、板上网络或实板录放音已接通。

下行窗口是滚动背压：每个完整 640-byte `AUDIO_DOWN` 只有在 DAC 接口确认整帧已交给
本地播放队列后，Gateway adapter 才在同一串行 owner 内返还等量 `WINDOW_UPDATE`。
返还失败会使连接 fail closed；不会提前放大窗口或从音频回调重入 gateway mutex。主机
回归使用一帧初始额度连续接收六帧，覆盖超过产品四帧初始窗口的回复。

App-private `bk7258_voice_wss` 已冻结一条可测试的安全边界：每条 binary WebSocket message
只承载一个完整 `companion-v1` frame，客户端强制掩码，严格检查 HTTP 101、Accept 和
`companion-v1` subprotocol，并对分片、ping/pong、partial I/O、deadline、interrupt 和关闭重试
做有界处理。它只接受名为 `open_verified` 的 TLS stream contract。AP 主配置已有
socket、DNS、mbedTLS 和硬件 TRNG 熵的构建基础。`BK7258_VOICE_TLS` 可编译
`bk7258_voice_tls`：TLS 1.2 双向认证、链/hostname/有效期校验，带 deadline 的非阻塞
I/O 和 interrupt；未可信系统时间拒绝连接。部署层须提供独立于证书主机名的 IPv4
拨号地址、借用的 CA/客户端证书/私钥句柄及可信时间检查，provider 不自行配置这些事实。
写操作跨 WANT_WRITE 重试保持 SSL context 独占；空闲读放锁允许上行，终止错误使双向
失效直到 close/open。主机实连 Gateway 的测试通过不表示这些依赖已安装到实板。

独立 RX task 使用 `bkvoice_gateway_receive_frame()`，把带连接代次的有界帧或错误交给
唯一 owner 调用 `bkvoice_session_dispatch_frame()`；RX 不调用 PTT/下行音频回调。
旧代次帧和错误被拒绝，WELCOME 由 owner 分发后才开放 sink。错误事件也必须送达 owner，
关闭前仍须 interrupt、join RX 和 capture，再关闭 transport。RX task、产品队列和 CP→AP
PTT 事件路径均已接入；GPIO 电平、队列运行和断线恢复仍需同一块实板验证。

AP 还编译纯 App 层的半双工 turn arbiter、task-neutral capture pump 和 joinable PTT worker
owner。主机故障注入已经覆盖 MIC
`acquire/prepare/start -> stop/drain/release`、DAC
`acquire/prepare/start -> drain/stop/release`、超时、取消、断连、控制序号溢出、乱序和旧
token；产品 PTT 事件与 WSS session 已绑定。除显式 `capture-test` 的短生命周期本地统计
sink 外，PTT owner 只有在真实 companion sink 建立 session 后才启动 worker；capture pump
只拼接并提交完整 640-byte/20 ms 帧。PTT release
必须先 interrupt recorder，再有界 join/detach worker，随后由 arbiter release MIC，最后才允许
sink 发布 `TURN_END`；join 超时会保留 worker/MIC 供安全重试，不会销毁仍被 reader 引用的
recorder。控制序号耗尽会在清理资源后终止 session。

同时启用 `CONFIG_BK7258_VOICE_SERVICE` 与 `CONFIG_BK7258_DISPLAY_SERVICE`
时，turn arbiter 的已提交状态会由低优先级 worker 异步映射为双眼表情：
`CAPTURING=listening`、`WAITING_TTS=thinking`、`PLAYING=speaking`、
`IDLE=neutral`、`FAULTED=error`。显示更新是 best-effort，LCD/资源包失败只记日志，
不会改变 MIC/DAC 状态或语音请求结果；显示较慢时允许合并短暂中间状态。
`bkvoice play` 的本地授权语音播放也会在实际 player 启动后显示 `speaking`，资源释放后
恢复 `neutral`。这些仅是代码与构建证据，仍需在实板观察两块 LCD 才能验收。

```text
bkvoice status
bkvoice capture-test 1000
bkvoice tone-test
bkvoice verify /mnt/voice/voicepack.ini
bkvoice play /mnt/voice/voicepack.ini greeting
bkvoice stress /mnt/voice/voicepack.ini greeting 100
```

Gateway/App 的运行时音量查询与设置随语音服务提供，不依赖 KVDB；设置值保留在
本次启动的媒体策略中，并在后续播放 prepare 后重新应用。非秘密持久偏好接口仍由
AP 的 `CONFIG_BK7258_PREFERENCES` 门控，默认关闭并依赖 KVDB：

```text
bkvoice prefs
bkvoice prefs volume 50
bkvoice prefs persona gentle
```

复用现有 voice RPC，不新建跨核存储协议。支持 0–100 音量以及 `gentle/playful/quiet/serious/tsundere_lite`
五种 persona，拒绝无效值并返回 KVDB 错误；`default_flags` 的 bit 0/1 表示音量/persona
使用缺省值。输出 `desired_*` 表示存储的意图；启用此功能后，下一轮 PTT 回复在
播放器 prepare 完成后，按公共策略查询到的范围映射并设置音量，再读回核对后启动。
档位量化可能使实际百分比与请求值不同；正在播放的本轮不被设置命令打断。
播放使用已确认的音量缓存，首次使用才读取数据库；`bkvoice prefs` 显式刷新、成功
设置或重启后首次加载会更新它。介质忙不会影响已有缓存；提交或清理结果不确定时缓存
失效，下一次必须重读。MSC 外部编辑不会自动改变正在使用的音量。
配置读取或策略操作失败会返回错误并释放播放器，不能记为音量生效。
Gateway 人物配置由鉴权后的 console 控制链路持久化并应用于活动 MiMo 会话；板端
KVDB persona 仍只属于本地偏好接口，两者不能互相冒充确认状态。
须先配置并验证 KVDB 后端、持久分区和断电恢复才能启用；凭据不经过此接口。
KWS 库的 microfrontend 参数、训练入口及启用条件见
[训练与推理契约](../../docs/platforms/bk7258/bkvoice-authorized-voice-app.md#训练与推理契约)。
同一 KWS 构建门还包含纯 App 层的 wake window：它用 caller-owned 32 KiB 环保存最近
50 个 20 ms PCM 帧，命中后冻结并按时间顺序提供 1 秒 pre-roll，再以可配置的环境噪声比、
连续语音/静音帧和最大时长给出免提收音结束事件。它不打开 MIC、不充当唤醒模型，阈值也
没有实板标定；正式模型资产、AP 单一 audio owner、PTT/播放切换及 Gateway pre-roll 上行
仍须接入后才能启用产品 KWS。

`stress` 先验证一次语音包，再有界重复播放 1 到 100 次；首次失败会报告精确轮次，
用于配合播放前后的 heap、fd、mqueue、audio reserve 和 `apctl status` 对比。
`capture-test` 需要操作者显式执行，时长范围 100..5000 ms；请求时长从麦克风完成启动后
开始计算，日志同时报告启动耗时、实际活动时长、期望帧数和帧覆盖率。PASS 要求帧覆盖率
不低于 80%，并且存在完整帧、非零样本和非零峰值；原始音频不写文件、不经 RPMsg 返回，
也不能替代真人听感、通道映射或长稳验收。AEC v3 的 EC 与外置 AGC 默认启用；BPF/DRC/CNI
和内置 NS 分别由 `CONFIG_BK7258_AUDIO_PREPROCESS_POSTFILTERS`、
`CONFIG_BK7258_AUDIO_PREPROCESS_INTEGRATED_NS` 门控，只有连续实板测试能满足每 20 ms
一帧的实时覆盖率后才可启用。
`tone-test` 同样只由操作者显式执行；AP 通过公共 `media_player` 按 640-byte/20 ms 帧播放，
对 partial write、零写、RPMsg 断开以及 stop/close 错误均失败关闭。PTT session、MIC/DAC
worker 或未释放的音频句柄存在时返回 `-EBUSY`，不会抢占产品会话。命令 PASS 只证明数据已被
播放器接受且清理成功；仍需实板确认可听音、失真/爆音、PA P50 时序及重复执行后的资源回收。

AIDK 的 SD NAND 只注册为 `/dev/mmcsd0`，不把 `/data` 误认为 SD NAND。
BKDisplay 仅在持有 USBMODE 块设备 lease 时短暂挂载它，并在释放给 MSC
之前完成卸载。双眼资源包的生成、首次拷贝、目录和回退契约见
[Shaniu eye assets](assets/display/README.md)。语音包详细格式、挂载互斥、训练边界和验收步骤见
[BKVoice 本地授权语音应用](../../docs/platforms/bk7258/bkvoice-authorized-voice-app.md)。

`CONFIG_BK7258_APP_DISPLAY=y` 在 CP 注册最小板端控制入口。命令通过独立的
`bkdisplay-v1` RPMsg 协议请求 AP 显示服务，CP 不直接访问 SD NAND 或 LCD：

```text
bkdisplay status
bkdisplay mood neutral
bkdisplay mood happy
bkdisplay mood listening
bkdisplay calibrate
```

`mood` 使用资源包中的逻辑 expression 名称，状态是易失的，重启仍回到
`neutral`。只有返回 `BKDISPLAY MOOD PASS` 且两块实屏画面符合预期，才算板端
显示闭环；`mapping=unverified` 仍表示左右物理映射尚未验收。
`bkdisplay calibrate` 只在正常资源画面已经就绪后运行：它把 `/dev/fb0`
显示为纯青色、`/dev/fb1` 显示为纯品红色，并保持
`mapping=unverified`。记录实体左眼看到的颜色后执行
`bkdisplay mood neutral` 恢复画面；校准证据由板级映射配置消费，App 不猜测或持久化结果。

## AIDK 设备健康 App

`CONFIG_BK7258_APP_HEALTH=y` 在 CP 注册只读 `bkhealth` 命令，AP 的
`bkhealth-v1` 服务通过标准 `/dev/bat0` ABI 读取充放电状态和毫伏电压，并通过
CP-owned 温度端点读取片上温度原始码：

```text
bkhealth status
```

命令允许电池或温度单项失败，并为每项打印独立状态；它不把某个传感器故障扩散成语音、
显示或 OTA 故障。当前没有经过电芯放电曲线标定，因此明确输出
`percent=unavailable`，不会用电压线性换算伪造电量百分比。温度原始码始终优先；只有为该
芯片提供有效的 25 摄氏度参考原始码后，才输出 `temperature_mC` 和
`calibrated=yes`。host 与目标构建通过仍不等于实板读数验收。

## AIDK NFC 在场 App

`CONFIG_BK7258_APP_NFC=y` 在 CP 注册 `bknfc`，AP 的 `bknfc-v1`
服务按次独占打开、读取并关闭 `/dev/nfc0`：

```text
bknfc scan
```

命令只返回 `present=yes/no`，协议中没有 UID、卡号或卡片内容字段；“检测到卡片”也不等于
身份认证或授权。读取只使用一个随即清零的私有 scratch 字节，任何 UID 派生内容都不会跨
RPMsg 或由命令打印。host 与目标构建仅验证协议、资源释放和隐私边界；仍需用无卡/有卡
重复扫描完成实板验收，后续白名单场景必须使用独立、可撤销且不能以 NFC 为唯一凭据的策略。

## AIDK 视觉 App

`CONFIG_BK7258_APP_VISION=y` 在 CP 注册 `bkvision`，AP 的
`bkvision-v2` 服务作为当前配置中 `/dev/video0` 的唯一应用 owner，经标准 V4L2 MMAP 路径
拍照或连续录像；当前 NuttX capture buffer 是设备全局资源，不能同时启动其他 camera app。
CP/AP 必须使用同一协议版本，v1 与 v2 不会建立连接：

```text
bkvision snapshot
bkvision record 10
bkvision bench 10
```

`snapshot` 只返回实际宽高、JPEG fourcc、有效字节数、capture sequence 以及 SOI/EOI/V4L2
错误标志。JPEG 数据仅在 AP 的 driver-managed PSRAM buffer 中短暂存在，用于边界校验；协议
没有像素或指针，snapshot 命令不会写文件。捕获使用非阻塞 DQBUF 和有界超时，
且仅在 STREAMON 成功后执行 STREAMOFF，随后结束映射视图并关闭设备；BK7258 的 flat-address
MMAP 缓冲区由 sole-owner 的最后一个 close 释放。2026-09-05 的 `18.6.230+290` 已通过
同次启动三次及正常重启后一次有效 JPEG 拍照和关闭，见
[SDK 频率投票修复与实板证据](../../docs/platforms/bk7258/aidk-gc2145-jpeg-frequency-fix.md)。
当同一 AP 配置同时启用 `BK7258_DISPLAY_SERVICE` 时，显式 `snapshot` 会复用现有眼睛资源包：
捕获前显示 `thinking`，捕获成功或失败后分别显示 `happy` 或 `error`，短暂保持后恢复
`neutral`。显示失败不会改写拍照结果；恢复使用条件替换，若语音等并发路径已经切换到更新
表情，则不会被迟到的 `neutral` 覆盖。
仍需完成超时恢复、100 次循环、fd/heap/Camera owner 泄漏和真实 Camera 隐私指示验收。
本纵切也不等于 Gateway VLM 或 Android snapshot 已接通。

`record <1..60>` 是同步、有时长上限的无声录像命令。AP 挂载已有 FAT
`/dev/mmcsd0`，使用三个 MMAP 缓冲循环 DQBUF/写入/QBUF，保持一次 STREAMON，
结束后 STREAMOFF/close。结果保存为卷内 `/recordings/video-<session>-<sequence>.avi`，
命令输出精确文件名、帧数、采集毫秒数和 AVI 字节数。已有文件不覆盖，不格式化存储；
单文件上限 64 MiB，写满、取帧超时、坏 JPEG 或关闭错误均报失败并尝试删除本次残片。
异常断电不保证删除残片或修复 AVI 头。启用 USBMODE 的固件正常完成后可通过 USB MSC
取走文件；当前 AIDK 配置未启用 USBMODE，尚需实板文件导出验收。

录像与显示资产操作由 `bk7258_media_volume` 互斥，共用 `/mnt/sdnand` 挂载点；若启用 USBMODE，
录像持有其 blockdev lease，USB MSC 与本地写入互斥。卸载失败保留 lease，下一次录像
先重试清理。不要从其他命令同时挂载该块设备或操作该目录。

AVI 使用实际交付帧数与采集用时计算平均播放帧率，不宣称固定 30 fps，也不表示无丢帧。
没有音频、实时推流或提前停止命令；到时自动停止，单次 DQBUF 最多额外等待配置的取帧超时。
主机验证与板端连续录像验收分别记录在
[连续录像适配](../../docs/platforms/bk7258/aidk-continuous-recording.md)。

`bench <1..60>` 使用同一连续取帧链路，但不挂载卷、不写文件。
`BKVISION PERF` 分别统计等帧与写文件耗时，`BKCAM PERF` 统计 SDK 完成回调、
缓冲耗尽及交付/丢弃次数；芯片统计覆盖 SDK 打开到关闭，包含启动阶段。
有效采集帧率按 BENCH 的 frames / elapsed_ms 计算，不能用 JPEG 中断数代替。

`record-verify <1..60>` 在录像结束后回读完整 movi 数据并核对 FNV-1a 摘要，
输出 `BKVISION VERIFY`；普通 `record` 不执行这项耗时检查。
这是落盘一致性检查，不等同于独立 JPEG 解码；结果中的 `elapsed_ms` 仍只统计
采集区间，回读发生在停流后，命令返回时间会更长。
