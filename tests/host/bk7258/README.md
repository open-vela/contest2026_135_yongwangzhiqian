# BK7258 主机回归测试

本目录直接编译仓库中的现役 `chips/bk7258` 实现，用主机 mock 隔离 MMIO、SDK 和
NuttX 内核接口。它用于快速发现源码迁移、生成 ABI 和纯逻辑回归，不代替固件构建或
任一块 BK7258 物理板的实板 xTS。

## 运行

依赖 GCC/Clang、Python 3、`pkg-config` 和 cmocka。唯一完整入口是：

```bash
make -C tests/host/bk7258 check
```

Host 测试不映射进 OpenVela 应用树。`check` 从干净的
`tests/host/bk7258/build/` 开始，记录提交、编译器、Python、cmocka、sanitizer 和
权威分区 CSV 哈希，然后依次执行公共模块、BL1、BL2 和 AP/CP 外设测试。
成功结束必须出现 `BK7258_HOST_TEST_PASS`。构建产物只写入该 `build/` 目录。

也可在本目录执行分层入口：

```bash
make run-core
make run-bl1
make run-bl2
make run-ap
make run-voice-pack
make run-voice-companion
make run-voice-turn
make run-voice-gateway run-voice-session
make run-voice-tls-concurrency
make run-voice-tls
make run-provision
make run-voice-kws
make run-preferences
```

`run-voice-kws` 直接编译已同步的 TFLM microfrontend 与固定点 KissFFT，检查
上游输出、批处理/流式特征一致性及模型训练清单审计；同时验证 50 帧/32 KiB pre-roll
环的覆盖顺序、时间戳断层、语音确认、静音结束、空唤醒和最大时长。也可单独执行
`make run-voice-wake-window`。该入口需 NumPy/pytest 和 C++ 编译器，不下载依赖、不使用
真人语料，也不构成唤醒准确率、能量门限标定或板端实时性验收。
`run-preferences` 检查 KVDB 适配的缺省/范围/错误传播，以及真实 CP 命令的 RPC 编码；
KVDB 后端和 RPC 传输在这些测试中为替身，不证明持久化或实板配置生效。

`run-voice-tls-concurrency` 编译实际 mbedTLS provider，以可控 SSL I/O 替身检查
WANT_WRITE 重试期间的独占、空闲读允许上行，以及故障/取消后的双向停止。
`run-voice-tls` 使用已同步的 `apps/crypto/mbedtls/mbedtls` 源码构建主机库，另需
CMake、OpenSSL 和 `gateway/shaniu/requirements.txt` 中固定的 Python websockets。
该入口实际运行 C TLS/WSS 客户端及临时 loopback 服务，测试证书校验、协议往返、
deadline 和 interrupt；成功路径只授予一帧初始下行额度，并用逐帧回补接收六帧回复。
证书与私钥只存在于测试临时目录，退出后删除；不启动 LAN 服务，
不下载依赖。这两个独立入口均不构成 CP 按键到 AP/Gateway 的实板验收。

`run-provision` 依次运行供应 helper、产品 GATT 窗口、队列和连接代际测试，以及使用实际 mbedTLS 的
认领、本地按键 owner、Wi-Fi/Gateway 试连、存储与回执、设置、身份、时间和 TLS 测试。
它需 CMake、OpenSSL 和工作区 mbedTLS 源码；构建产物、测试证书和私有存储都位于运行后
自动清理的临时目录。不启动 LAN 服务，也不代表实板验收。

## 当前覆盖

- 公共层：RPTUN mailbox、CP/AP RPTUN core、PM activity、BL1 policy；
- BL1：libc、SHA-256、flash、clock、runtime 和现行 Beken manifest；
- BL2：security counter、flash-map/CRC trailer 写入和 CP/AP pair policy；
- AP/CP 外设：JPEG、YUV/H.264、scale/rotate、CAN 和 IrDA。
- App 纯逻辑：授权 voice-pack/WAV gate，以及 transport-neutral `companion-v1`
  network-byte-order codec、sequence/window/cancel/reconnect 状态契约；半双工 turn arbiter
  的 MIC/DAC 严格释放顺序、重放/旧 token、超时、取消和逐阶段故障回滚；下行测试还以
  一帧初始额度连续接收六帧，检查每次 DAC 接受后才返还同量窗口。

分区头不使用历史副本，而是由
`boards/bk7258/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv`
在 `build/layout/` 临时生成。BL1 公钥 fixture 是确定性的公钥字节，仅用于模拟验签
ABI；它不是私钥、下载密钥或可部署信任根。BL2 不固定断言任何签名公钥，因为正式
构建必须按每一代的新密钥生成对应源码。

## 边界

主机 PASS 只能证明被编译模块的逻辑和 ABI。它不证明串口、时钟、电源、真实 flash、
CAN 收发器、RTC、存储介质或 12 小时稳定性。涉及硬件的状态必须另附当前代构建身份、
下载边界、原始串口日志和恢复结果。

测试源码的许可证范围、SDK/NuttX 接口替身和公开密钥夹具来源见
[`PROVENANCE.md`](PROVENANCE.md)。

### KVDB FILE 上游补丁

`make run-kvdb-file` 在临时副本应用团队 `frameworks/patches/kvdb/0001`，编译真实
上游 FILE 后端并运行 12 个正常/故障子场景；不改官方源码，不访问板卡。
`make run-preferences` 继续验证产品设置 API/CLI。两者不构成掉电持久化验收。
偏好测试同时覆盖共享介质短时挂载：忙介质、目录/挂载失败、卸载失败保留占用、
释放失败保留状态及下次重试；KVDB API 替身检查所有读写均位于 begin/end 内。

`make run-kvdb-unqlite` 使用真实 UnQLite 引擎验证日志恢复、DIRECT 显式提交与错误传播，
在临时目录应用 framework 和引擎补丁；涵盖底层同步失败、事务未提交退出和同步点中断。
不修改官方 checkout，不能替代板端断电、文件锁与目录同步验收。
该入口也运行 CMake 消费测试：通过 manifest 映射读取真实上游源码，验证延后替换四个
编译源、连续两次配置以及官方源文件哈希不变；这不是完整固件链接测试。

### 播放前应用偏好音量

`make run-voice-turn-audio run-voice-turn-volume` 分别编译偏好关闭和开启的真实 turn/audio
实现。两组都验证无需 KVDB 的运行时查询、设置和跨回复重用；开启组另覆盖 8 个
存储/策略故障场景及 6 个量化边界，检查失败不启动、资源释放、0/100% 和不同策略
最大档位。媒体和 KVDB 在此为边界 mock，不证明真实 DAC 音量。
`run-preferences` 还验证首次加载、已缓存时零存储访问、忙介质拒绝写入后保留旧值、
成功设置更新缓存、不确定结果失效、外部编辑需显式刷新。缓存和读写由偏好所有者的
同一 mutex 串行化；挂载 helper 只在该序列内调用。
