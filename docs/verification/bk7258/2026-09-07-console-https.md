# Gateway console-v1 HTTPS 控制接线

日期：2026-09-07。范围：主机 HTTPS/WSS 与 Android 协议验证，未刷机、未安装手机、未调用外部模型。

## 实现

- Gateway 可选启用独立 HTTPS 端口，提供设备 snapshot、mutations 和 events WSS；语音入口继续要求板端 mTLS。
- 操作员登记的 bearer SHA-256 授权限定设备、有效期和写权限。原始 token 不写入登记文件或访问日志；这不等同 BLE/App 自助认领。
- 设备连接必须来自已验证证书登记。SQLite 持久化控制 generation，断线重连产生新代；旧代请求拒绝。
- 人物设置实际调用当前 MiMoConversation.set_persona，仅空闲时允许，支持 revision 检查、请求去重与过期检查。人物尚未持久化。
- 状态来自真实 Gateway 会话；电量、充电、固件、音量、情绪和 OTA 未接入的值明确未知。Gateway speaking 不表示扬声器播放完成。
- volume.set、turn.cancel 和 OTA 等未接线操作明确拒绝，不返回假成功。
- Python 与 Android 共用三份 console-v1 JSON 向量。Android 界面支持未知情绪、OTA、音量及充电状态。

## 验证

- `make -C gateway/shaniu test`：51 项通过，覆盖实际本地 TLS/WSS 端点、授权、只读、过期、设备隔离、重连代数、人物修改与幂等。
- 复核发现 Python 3.10 的 asyncio.TimeoutError 与内置 TimeoutError 不同，修正等待下行窗口的异常捕获；新增无下行额度测试验证 ERROR 帧和 1002 关闭。生产超时阈值未变。
- `ANDROID_HOME=/tmp/shaniu-sdk-proxy ./gradlew --offline :app:testDebugUnitTest :app:assembleDebug`：44 项测试通过，APK 构建成功。
- APK：4190657 字节；SHA-256 `149568b99e2a93c99f2694a33e0ef39e56e3b30ba0876b7d4705ec9ad4384941`。
- `git diff --check` 通过。未提交、未推送。

## 剩余验收

手机部署需要可信 HTTPS 证书、可达地址及独立 console 授权。尚未进行真实手机/板端验收；PTT、扬声器完成确认、远程取消、板端音量与遥测接线继续推进。128MB 存储及已合入 SDIO 一致性基线不重新开启调查。
