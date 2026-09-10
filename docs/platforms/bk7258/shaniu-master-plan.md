# 傻妞 AIDK AI Toy 全项目 Master Plan

状态：`IN_PROGRESS`

## 2026-09-10 每板应用入口收束

正常AIDK CP入口由 `configs/openvela_cp` 迁为 `configs/app`，配置内容保持
逐字节不变；配套AP、分区与发布策略不变，`--board aidk_ai_toy` 自动选择新路径。
这只是配置组织整改，不代表新实板验收。历史417与其他产物路径保留作为原版证据。
小海豚只在T5-Board的app中启用；没有将傻妞产品替换为小海豚。

## 2026-09-10 发布当前进度并转入小海豚新固件

用户要求保存并推送当前项目进度，随后开启独立“小海豚”固件开发。此检查点不是傻妞完整验收通过：实板最新已核验411，417候选未安装；真实认领/唤醒/图片理解/App OTA及K2电流与唤醒仍待实物验收。Gateway保留已冻结代码，后续主线不恢复其部署依赖。

- 发布前重复执行产品键、认领、cloud/vision/memory、OTA事务、wake listener/owner/session、USB存储租约和reset主机回归，均通过；日志 `out/shaniu-product-acceptance-20260910/publication-host-tests.log`。
- Android `testDebugUnitTest`、`assembleDebug` 通过：26 suites、133 tests、0 failures/errors、2 skipped（跳过项不计通过）；日志 `out/shaniu-product-acceptance-20260910/publication-android.log`。
- 417双核构建及签名包证据沿用上节，未修改二进制或刷写设备。代码发布状态由实际Git提交/远端SHA确认，不把提交等同实板通过。
- 发布排除：735份原始诊断日志、两份drivercheck候选配置、3份引用已撤回容量/busy探针的失效测试。它们留在本地，不混入产品提交。凭据与私有语料不纳入。
- 新固件的需求与进度独立维护；复用已验证的BK7258板级/驱动与OpenVela生态，不把傻妞未验收功能自动标成新产品已支持。


## 当前交付状态（2026-09-10，覆盖下方历史检查点）

证据目录：工作区 `out/shaniu-product-acceptance-20260910/`。本节按当前版本判断；下方流水记录仅保留定位过程，不作为当前结论。

用户最新设备状态：已再次人工复位，COM8只读确认411/confirmed、manager idle、faults/recoveries=0/0（`board-user-reset-status.log`）。小米10已由用户拿走；ADB当前只有 `emulator-5554`。后续App开发使用模拟器，不再操作手机；物理BLE/NFC与手机OTA继续单列待验收。现有复位证明正常启动，不等于Loader读取窗口已捕获。

模拟器当前APK `524d487e6bcf000cc5c49ec8d129ce870d6a65b478daf53ef890bc5f57b175cf` 已安装；Keystore不可导出密钥、加密待定事务恢复、认证证书pin、TLS分片/错误pin拒绝、合成设备状态UI通过，见 `emulator-ui-security-result.json`。合成状态不构成真实认领证明。

模拟器后续验收：新增instrumentation从实际首页进入“固件更新”，未连接/旧能力快照禁用升级；使用唯一测试偏好调用真实 `confirmExpectedOta()`，INFO版本/计数不匹配拒绝、匹配才成功、终态-5显示失败均通过。记录 `emulator-ota-ui-result.json`，测试APK SHA256 `0ecaa382700c6b3aea5f83ab6f53f0c07ec3659c70e3829e28cafc0d867989b7`。未创建连接/上传或触碰真实绑定。416包的模拟器HTTPS来源尝试在 `open_wifi` 被拒绝：当前仅eth0/10.0.2.15，没有Wi-Fi IPv4；`emulator-ota-source-416-result.json` 为失败记录，不冒充手机来源或实板OTA通过。

| 需求 | 当前状态 | 证据与下一动作 |
|---|---|---|
| 实板与恢复路径 | 实板通过（仅411启动健康）；候选安装外部阻塞 | 软件复位后仍为 `18.6.347+411`、A/confirmed、faults/recoveries=0/0，见 `board-after-readback-probe.log`。Loader read 不支持 swrst；软件复位后立即 read 仍未取得总线，未读出文件、未写 Flash。等实际 RESET/CEN 配合完成两份一致的8MiB恢复备份，不能把进程退出0当读取成功。 |
| 候选固件 | 自动化通过；未安装 | `18.6.353+417` 双核增量构建、包校验及签名验证通过，见 `candidate417.json`；包 SHA256 `7c9485e5005f6d46cc2d956c3215d54d92833d598fe2352e7589b9fbbf04c0cd`。413–416候选与证据保留，均未安装；默认KWS、wake runtime、AEC关闭，不能据此称唤醒可用。 |
| K1/K3音量、K2电源 | 已实现未实板验证 | 417新增K2长按3秒松开→业务排空/存储同步→CP整芯片reset-to-super-deep→P12模拟GPIO唤醒；K1/K3音量保持。尚未安装，未测休眠电流/唤醒；不代表物理切断VBAT。 |
| BLE认领、配网、配置恢复 | 已实现未验证 | 自动受控发现与认证/提交恢复已实现并有主机测试；新固件上的真实认领与重启恢复未验收。 |
| NFC入口 | 已实现未验证 | 真机小米钱包模式 operation=-93，临时HCE钱包模式=-110，均未收到选择事件；已恢复原钱包模式并读回，见 `nfc-route-experiment.json`。不能算射频成功。 |
| 唤醒、自动语音及打断恢复 | 未实现（组件自动化通过） | 可信模型session与runtime接线、listener/owner生命周期和真实cloud异步取消排空均已实现；根代理回归与启用分支ARM对象编译通过，见 `wake-session-integration-host.log`、`wake-session-integration-arm.log`。仍缺有效模型、采播并发与真实唤醒/语音验收。 |
| 语音拍照理解 | 已实现未验证 | 真实取帧上传路径有源码及主机检查；真实说话触发、上传、理解与扬声器未验收。 |
| App固件更新 | 手机来源组件自动化通过；整链已实现未验证 | 小米10当前APK SHA256 `524d487e6bcf000cc5c49ec8d129ce870d6a65b478daf53ef890bc5f57b175cf`，文案修复安装及回读哈希通过（`phone-product-copy-result.json`）。HTTPS源验收对应此前APK `ce40687a1dcef6403ef2770436e9e959417047c557cfa56834e6e7d8c37cd46b`：159次请求、2576998字节及包哈希、错误信任/主机名拒绝、清理均通过，见 `phone-https-source-result.txt`。未执行App→实板升级/重连版本确认/回滚验收。 |

415新增安全边界：Flash写入前，AP owner必须先持久化已验证的目标身份，worker收到批准才能读镜像；取消与批准竞争、暂存提交状态不明均保留恢复记录。主机并发/持久化/恢复测试及完整构建通过；真实断电、丢失完成通知与回滚试验未执行。

唤醒打断的独立缺口：当前 `bk7258_voice_turn_audio.c` 的 MIC/DAC acquire 双向互斥，`wake_owner` 在 cloud busy 时停止监听。因此单轮自动收音接线不能证明播放中声学打断；不得仅删 busy 判断或另建上下文绕过仲裁。芯片 `bk7258_media_root.c` 同样只有单一 ADC/DAC owner，`chips/bk7258/Kconfig` 明确共用 AUD clock root；AIDK MIC2 为扬声器参考，产品 defconfig 尚关闭待AP-SMP验收的预处理。最小下一步是复合会话的时钟/PM、两路DMA和停止顺序验证，再复用 MIC2/AEC 完成近端唤醒与远端自触发对照；此项仍属必交验收，未降为可选。实板恢复与当前候选安装前不直接移除芯片互斥。

App产品指引再次校正：删除首页、交互页、设备状态的4条“按住说话键”残留，现有状态文案测试与APK构建通过；实际安装小米10并回读APK哈希一致，当前App可见且无此提示。未把“网络已连接”改写为“唤醒已可用”。

OTA source主机安全回归新增并由根代理复核通过：真实签名与预期摘要绑定、签名篡改拒绝；真实MbedTLS回环HTTPS正确CA/hostname成功、错误CA/hostname拒绝。记录 `ota-source-auth-host.json`、`ota-source-tls-host.json`，脚本 `tests/host/bk7258/test_bk7258_ota_source_http.py`。同步POSIX回环桩不覆盖NuttX/RPMsg，也不证明此前实板-EIO根因已解决。

最新接线修复：`bkcloud_runtime_cancel()`只发布取消标志，关闭路径必须驱动既有cloud step排空。新增最小 `cancel_drain()` 并以真实cloud runtime验证延迟worker释放，避免OTA/配置/关闭早退造成永不空闲；失败时保留对象，外部非wake会话不误取消。固定路径模型有界读取、SHA校验、FIFO非阻塞拒绝、缺模型不占MIC/不刷屏/不阻断内部调试入口均有主机证据。416包含默认配置下的集成，启用KWS分支仍只有主机与ARM对象证据。

### 2026-09-10 K2低功耗关机实施检查点

- 根代理负责芯片/板级/安全和集成；复用已指定Terra的按键代理与Sol的AP代理，未新增代理。后端实际模型路由未验证。手机/App物理联调仍按用户指示暂停。
- 板级传入P12/低有效唤醒映射；按键长按3秒只锁存，全释放才提交。断链/组合键/时钟回退不会误提交。`run-product-keys`通过，日志 `/tmp/shaniu-product-417-keys-test.log`。
- AP持有SD块设备使用租约，MSC导出拒绝关机；OTA未恢复、持久intent/作业、认领/配置事务未闭合时拒绝。排空wake/cloud/legacy音频后sync，再向CP请求。接受或通信结果不确定时保持关闭业务，只有CP只读status明确空闲才能恢复，不能按超时猜测。
- CP使用官方 `RESET_SOURCE_FORCE_DEEPSLEEP=0x19`，只接受confirmed固件的请求。LPWORK同步文件系统并取得Flash事务guard后整芯片重启，避免单独停止AP留下CP Wi-Fi持有的旧指针。早启动消费意图，在AP/无线/SD启动前复用SDK `pm_hardware_init`、模拟GPIO唤醒和super-deep入口；进入失败则普通重启，不循环睡眠。正常NuttX PM_SLEEP仍保持既有保护。
- SDK回调会处理GPIO16=SD_D0，因此睡眠仅在冷启动阶段进入。SDK/NuttX源码未改动。主机reset测试验证功能开/关两种reset reason白名单；不作为物理睡眠证据。
- 417双核增量构建、包校验、MCUboot CP/AP签名验证通过；证据 `out/shaniu-product-acceptance-20260910/candidate417.json` 及同目录 `candidate417-*.log`。当前未安装，板端仅保留411既有证据；没有本轮真实K2操作、关机电流或唤醒证据。
- 下一动作：417候选包已校验，按既有恢复保护完成安装与长按松开/再次按K2/启动版本/电流验证。整片下载仍缺本机完整备份，手机OTA物理联调仍暂停，不以绕过这些条件换取“通过”。

### K2/P12 电源能力核对（源码结论，未实板执行）

用户补充引脚图后重新核对：KEY2=P12。SDK `cp/include/driver/gpio.h:493` 明确模拟GPIO唤醒接口专用于super deep sleep；`cp/middleware/driver/gpio/gpio_driver_base.c:1227` 接受GPIO0～15及高/低电平，`cp/middleware/soc/bk7258/hal/sys_pm_hal.c:1133` 将0～15选择值写入模拟唤醒寄存器。因此P12具有超深睡按键唤醒的源码依据，不能因缺少VBAT锁存就判定必须先改硬件才能实现产品开关机。

现有适配缺口：实际链接的 `v3.1.1.9/cp-aidk/config/sdkconfig.h` 已开启 `CONFIG_PM_SUPER_DEEP_SLEEP` 和 `CONFIG_GPIO_ANA_WAKEUP_SUPPORT`，未开启 `CONFIG_DEEPSLEEP_USING_WDT_PROTECT`；此前仅看通用默认Kconfig的结论已纠正。NuttX `chips/bk7258/cp/bk7258_pm_policy.c:60` 主动拒绝PM_SLEEP，产品K2路径目前返回ENOTSUP。应先复用SDK机制完成CP拥有的低功耗关机/唤醒接线，安全停止AP/外设、保存数据，释放K2后再使能低电平唤醒，核对SDK回调对GPIO16的处理，并实测电流与再次启动。未改SDK/NuttX代码，也未向实板发送睡眠/关机命令。

此结论支持低功耗软关机候选，不证明VBAT被物理切断或整板达到指定功耗；真正断开电池供电仍是独立电路问题。纠正此前“必须新增锁存才能做K2开关机”的过度结论，保留真实电源行为验收标准。手机相关联调继续暂停。

### 恢复入口与剩余外部条件

- **恢复备份/新版本实板验收阻塞**：两种自动read路径均未读出数据，且当前恢复基线不能代替本机新备份。等待一次真实RESET/CEN配合，在读工具进入Getting Bus后操作；不是中间K2。先取得两份8MiB且哈希一致的私有备份并核对设备，再按板规则构造恢复/安装路径；不提前启动无限等待或重复写入。用户随后报告已复位，已确认411正常启动；本次没有与运行中的Loader读取同步，完整备份仍未取得。
- **官方唤醒与声学打断未完成**：需要有效“你好，openvela”模型及真实多人/噪声输入；当前9条合成管线fixture不可发布。还需恢复后核验芯片ADC/DAC复合owner、时钟/DMA/AEC实测，之后才能开放播放中声学打断。不得将半双工单轮测试算通过。
- **K2电源行为未完成**：P12超深睡唤醒已有SDK源码依据，先完成低功耗开关机适配及电流/再次启动验证；物理切断VBAT另需电路，不能将两者混同，也不预先断言产品开关机必须改板。
- **完整流程仍待实物输入**：新版本BLE认领/配网重启恢复、NFC选卡与保护、真实说话拍照理解、App OTA/失败/回滚均未验收。恢复后先认领与配置，随后语音/照片，再App更新及安全回归；不使用旧411证据覆盖新版本。

目前没有提交、推送或发布。交付状态为未完成；已保留候选固件、实际安装App与证据。物理准备到位后从本节继续，不再重跑已通过且未变化的主机基线。

## 2026-09-10 先前执行检查点（历史，实测与源码分开）

- 当前只读实测：COM8 返回 `18.6.347+411`、A/confirmed、counter=411，supervisor faults/recoveries=0/0；
  小米10（59d707dc）和模拟器均在线。未安装412，也未重做其验收。记录在工作区 `out/shaniu-product-acceptance-20260910/baseline.json`。
- 原理图核对：KEY1=P13、KEY2=P12、KEY3=P8，均按下接地；另一个 RESET 器件的位号 K1 接 CEN，不能和功能键KEY1混用。
  原理图第2/3页显示主芯片 VBAT 直供、无 K2 整机电源锁存；外设 LDO33_EN 不切断主芯片供电。
  真断电能力尚不满足；最小硬件方案为可按键启动、GPIO保持/释放的电源锁存或负载开关，并需实测关机电流/再次启动。
  不把未测深睡或仅关闭外设标为真断电，当前不执行可能丢失软件恢复入口的电源实验。
- 首次自动认领 owner 代码与限定主机测试通过：仅存储明确 `-ENOENT` 时开放，有界窗口/退避；身份认证后授权，
  不再依赖 PTT 链路/长短按。App 首次认领提示已同步，相关单测与 APK 构建通过（`/tmp/shaniu-auto-claim-android.log`）。尚未部署或手机认领。
- 三键标准 `/dev/buttons` 绑定已写，GPIO层用既有芯片接口；轮询模式不提供中断通知。产品键事件/音量处理正在接入。
- 提交结果恢复仍在改造：优先使用与待定事务绑定的密封控制密钥、固定证书 TLS 认证证明已提交；明确未认领设备仅允许认证后的只读回执查询。
  失败不得清空待定事务，不以删除认证或8秒PTT流程替代恢复。
- 三键产品配置已同时接入 CP/AP，发布端不再启用诊断 PTT；主机键策略通过。413 clean 目标构建失败于 CP GPIO 输入接口仅有 AP 实现，正在补芯片 CP 适配；未签名、未安装。
- 提交恢复 App 单测与 assembleDebug 已再次通过；已提交事务以对应控制密钥认证确认，本机提交失败保留未确认，退出/取消使旧回调失效。移除8秒指引。仍未实机认领。
- OTA 实际缺口复核：当前 App 升级页仍读取 Gateway 发布列表，不能算直连升级已实现。后续复用设备 HTTPS OTA source/manager，BLE 仅传认证控制，Wi-Fi 拉取大包。
- 413 最终 clean 构建、包/签名校验通过（包 SHA256 `6df73838c8348fd9e8ce8a2a16144ab69211e59f61aceb90376d65a595e8969a`），CP/AP 产品键实际配置均启用。
  两次 HTTPS 部署失败：首次 CP 暂存 progress=131072 返回 -EIO；降低服务器发送突发后第二次 AP progress=262144 返回 -EIO。均未发送重启，411继续运行。停止HTTP写入重试，下一步隔离下载源与Flash暂存；临时HTTPS已关闭并移除私钥。
  日志 `/tmp/shaniu-413-ota-live.log`、`/tmp/shaniu-413-ota-r2.log`，候选与App摘要 `out/shaniu-product-acceptance-20260910/candidate413.json`。
- 小米10实际安装新版App并进入添加页，BLE查找/NFC碰一碰入口可见且无长按PTT提示（`phone-discovery-entry.json`），仅UI入口通过，不算认领/NFC射频成功。
- 后续源码新增显式自动语音begin/end接口，复用capture仲裁，预填失败保留借用直到取消join，不上传失败回合；cloud/runtime、vision、memory主机测试通过。这些后续源码不在413中。
- 部署隔离补充：本地HTTP对照被411协议策略拒绝（-EPROTONOSUPPORT，phase=0），未进入写入。SDNAND经MSC新增 `/ota413` 公开签名文件并主机校验后安全弹出，USB已恢复CDC；411缺少直接挂载该AP目录的控制入口，尚未执行file OTA。两次HTTPS的-EIO根因仍未确认，不继续盲目写入重试。状态证据 `deployment413.json`。
- 官方唤醒资产复核：仅找到9条合成样本、1 epoch的既有管线测试模型，不能视为可交付唤醒模型。官方推理运行时可复用；有效模型及真实多人/噪声验证仍缺。
- 本轮工具模型已指定 Luna low 与 Terra medium；实际运行模型未独立可核验。goal 工具仍绑定旧 blocked 目标且拒绝新建，
  未虚报旧目标完成，工程工作继续以本计划恢复。新增源码未提交/推送。

- 自动语音基础补充：连续MIC listener生命周期主机测试通过、listener/HTTP source及最新INFO runtime已用实际AP ARM编译命令编译通过（仅对象编译，不是完整固件或实板证明）。有效唤醒模型与owner接线仍缺。
- INFO=9受认证版本查询及旧固件能力位兼容单测、APK构建通过（`/tmp/shaniu-info-android.log`）；手机已安装此APK。真实首页检查发现更新入口仍只在设置页，已补首页入口但该补丁尚未重装。
- App本地bkpack检查/选择入口正在验证：读取实际catalog与镜像哈希，不将完整性冒充签名信任；第一次单测编译失败已修，第二次发现Android JSON桩依赖，正改用已有Gson。未开始App OTA，未修改实板411。

- 手机本地固件检查已实际通过：首页下方“固件更新”→系统选择器→真实413包，显示 `18.6.349+413`、aidk_ai_toy、镜像2576384字节；不把完整性当签名。证据 `out/shaniu-product-acceptance-20260910/phone-package-inspection.json`，包含安装APK哈希。尚无手机发起升级或板端版本切换。
- SOU1请求解析与SDC1 OTA 10–14分片/认证/错误保留/清理测试通过；C端TLS控制、云语音/图片/记忆回归通过（`/tmp/shaniu-ota-control-host.log`）。Android OTA协议限定单测通过；尚未接实际升级。
- HTTPS源新增独立服务器认证入口，原双向TLS入口仍强制客户端证书；主机公开头98项和AP ARM对象编译通过，TLS连接及签名/摘要不匹配运行测试尚未执行。正接既有OTA manager/store，未改SDK/NuttX。
- 云服务未创建时的STATUS可继续提供本机能力/网络快照，空云会话turn明确未知，不伪报idle；相关云运行期回归通过。所有这些后续源码均不在实板411或签名413包中。

- 手机 HTTPS 来源实测已开始，输入包通过 ADB 文件传输后 SHA256 与 413 发布包一致。首轮 stdin 复制损坏已隔离；正常 TLS 连接仍在 catalog 握手阶段失败，尚未通过手机 HTTPS 验收。记录 `out/shaniu-product-acceptance-20260910/phone-https-source-result.txt` 与 `phone-https-source-apks.json`；未触发板端升级。
- App 已接直接 OTA 分片上传、独立状态、期望版本/设备绑定持久化、防重复启动及前后台传输中断提示；成功判定必须实际 INFO 与 CONFIRMED 阶段一致。限定单测与 APK 构建通过，手机尚未安装这批 UI 后续修改，不能当作 App OTA 验收。
- 设备直接 OTA 已复用既有 manager/store，移除该路径对 Gateway 配置的依赖；审查修复已暂存/等待重启时的取消行为，无法撤回返回 -EALREADY 并保留记录，不伪报取消。取消策略主机测试与实际 AP 对象编译通过，完整新固件构建/安装和物理验收仍缺。

- 414 候选 `18.6.350+414` 完整构建、包校验和 MCUboot CP/AP 签名验证通过（SHA256 `78d6b28afa9665b44df14c48ef8884126990365f51a522d7e404d8e44a0af522`）。随后审查补上重启恢复的 durable staged intent 取消保护，414因此不安装；新改动已通过取消主机测试和AP对象编译，需下一候选。记录 `candidate414.json`。
- 手机TLS第二轮仍在正常catalog握手失败，异常为 CertificateException 且证书仅CN；正在补SAN证书并同条件复测，不取消TrustManager或主机名检查。COM8再次只读确认411/A/confirmed/faults0/recoveries0，日志 `board-after-phone-https.log`。

- 小米10 HTTPS 来源实测通过：159请求/2576998字节，真实413包 catalog/AP/CP SHA256全部一致；错误主机名与无受信证书拒绝，阻塞客户端关闭及唯一临时密钥/解包目录清理通过。SAN证书修复保持系统TrustManager/hostname校验，App实际安装哈希 `ce40687a1dcef6403ef2770436e9e959417047c557cfa56834e6e7d8c37cd46b`。证据 `phone-https-source-result.txt`、`phone-https-source-apks.json`。这是手机自动化通过，不是实板OTA；实板仍411。
- 自动唤醒会话owner已新增并有主机生命周期测试，正在补终态锁存/停止等待期间追加帧与目标ARM适配。有效模型、runtime接线、真实唤醒及语音图片验收仍缺；未使用模拟输入冒充物理证据。

- 自动唤醒owner已通过真实window端点的主机回归：END后追加帧与auto_end等待、空唤醒后重监听、MIC初始化失败清理及1秒退避恢复。已加入KWS条件构建并用实际AP编译命令编译通过；有效模型和runtime接线仍缺，不能声称唤醒可用。日志 `wake-owner-host.log`。
- OTA恢复审查确认CP可启动提交早于AP STAGED记录的断电窗口；正在将已验证目标身份提前到Flash写入前由AP owner持久化，worker等待明确批准，任何不确定提交结果保留intent。此修复未完成前不安装候选，也不以返回取消代替回滚证明。

- 当前手机App已真实进入NFC_ARMED，411执行一次HCE返回 `rpc=0 operation=-93`，手机未收到selection；与旧超时区分记录，尚不认定射频或认领成功。日志 `nfc-real-once-board.log`/`nfc-real-once-phone.txt`。正核查选择结果到ISO-DEP激活的SAK处理，未去除协议或身份校验。

## 产品要求校正与当前交付门（2026-09-09，权威）

以下要求覆盖本文更早的 PTT 优先、有限 PTT gate、Gateway 依赖和“可选/后续”措辞；旧文字与证据保留为历史记录，不能作为当前产品范围或完成声明。

| 产品要求 | 当前状态 | 当前证据/缺口 |
|---|---|---|
| 中间 K2 为电源键（实测区分真正断电与待机）；左 K1 音量减、右 K3 音量加 | 未实现 | 需要按键映射、真实断电/待机读回及实板观察；不能沿用旧 PTT 映射 |
| 官方唤醒词“你好，openvela”→自动录音/端点→MiMo→TTS；重唤醒、打断、离线恢复 | 未实现 | KWS/端点/会话接线及实板连续、打断、断网恢复均缺；主机测试不构成产品验收 |
| PTT 仅内部调试入口 | 已实现未验证 | 仅可作诊断/回归辅助，不能作为产品能力、验收前置或发布 gate |
| 未认领设备自动受控发现；BLE/NFC 共享认证认领与安全；BLE 配网及 Wi-Fi/云配置持久化 | 已实现未验证 | 现有 BLE/NFC/持久化各有源码或主机证据，认证联动、重启恢复和实板认领仍缺 |
| NFC 必须提供物理证明；“拍照看看/看看眼前”→真实 JPEG→云端→TTS | 已实现未验证 | 412 主机/签名候选证据通过，未安装；411 无实板图片理解或物理 NFC 证明 |
| App 首页下方固件更新入口；签名/兼容 OTA 进度、错误、重连、版本、回滚 | 已实现未验证 | 底层 OTA 与 UI/主机检查存在，受信源、跨重启状态及实板升级/回滚证据缺 |
| Wi-Fi 承担固件等大包传输，BLE 承担发现、配网和必要控制；不要求 Gateway | 已实现未验证 | App 发起升级、断网恢复尚未闭环；不新增批量设备控制范围 |

状态含义：`未实现`=尚无可交付实现；`已实现未验证`=源码/主机或候选包已有证据但无规定实板/物理验收；`自动化通过`=目标自动化检查通过；`实板通过`=指定版本和步骤的物理证据；`外部阻塞`=需用户、设备、服务或环境输入。当前版本与证据以本文顶部“当前交付状态”为准；主机镜像/运行测试通过不改变实板验收状态。

历史基线日期：2026-09-02；本轮执行顺序更新：2026-09-07（第 17.5–17.6 节，复用边界、功能完善计划与实施结果）。

目标产品（2026-09-09 修订）：运行于 AIDK AI Toy 的虚构 AI 伴侣“傻妞”，板端直接
调用可配置的云端模型（首个适配为 MiMo），Android 负责认领、配网、凭据配置及设备控制。日常 MiMo 对话不依赖
额外 Gateway、电脑或手机常驻。本节新决策优先于下文历史 Gateway 实施记录。

## 当前检查点（2026-09-09）

- **已部署且健康：**签名 HTTPS OTA `18.6.346+410` 与 `18.6.347+411` 均已安装并自动确认；当前为
  `18.6.347+411`，CP/AP/CPU2、RPMsg 与 supervisor 均健康，`faults=0`、`recoveries=0`。410/411 的精简证据在工作区
  `/home/lijian/project/open-vela/out/shaniu-acceptance-20260909/`（411 为 `ota411.log`）；原始 411 串口记录为
  `/tmp/shaniu-411-ota-live.log`。这只确认该 OTA 及启动健康，不替代产品功能验收。
- **本轮完成：**411 修复了 NFC service 对旧 `CL_MFRC522` 的依赖，改为匹配板载
  `CL_MFRC522_FRAME`，并补齐 CMake/Make 的公开头路径及延迟初始化重试。实板启动出现
  `BKNFC SERVICE READY`（0.47 s）；初始 idle 为 pending `-2`，注册后 4.15 s 报告 field released，证明服务
  已启动并执行关 RF。手机 NFC 保持开启，间隔 20 秒两次确认 App 保持前台；随后通过真实 UI 一次进入 `NFC_ARMED`。但 `bknfc hce` 超时 `-110`、随后
  `scan present=no`，未获得选卡/APDU 或认领成功；不归因硬件。NFC 仅辅助定位/认领联动，不作为认证。
- **已恢复基线：**经授权清空 SD 后，FAT 卷标为 `SDNAND`；项目默认显示包已恢复并完成 SHA-256 核验，双屏
  `READY` 且 `last_error=0`。左右视觉映射仍未验证；卷标变更本身没有证明或修复此前 `ENOTDIR` 根因。
- **411 只读功能验收：**复用现有七项 AIDK HIL 合约，AP 健康、Wi-Fi 原生路由、语音状态、双屏状态、电池/温度读取、加速度采样、NFC 在场查询共 11 步全部通过。机器结果为上述工作区证据目录的 `aidk411-safe.json`，串口为 `aidk411-safe.log`；不替代声音、屏幕映射、传感器标定或已知卡片的物理观察。HIL 成功条件已按源码枚举由错误的 `link=2` 改为严格 `link=3`，继续拒绝连接中与未连接状态，5 项主机合约测试通过。
- **仍阻塞产品闭环：**板端 PTT 服务为 `link=1`，但尚无云配置或 Cloud 状态。下一实板步骤是既有 BLE
  认领→云配置提交→一次真实 PTT→MIC→MiMo→DAC 闭环；随后各执行一次取消与断网释放。Windows 原生
  `192.168.1.2:18406` 已完成 32-byte TCP 回显和 HTTPS OTA，WSL 服务路径失败，不能据此归因全部网络问题。
- **范围与发布：**比赛版保持 MiMo 为主；IndexTTS 私有模型是后续路线，不新增声音切换入口。新增改动尚未提交或
  推送。模型路由分别指定 Terra（盘点/进度）与 Luna（局部 HIL 合约修复），实际运行模型未独立验证。

有限验收标准是：同一已确认镜像完成一次非零 MIC、MiMo 回复、可听 DAC 播放和播放后短期上下文提交，并在取消与
断网各一次确认 MIC/DAC/worker 有界释放；BLE 认领须另行验证提交和重启恢复。未通过的 KWS、长期记忆、NFC APDU、
视频和 AEC/并发能力继续关闭或不声明。

## 当前续作：等待实体认领期间的独立任务

- 411 最新只读串口仍为 PTT link=1、pressed=0、presses=0；未观测到本次启动后的按键输入。
  已请求真实长按 3 秒后松开，不通过软件注入替代物理认领验收。
- 拍照理解先补通用 Chat Completions JPEG 请求/客户端，复用现有 HTTPS、截止时间、取消和回复解析；
  图片上限 512 KiB，借用不可变 JPEG，按块 Base64 编码，不保留完整编码副本。
  请求/客户端已实现，独立 JSON/Base64 分块测试和真实 OpenVela webclient 的本地 TLS 测试通过；
  AIDK CP/AP 目标构建通过（`/tmp/shaniu-image-client-build.log`）。尚无实板图片理解证据。
- AP 本地 JPEG 复制接口已实现：复用同一 V4L2 capture helper，互斥 trylock 使 snapshot/record 不并发占用相机。
  容量不足拒绝，退出时释放 V4L2；关闭失败则擦除已复制数据。`run-vision-core` 主机测试通过，尚未单独实板验收。
- 语音入口限定为用户明确说“拍照看看”或“看看眼前”，仅忽略外围空白/结尾标点；否定句和普通谈话不触发相机。
  已接入现有云 worker 的 ASR 后阶段，复用同一 TTS、取消、播放完成和历史提交；不由模型回复触发拍照。
  `run-cloud-runtime` 与 `run-cloud-vision-runtime` 两种宏配置均通过，覆盖否定句、采集失败、采集后取消、播放后提交。
  取帧前后均检查取消/截止；本地 capture 复用已有屏幕拍照反馈，`run-vision-feedback` 通过。
- 新增功能已完成独立 counter=412 的 AIDK 构建与签名 OTA 发布校验，版本 `18.6.348+412`；
  候选包为工作区 `out/shaniu-vision-412/package/firmware-aidk_ai_toy-v18.6.348+412-ota.bkpack`，尚未安装。
  最终 AP ELF 保留 capture/image/client 三个调用符号；证据位于工作区 `out/shaniu-acceptance-20260909/vision412-*`。
  板端仍为原已签名 411，最后检查 PTT presses=0、pressed=0；无云配置，真实认领/语音/图片上传未验收。
- 现有 `bkvision-v2` snapshot 只返回元数据，在关闭 V4L2 前未向云端转交像素。
  产品接入须在 AP 内由现有相机 owner 交付有界 JPEG，再释放相机并由云 worker 上传；
  不通过 RPMsg 传递地址，不新增并行相机 owner，不延长 V4L2 缓冲持有到网络完成。
- 接口依据：<https://mimo.mi.com/docs/en-US/quick-start/usage-guide/multimodal-understanding/image-understanding>，
  本日官方文档列出的图片理解模型为 `mimo-v2.5`；通用配置继续由使用者选择具备图片能力的模型。
  只有请求测试通过不能宣称拍照理解或当前 PTT 主线已完成。

## 历史证据（截至 411 前）

下列按版本叙述保留为源码、主机测试和当时实板过程的历史证据；与上述当前检查点冲突时，以上述当前检查点及其日期化
证据为准，不得把历史构建或旧版本状态表述为新的实板验收。

复位后实板核对（2026-09-09）：COM8 在 115200 下返回 NSH，安装版本为
18.6.336+400，A 槽 confirmed、counter=400；AP supervisor HEALTHY，
faults/recoveries=0/0，CPU2 ready=1。`/data/shaniu` 仅有 identity 目录，
其中 config.bin 为 719 bytes；未读取凭据正文。当前没有 `BKVOICE CLOUD`
状态行。源码中 configured/connected 属于旧 session，不能单凭这两个零值
判断直连网络失败；产品配置根为 AP `/cpdata/shaniu`，映射到 CP `/data/shaniu`。
下一实板步骤应恢复认领与云端配置路径，再验证真实语音，不重复排查已恢复的串口。
本次只读检查未复现挂起，也未证明此前无响应的根因。

首次认领按键启动缺口（2026-09-09）：实板 `ps` 无 CP 按键采样线程，
`bkvoice status` 的 PTT link=0。源码仅在 connect 命令启动采样，连接失败和
disconnect 又会停止它，rcS 未启动，因此未配置云端时无法通过长按发起认领。
新增产品版 PTT_AUTOSTART 和幂等 buttons 入口，由 AIDK openvela_cp rcS
启动真实 GPIO 采样，连接失败/断开不再停止产品按键服务；drivercheck 行为保留。
AIDK CP/AP 构建通过，已检查生成 rcS 含启动命令；尚未安装此修复，不能标记
实板按键恢复。小米 10 已重新连接 ADB，新版 App 更新安装成功，应用数据保留。

402 安装尝试：18.6.338+402 签名 OTA 发布校验通过，但 COM16 握手超时，
板端 manager 保持 idle、progress=0/0，未开始写入。随后软件 reset reboot
成功返回 NSH；启动日志明确 USBCDC consumer=/dev/ttyGS0，当前构建配置
未启用 CONFIG_BK7258_OTA_SOURCE_USB。普通 CDC 枚举不代表 OTA 服务存在，
不能据此重复 USB OTA 尝试或归因硬件。402 仍未安装，应使用已支持的更新路径。

403 实板更新：新独立 BL1/MCUboot 密钥构建并签名的 18.6.339+403 全量包，
通过 COM8 BK Loader 软件复位接管、擦除、写入成功。启动查询确认 pair=confirmed、
counter=403、AP healthy 且 faults/recoveries=0/0；无需云配置即出现 bkvoice-button
线程及 PTT link=1、pressed=0，修复前为线程缺失、link=0。这证明开机按键租约
恢复，不替代实际长按认领验收。原身份 resume 事务被 ready 阶段以 -EBUSY
拒绝，未重复供应，继续核查身份/配网 owner 状态。

404 实板更新：发现 CP provision 在按键线程运行时无条件拒绝，因此产品常驻
模式跳过该旧检查，仍由 AP CONFIG_BEGIN 的真实事务忙状态守护；诊断模式保留。
新独立签名全量 18.6.340+404 下载、启动 confirmed，AP healthy、PTT link=1。
身份供应已通过 ready/传输，commit 返回 -ENODEV。实板 mount 仅列出 /etc、
/proc，/dev/mtdblock0 存在而 /data 未挂载；手动执行启动脚本同一普通挂载
返回 errno=14 (EFAULT)，未格式化。当前下一阻塞点是片内 LittleFS 挂载，
不是 SD NAND 容量/一致性，也不能将身份事务记为完成。

404 私有存储恢复：NuttX 将 LFS_ERR_CORRUPT 映射为 EFAULT；检查已下载镜像
0x600000 起 1 MiB persistent_data 全为 0xff，原因为采用空白基底后未初始化，
并非已证明的 SDK 读错误。仅对该已知空白片内分区执行一次 LittleFS 初始化，
未格式化 SD NAND、未修改 SDK/NuttX。原身份供应随后成功（655 bytes）；
再次软件重启后 /data 自动挂载为 littlefs，identity/config.bin 仍为 719 bytes。
凭据正文未回读。身份持久化已通过本次重启验证；App 认领及云端配置仍需继续。

小米 10 认领界面：实机进入首次添加页面并成功启动 BLE 扫描，无权限拒绝；
未开启实体认领窗口，不能据此判定设备发现或认领成功。补齐首次添加的
“空闲时长按说话键 3 秒后松开”和连接确认的“先松开再短按”提示，恢复流程
仍保留 8 秒说明，不用首次添加时长替代恢复操作。APK 构建通过。

NFC 辅助认领开始实现：现有 MFRC522 为读卡器，手机采用 Android HCE 卡模拟，
不是让手机直接读取板子为标签。当前 NuttX MFRC522 接口只有选卡/UID 和 MIFARE
操作，尚缺面向 HCE 的 ISO-DEP/APDU 交换。Android 新增非支付 HCE 服务，
只在认领页面前台且设置 preferred service 成功时响应；离开页面立即关闭应答，
要求手机解锁。当前 SELECT AID 只返回协议版本，不发送凭据、不授予认领权限。
命令截断、变异、额外数据、非前台拒绝测试及 APK 构建通过；已安装小米 10，
系统 NFC=on，dumpsys 确认 ShaniuHceService/AID 注册。尚无射频 APDU 验收，
也未接入设备定位/现有 BLE 认领事务，不可称“碰一碰添加”完成。
下一步补板端标准 ISO-DEP 交换及其超时/撤场处理，再接已有 BLE 身份校验。
参考：Android https://developer.android.com/develop/connectivity/nfc/hce
与 NXP https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf 。

NFC 后续进展：小米 10 dumpsys 确认认领页将 ShaniuHceService 设为 preferred
foreground service；同轮板端 bknfc scan present=no，当前天线贴合未确认，
不据此判断射频硬件故障。新增维护补丁
nuttx/patches/contactless/0002-mfrc522-crca-frame-exchange.patch：选卡后有界
CRC_A 帧交换，发送最多 62 字节，由驱动追加/检查 CRC，错误清空接收结果。
调用方仍须串行独占选卡状态，沿用当前硬件计时器；未实现 ISO-DEP 会话、WTX
或 APDU。test_mfrc522_exchange.py 在固定 NuttX 基线组合两个补丁，编译实际
新增函数并验证所有字节长度、错误传播、残留清理与异常回复，通过 UBSan。
补丁尚未接入目标构建/烧录，因此不得声称板端已有 HCE 交换能力。

帧交换构建接入：新增规范目录下完整 MFRC522 覆盖源码和公开扩展头，复用已有
vendor/beken/nuttx 目录映射及 CMake/Make 接口。AIDK 选择 CL_MFRC522_FRAME，
关闭 CL_MFRC522，构建禁止重复实现；保留独立板级 UART 适配。测试核对覆盖
源码与固定基线加两份维护补丁一致。AIDK CP/AP 构建通过，最终配置和 Ninja
确认新实现确实参与编译。尚未烧录，ISO-DEP/WTX/APDU 仍未实现，不是射频验收。

1. 手机配置 HTTPS 服务地址、协议类型、ASR／对话／TTS 模型及凭据；MiMo 作为首个适配。
   板端在私有片内 LittleFS 中持久保存凭据，不提供明文回读。
2. 复用音频和 TLS 基础，完成板端 ASR → MiMo 对话 → TTS → 扬声器，并验收
   电脑 Gateway 关闭、App 退出后的独立 PTT 与多轮对话。
3. 人物配置和可选长期记忆使用 128MB SD NAND；近期上下文在 RAM 有界保存。
   SD 数据设计须处理 USB MSC 导出边界，不能将目录隐藏等同于访问保护。
4. 比赛版以 MiMo 声音为主，不做声音切换入口。IndexTTS 2.5 私有声音模型作为
   后续独立路线保留，推理运行位置另行确定。

长期记忆基础（2026-09-09，尚未启用）：私有 LittleFS 策略默认关闭，绑定当前
SCB3 owner；SD 快照只保存 AES-GCM 密文，支持密钥轮换使旧密文失效。
新增有界快照保存/恢复及现有 preferences owner 下的串行存储回调，避免另建
一套 mount/MSC 所有权。缺失、篡改、截断和读取失败不会返回残留明文；重命名后
同步失败返回 EINPROGRESS，不宣称旧快照仍有效。主机策略/快照故障注入、
存储占用与清理测试、AIDK CP/AP 构建通过。已接入已提交 SCB3 owner 和运行时：
首次真实对话在工作线程中读取策略并恢复，连接探测不读取；默认关闭时不访问 SD。
播放确认后由独立任务保存最近三轮上下文，保存期间保持 busy，控制线程不执行
文件 I/O；取消/配置清理必须等待任务退出。恢复数据在卸载/释放成功后才进入 RAM，
保存发布后的清理失败映射为 EINPROGRESS。SMH1 显式编码轮数、人物和文本长度，
不把含 padding/size_t 的 C 结构体直接写盘；人物不符、截断和多余字节均拒绝。
主机验证含最大长度、所有截断边界、恢复内容进入下一次请求、关闭时零 SD 调用、
播放失败不保存、保存阻塞时拒绝提前释放，以及相同 owner 刷新与更换 owner 的忙状态。
App 已接入跨重启记忆开关、删除确认和 SDC1 异步回执：新命令受理只显示处理中，
必须在后续状态中 pending 消失、无失败且 enabled 符合目标后才显示成功。断连清除
本次操作归因，重连只展示当前状态，不自动重放。私有策略发布不确定时，进程级
锁定后续修改/读回确认，更换 owner 不解除该状态；重启后再读持久结果。
默认仍关闭。开启不会用旧磁盘快照覆盖已有 RAM 对话；关闭保留密文，删除轮换密钥、
关闭保存并在策略确认后清空 RAM。当前只实现最近三轮上下文的跨启动保留，不等于
完整的长期事实记忆。C 运行时/协议测试、Android↔C 新命令互操作及 104 项 Android
测试通过（无跳过），目标构建成功。新版 APK 已安装模拟器，未绑定页面布局已核对；
已绑定控件及 BLE 实板读写尚未验收。未烧录，FAT 断电恢复未验证，不能称长期记忆
功能闭环。同一密钥下旧密文回放不可检测，后续恢复策略需保留此边界。

真实云服务验证（2026-09-09）：使用用户授权的 MiMo Token Plan 专属地址，
由当前 C 客户端 + OpenVela webclient + mbedTLS 完成固定合成输入联测。
对话成功 1342 ms；TTS 成功 1191 ms，24kHz PCM16 单声道 92160 bytes、
非零峰值 19246；主机转换为 16kHz 后 ASR 成功 688 ms，输入 61440 bytes，
预期测试词匹配。只记录汇总，临时凭据配置和合成音频随测试目录自动清理。
该结果验证主机运行的真实协议路径；主机 ffmpeg 转换不替代板端 Speex/DAC 验收。
发现并修正 App 默认普通接口与 Token Plan 凭据不匹配的问题：新增明确的接入方式
选择，默认 Token Plan，可选择标准接口或自定义服务，不从 Key 静默推断/改写地址。

设备状态反馈（2026-09-09）：ready 只代表语音会话已初始化，原按键链路 link
不是 Wi-Fi 状态。SDC1 已通过现有芯片本地连接快照 + AP native lease 匹配报告
wifi-known/wifi-ready；不发起网络探测，不回传 SSID、凭据或 IP。App 区分未知、
未就绪和 Wi-Fi 已连接，旧固件没有字段时保持待确认，不把 Wi-Fi 连接当成云服务
可达证据；记忆任务单独显示，上次对话错误提供重试提示。Android/C 字段互操作、
界面状态映射测试和 AIDK 构建通过；尚无实板断网/重连验收。

手机云服务预检（2026-09-09）：发现 CloudEndpoint 默认协商版本可能与板端
仅 TLS 1.2 不一致。已将手机握手显式限定 TLS 1.2，保留系统 CA、HTTPS 主机名
验证及连接超时。APK 构建并安装模拟器；显式 cloud_probe=1 instrumentation
通过真实 MiMo Token Plan 端点握手和系统信任根提取，不发送 API Key 或模型请求，
同时原有 Android Keystore、证书绑定、TLS 分块及错误 pin 拒绝测试通过。
该证据仅覆盖手机预检，未替代板端使用已供应 CA 联网和物理播放验收。

已连接界面验收（2026-09-09）：真实 MainActivity 注入测试内存快照，验证已关闭、
记忆处理中、结果未知及 Wi-Fi 未就绪状态，按钮启用条件符合预期。发现并修复
记忆任务 busy 时误显示“停止这次对话”的问题；存储事务仍不可被对话取消按钮
伪装成已取消。ui_probe=1 模拟器测试通过，已核对合成截图中三个记忆入口可见。
测试不写入真实绑定资料、不打开 BLE、不操作开发板；这不是三端物理验收。

Gateway 已归档为 `archive/shaniu-gateway-20260909`，仅作历史参考与测试工具。
CCF1 通用手机/板端配置已接入 SCB2 认领事务和云端请求，代码与主机验证通过，
实板直连仍未验收。官方唤醒词优先“你好，openvela”，“小冰”后置。

当前实现：CCF1 按协议类型配置服务地址和三个模型，不将厂商名作为公共接口。
板端配置校验、Android 配置测试及 APK 构建通过；本轮 AIDK 的 CP/AP/BL1/BL2
目标构建通过，未烧录。Chat Completions 音频请求支持
WAV/Base64 按需编码，直接匹配 OpenVela webclient 的 body callback，不保留完整
Base64 副本；8 种 PCM 长度（含 30 秒上限）、4 种拉取分块大小通过独立 JSON、
Base64、WAV 解码核对。该音频格式不是 multipart transcription，也不意味着所有
声称兼容 OpenAI 的服务都支持；具体协议适配与能力验证仍需完成。

TLS 已增加显式 server-auth-only 模式用于云端 HTTPS；默认仍要求双向认证，
所有模式都校验服务器 CA、域名和可信时间。此前 6 项 TLS 互操作测试通过。
CCF1 已嵌入认证认领事务；直连服务探测成功后仍需完成本地确认和配置提交。
已新增 OpenVela webclient 到现有 TLS provider 的 HTTPS 适配，保留总期限、
取消、响应大小限制、凭据缓冲清零，并拒绝重定向。ASR client 已将 PCM 编码、
HTTP POST 和文本解析串联；真实 webclient 的部分收发、chunked、401、超大响应、
取消和超时测试通过，真实 mbedTLS 与本机 HTTPS 服务的组合测试也通过。测试
不依赖冻结 Gateway，未调用真实云模型。CP/AP/BL1/BL2 构建通过，未烧录。
本轮新增 Chat Completions 对话、最多三轮 RAM 上下文与 MiMo 流式 TTS adapter。
上下文由播放完成后的显式 commit 更新；请求失败或取消不写入，支持清空和淘汰
最旧一轮。MiMo 扩展参数只在 MiMo dialect 下发送。TTS 严格检查 SSE、Base64、
单 choice、stop 与 DONE 顺序，有界接收，取消/截断会失败。2026-09-09 已补
协议 1 的标准 `POST audio/speech`：配置的模型、input、固定音色 alloy 和
`response_format=pcm`，直接流式消费 24kHz PCM16-LE，再复用现有 16kHz 播放适配。
兼容范围仍要求 ASR 支持 Chat Completions input_audio，不能将任意文字或
multipart transcription 模型视作可用。原始音频要求 audio/pcm 或
application/octet-stream，JSON/其他音频格式、非 2xx、空流、奇数长度、截断和
超过上限均失败，部分播放仍由原 turn owner 中止。参考标准协议：
https://developers.openai.com/api/reference/resources/audio/subresources/speech/methods/create
及 https://developers.openai.com/api/docs/guides/text-to-speech 。
标准 TTS 主机故障测试、本机真实 mbedTLS HTTPS 双适配联测、AIDK 构建与 App 构建通过；尚未调用真实标准 TTS 服务，未烧录验收。
后续发现标准 PCM 路径的真实缺口：HTTP 可按奇数字节分块，但播放器此前逐块
要求偶数长度。已在既有播放适配中保留一个未完成采样字节，下一次回调补齐；
整流结束仍有半采样时中止并释放 DAC，90 秒长度上限包含暂存字节。
真实 Speex 验证覆盖 8 种长度 × 10 种分块，含逐字节及奇数分块，与完整输入
输出逐字节一致；尾部截断、长度边界、滤波尾部及取消/排空测试通过。
实时云测试的音量统计也改为跨分块补齐采样，本机 HTTPS 回归及 AIDK 构建通过。
同轮 COM8 115200 回车探测可打开但 3 秒返回 0 字节，无 NSH；已关闭串口，未复位、
未刷写，因此新增播放修复还没有实板证据。

参考官方说明，MiMo PCM16 是 24kHz mono：
https://mimo.mi.com/docs/usage-guide/speech-synthesis-v2.5 。现有板端播放器及 DAC
基线仍是 16kHz。本轮已复用 OpenVela SpeexDSP（fixed-point）完成 24→16kHz
重采样，`bkcloud_synthesize_turn` 在收到第一块音频时才向既有 turn arbiter 申请
播放资源；按原帧长输出、补齐滤波尾部与最后一帧，结束后仍走原 drain/result。
请求失败或取消会清理该轮；返回成功只表示已安排 drain，不等于扬声器播放完成。
8 种输入长度、5 种分块、滤波尾部、混叠抑制、DAC 取消和异步完成测试通过。
重新生成配置时修复了 VOICE_TLS 与 WEBCLIENT 对 NET_SOCKOPTS 的循环依赖；
干净目标构建实际启用了 SPEEXDSP/FIXED_POINT/WEBCLIENT，并编译了播放适配。

本机真实 HTTPS 联测已串联 ASR、两轮对话与流式 TTS，测试验证的是协议和音频
字节，不是云模型质量或实板出声。配置事务与运行时 PTT 已接线：录音停止并回收采集
线程后启动 ASR/对话/TTS 工作线程，播放排空成功才提交 RAM 上下文。取消和清理必须
等待工作线程退出；配置提交前禁止 PTT 抢用确认按键，播放超时不提交上下文。
主机模拟按键、超时及取消生命周期测试和 AIDK 目标构建通过，不能称为实板产品闭环。
直连状态查询已区分 cloud mode/ready/busy；工作线程持有音频回合时返回
`turn_known=0`，不并发读取回合或误报空闲。原有 `BK7258_VOICE_HIL_TEST` 自动采集
入口已接入云语音路径，仍须显式发起、限时 200..5000 ms、标记 `physical=0`；
不新增录音实现，也不算物理按键验收。正式配置保持关闭；正式目标构建及开启 HIL
分支的目标编译器语法检查通过，尚未烧录验证自动采集。

Android 普通配网页已支持云端地址、模型和凭据，现使用 SCB3 一同提交独立控制密钥；旧 Gateway 地址仅在
开发者模式要求。APK 构建及配置编码测试通过，尚未完成手机/实板事务验收。
当前连接探测只验证对话服务，不能证明 ASR/TTS 模型均可用。云任务已复用 OpenVela
`getaddrinfo(AF_INET)` 解析服务地址，遵循系统 DNS 缓存；不继续依赖手机保存的旧地址，
域名证书校验保持不变。解析在云工作线程执行，AIDK AP 配置将 DNS 单次收发等待设为
2 秒、尝试次数设为 2；这不是整个解析过程的硬期限，也不代表能立即中断 DNS。
取消期间仍持有任务资源，解析返回后检查取消状态，禁止继续发云请求。
主机模拟解析失败、恢复、地址变化与取消清理通过；真实板端 DNS 尚未验收。
App 普通入口已切换到直连配网路线，不自动连接旧 Gateway、不要求导入控制令牌；
旧服务联调须在调试版显式进入。本机认领结果与在线状态分开展示，尚未接通的
人物控制、手机升级等明确不可用。模拟器已验证首页、心情、设置、隐私、升级提示和
添加设备页面，89 项 Android 主机测试通过；真实认领/在线状态及板端控制仍待验收。
直连人物已复用 SD NAND 上的 KVDB 配置；正式 AP 开启已有偏好模块。每轮请求读取
人物风格，介质忙碌/读取失败时保留上次值，首次使用默认为 gentle。主机测试验证了
quiet/playful 切换及读取失败回退；偏好存储回归通过。补齐 UnQLite、任务局部存储和
文件锁依赖后，正式干净构建通过，并核对最终配置确实启用 KVDB_UNQLITE 和
BK7258_PREFERENCES，尚未实板验收。
空闲直连会话允许偏好命令，录音/云任务/播放/配网期间仍禁止变更。
人物发生实际切换时清空 RAM 短期上下文；读取失败保留原人物和上下文，相关主机测试通过。
开启 VOICE_VOLUME_PERSISTENCE 时，偏好命令和播放已共用 LittleFS 音量存储，不再
读写旧 KVDB 音量；每次播放读取持久化值，旧内存 override 不再覆盖后来的设置。
未开启该选项的配置保留 KVDB 行为。主机测试覆盖外部变更、SD 占用、保存错误及
已保存但立即应用失败后的下一轮恢复。实板音量、App 人物/音量控制尚未验收/接通。
2026-09-09 本轮 COM8 查询可成功打开端口，但 8 秒内未收到 `bkota status` 响应；
串口已释放，未复位或烧录。PnP 正常枚举不等同于固件控制台可响应，当前安装版本未确认。
随后 COM16 可打开，但使用既有 OTA 协议发送 HELLO 后 5 秒内无 ACK，已关闭端口；
未发送 START 或写入 Flash，自动升级尚未执行。
再以 Ctrl-C/换行退出可能占用控制台的前台命令后查询，8 秒内仍为 0 字符，已释放 COM8；
该尝试未恢复控制台，也没有据此定位到硬件或固件根因。

失联操作回溯（2026-09-09）：07:41 左右仍可查询 `/data/ota-ca.pem`，
随后串口 `echo` 重定向上传公共 CA，先超过 CP NSH 80 字符限制产生截断错误，
缩短命令后等待提示符超时，之后被动采集为 0 字节。该时间窗口指向 CP
LittleFS/片内 Flash 写路径，尚无故障栈证明具体阻塞点；不能归因于已验收的
SD NAND 一致性路径。暂停此串口重定向上传方式。历史 g400 未启用 USB OTA
接收，故 COM16 无 HELLO ACK 本身不能证明整板死机。
当前源码检查未发现 MTD 返回分支漏解锁；当前构建 ELF 确认 SDK Flash 调用
调度锁和跨核 START/END 通知，但尚未证明与失联的因果关系，未据此改 SDK。

真实云接口验证（2026-09-09）：使用目标同源 C client、OpenVela webclient 和 mbedTLS，
通过 Linux 主机套接字直连已配置的 MiMo 服务；对话返回 0、耗时 1822 ms，TTS 返回 0、
耗时 1419 ms，解出 92160 字节 24 kHz 单声道 PCM，峰值 20218。未经过 Gateway。
仅记录状态、长度、耗时和音频统计，未记录密钥、对话正文或音频；临时配置已删除。
追加固定合成语音回环：TTS 解出 107520 字节 24 kHz PCM，主机 FFmpeg 转换为
71680 字节 16 kHz PCM，再由同源 C ASR 请求识别；返回 0、耗时 816 ms，结果包含
预期测试词。该轮对话 1527 ms、TTS 1775 ms，均返回 0。转换器为主机工具，不构成
板端 Speex、DNS/网络、MIC/DAC 或物理验收。临时测试音频与凭据随测试目录清理。

直连设备控制契约：复用 BLE/TLS 传输，目标命令为状态、取消、音量、人物设置；
AP 保持配置与音频的唯一可变状态所有者，App 只展示设备回执，不乐观宣称成功。
工厂 possession secret 只用于初始认领，日常控制使用手机随机生成的独立 32 字节密钥。
SCB3 在 SCB2 尾部追加该密钥，沿用现有私有配置事务，确保网络/云配置与控制身份
一同提交；密钥不得出现在状态或恢复响应中。旧 SCB1/2 仍可解码，但不具备新控制身份。
已完成 C/Android 编解码及非零/长度/尾部检查，相关主机测试和 APK 构建通过。
手机端已实现 Keystore AES-GCM 密钥封装，AAD 绑定设备及事务；待确认密文与回执
一同保存，确认后与有效绑定原子切换。16 项绑定测试通过，包括失败重试、重启恢复、
密文替换/丢失拒绝、跨设备拒绝和明文回调清零；APK 构建通过。测试使用 JVM AES 密钥，
2026-09-09 已在 Android 模拟器运行独立 instrumentation，实际 AndroidKeyStore
密钥不可导出、待确认密文经重新创建存储对象恢复、确认晋升和回调明文清零均通过。
测试使用随机别名与独立 SharedPreferences，结束清理；未验证进程被杀/掉电、
物理手机硬件保护或实板认领事务。
BLE 连接对象已接入协议 APPLY 前的控制密钥保存：从本次已上传候选提取，先加密
保存再允许 APPLY；保存异常禁止 APPLY，配置/事务/密钥临时副本清零。认领包测试
及 APK 构建通过，覆盖多分片候选、失败顺序、畸形长度和 SCB2 无密钥兼容。
普通 App 已切换 SCB3，每次新认领生成独立 32 字节 SecureRandom 密钥，编码后清零；
恢复仅查询原事务，不重发配置或生成新控制身份。设备证书指纹取自激活资料，与
设备 ID/事务一起纳入 AES-GCM AAD，待确认到有效绑定同时保存；指纹替换或删除
均拒绝解密，不退回无 pin 的日常连接。96 项 Android 测试及 APK 构建通过，模拟器
实际 Keystore 验证了指纹绑定、对象重建后的恢复和临时密钥/pin 清零。
尚待 BLE 日常认证和实际控制命令，以及进程重启/实板认领恢复；不能将配置/存储
测试通过算作认证控制完成。
旧 Gateway 控制仅保留在已冻结的历史调试入口，不新增 Gateway 依赖或复用其 bearer token。
日常控制 SDC1 会话核心已实现：仅接收 TLS 内完整帧，独立密钥认证、严格递增序号，
支持状态/取消/音量/人物的 AP 执行回调。认证失败、重复序号、非法参数均终止会话；
操作失败返回错误及未知字段，不伪造设置成功。主机测试及 ASan/UBSan 通过
（当前环境不支持 LeakSanitizer，单独关闭泄漏检查），AIDK 目标构建通过。
新增 control pair 已复用现有 TLS/GATT API 串联该核心，保留跨步响应，发送背压
期间不继续读取或重复执行。1..48 字节分片、连续 AUTH/STATUS、队列背压、认证
10 秒期限、TLS generation 失效和启动失败清零测试通过，AIDK 构建通过；测试
使用 TLS 接口模拟，不等于真实 TLS 联测。AP execute 回调已实现，复用云状态、
取消及既有偏好存储；录音/云任务/播放忙碌时拒绝设置，不在取消路径读取文件。
取消请求保留 cloud 会话，等待云工作线程和采集退出再释放缓冲，按键保持按下时
不重触发。主机模拟采集取消不上传、云工作线程取消后保留会话/不提交上下文通过，
AIDK 构建通过。产品 owner 现已从已提交 SCB3 快照获取独立控制密钥，打开日常
BLE 窗口并调用 control pair/AP execute；认领与控制共用一个 GATT owner。
日常连接及其断线清理不占用 PTT，身份替换仍受 busy 保护；八秒物理长按可转入
回执恢复，并等待音频取消完成。owner 测试覆盖自动窗口、普通按键、断线清理、
密钥复制和恢复转换，AIDK 构建及最终 AP 符号确认通过。Android 已实现 SDC1
对端及基于已绑定密钥/pin 的 DeviceControlConnection，复用 AndroidProvisionGatt
工作线程串行收发和超时，认证前不执行控制命令，超时不重放；设备忙碌和未知
字段按回执解析，不将取消回执等同播放排空。99 项 Android 单元测试及 APK
构建通过。App 页面已接入附近设备扫描、已绑定身份认证连接、状态轮询、取消、
音量和人物设置；后台退出关闭手机连接，界面以设备回执显示结果。
主机跨端 TLS 已联测；Android 设备提供者/实板尚未联测，未烧录，不能称为日常控制已实板可用。

Android 视觉改版已接入生成的陶瓷机器人插画、奶油白/深绿配色和图标导航。
首页与未认领心情页已在模拟器检查；未认领时提供添加设备入口。
1080×2400 / 560dpi（约 309dp 宽）检查发现插画将主按钮挤出首屏，已改为
按屏高缩放插画，复测主按钮完整可见；测试后已恢复模拟器默认 420dpi。
配网页默认 MiMo，将地址、协议、模型收进自定义服务区域，保留通用配置能力；
返回设备选择时清除输入凭据并使未完成的服务查询回调失效。
APK 构建与 99 项既有单元测试通过；不能据此认定实板配网、设置回执或全部
屏幕尺寸已经验收。

认领持久化失败保护：依据本机 Android 35 `SharedPreferencesImpl.commit()` 的
先更新内存、再确认写盘顺序，修复写盘失败后其他页面误读内存绑定的风险。
共享同一 preferences 的 backend 在失败/异常后停止读写，直到新进程从磁盘
重新加载；不尝试用可能同样失败的补偿写入覆盖待核对结果。新增两项回归覆盖
内存先变更再返回失败、异常、现有/新建页面和独立磁盘重载模型，共 101 项
单元测试通过。真实 Android Keystore/SharedPreferences 的正常保存恢复仍需
与故障注入结果区分，模拟写盘故障不等于手机断电测试。

SDC1 跨语言联测：Android `DeviceControlProtocol` 通过进程管道直连固件
`bk7258_control_session.c`，使用主机合成 AP 状态，验证认证、状态、忙碌拒绝、
取消、音量和人物设置，以及逐字节响应分片。包含该联测的 Android 102 项测试
通过，未跳过。复现命令在 Android README；C 可执行文件已列入 Gradle 测试输入，
避免未启用原生对端的跳过结果被复用。该证据不包含真实 TLS/GATT 或实板 AP。

日常控制真实加密通道验证：在既有 `test_provision_tls.py` 中接入产品
control pair/session，复用实际 workspace mbedTLS 与 20 字节模拟 GATT。
20 轮临时证书场景全部通过，新增 AUTH 按单字节 TLS record 分片、输出拥塞
不重复执行音量、重复序号拒绝、错误 owner key 拒绝、认证超时全对象清零。
这是主机 mbedTLS 客户端/服务端证据，尚非 Android TLS 对端或实板 BLE 验收。

App/固件加密互通：现有 host runner 可用 `SHANIU_ANDROID_INTEROP=1` 同时
构建两个原生对端并运行全部 Android 测试。App 的 `ProvisionTls`、
`ProvisionGattSession`、`DeviceControlProtocol` 经 20 字节密文管道对接产品
mbedTLS/control pair，通过指纹匹配握手、owner 认证、音量回执及错误指纹拒绝。
完整 Android 103 项测试通过、零跳过，原有 20 轮 mbedTLS 场景也通过。
该 TLS 客户端使用主机 JVM 提供者，仍未覆盖 Android 设备 TLS 提供者、真实
BluetoothGatt 回调或实板动作；临时证书/测试私钥退出后删除，未改动板端信任。

Android 原生 TLS 提供者验证：模拟器 instrumentation 使用临时 AndroidKeyStore
P-256 签名密钥、Conscrypt TLS 1.2 与产品 `ProvisionTlsChannel`，经内存 20 字节
密文分片完成双向明文核对，并拒绝错误证书指纹。与加密回执恢复/借用清零测试
一并返回 PASS。测试密钥按 Conscrypt 预哈希签名要求允许 NONEwithECDSA，并
允许 SHA-256 签署临时证书；该设置仅存在于 androidTest，未修改产品密钥策略。
这补足了 Android 模拟器提供者的基础证据，仍未证明手机 Conscrypt 与板端
mbedTLS 的真实 BLE 连接、物理按键、扬声器及存储写入。

设置/心情 UI：设置页已采用带说明的设备、声音、隐私和管理选项行，未认领
时突出添加入口；人物列表仅对已确认 persona 显示选中标记。debug 历史服务
入口缩为页脚，release 不显示。未认领设置页已在模拟器截图核对，APK 构建通过。
移除本机连接资料现在先关闭日常控制会话、停止轮询再删除持久化绑定，避免
旧认证连接继续存活；实板解绑后的断线效果仍需验证。

近期对话清除：SDC1 新增 `CLEAR_HISTORY=6`（空载荷），App 隐私页经确认后
发送，只有设备成功回执才提示完成。AP owner 在空闲状态清除 RAM history 和
文本缓冲，录音/云任务/播放/取消清理中拒绝；不修改 SD、凭据或人物设置，
不宣称删除云服务记录。主机覆盖已有上下文清除、重复调用和忙碌拒绝，C/Android
互通及全部 103 项 Android 测试通过，AIDK CP/AP 构建通过，未烧录。
长期记忆仍未实现；已确认现有 preferences/media_volume 租约可复用来约束
SD 与 MSC 互斥，后续仍需加密存储、开关/删除及导出边界验收。
本次 COM8 仅换行探测：端口可打开，3 秒收到 0 字符，已关闭释放，未复位或下载。

## 1. 文档职责与唯一入口

本文是傻妞项目的总入口，统一管理：

- 产品范围、非目标和成功判据；
- AIDK 固件、Gateway/AI、Android、模型/音色/UI 资产六条工作流；
- 跨工作流依赖、里程碑、验收门和发布物；
- 当前状态、风险、决策和下一执行队列。

专项细节由以下文档承接：

- [BKVoice 产品架构与适配计划](bkvoice-authorized-voice-app.md)：板端 App、Gateway AI、
  授权音色、视觉、UI 和 OpenVela 组件；
- [下一阶段单轮接话纵切计划](shaniu-next-stage-single-turn-plan.md)：PTT、出站 WSS、Gateway、
  下行播放、无串口验收和故障门；
- [原生 Android companion 计划](shaniu-android-companion-plan.md)：BLE 配网、控制台、视频、
  权限及后续微信小程序；
- [BK7258 构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)：构建、签名、
  打包、部署和实板证据；
- [AIDK 当前板测交接](aidk-ai-toy-board-test-handoff.md)：日期化硬件基线和明确缺口。

冲突时使用以下优先级：当前源码/配置与日期化证据 > Master Plan 的状态和优先级 > 专项设计 >
历史研究文档。Master Plan 不覆盖真实配置或实板证据。

## 2. 产品定义

傻妞是一台可独立启动、能听、能说、能看、能通过双圆屏表达状态的随身 AI 伴侣：

- AIDK 是身体和唯一运行终端；
- 用户 Gateway 是 ASR、VLM、LLM、TTS、唱歌和长期记忆的可替换算力端；
- Android 是可选的认领、配网、设置、视觉和隐私控制台；
- 手机不在线时，AIDK 仍能显示状态、执行本地按键、播报签名离线资产并明确降级；
- 人物 machine ID 固定为 `shaniu`，显示名为“傻妞”；
- `fictional-ex-girlfriend` 只表示虚构互动风格，真实身份始终披露为 AI companion 和合成声音。

产品体验不是“让大模型任意控制所有外设”，而是把板上外设封装为有限、可撤销、可审计的
语义能力，由确定性状态机和权限策略决定是否执行。

## 3. 已冻结决策

| ID | 决策 | 状态 |
|---|---|---|
| D-001 | 产品名“傻妞”，稳定 ID `shaniu` | `ACCEPTED` |
| D-002 | AIDK 是主产品；手机和 Gateway 都不能替代板端隐私/安全状态机 | `ACCEPTED` |
| D-003 | 首版原生 Android；微信小程序后置为 Gateway 轻客户端 | `ACCEPTED` |
| D-004 | BLE 只做认领、配网、恢复和小型控制；PCM/JPEG/视频走 Wi-Fi | `ACCEPTED` |
| D-005 | 日常手机控制经 Gateway；板端不开放公网入站服务 | `ACCEPTED` |
| D-006 | 首版半双工；MIC 完全释放后才允许 DAC，视频与语音并发另立门 | `ACCEPTED` |
| D-007 | Camera 首版显式单帧；低帧率预览在单帧稳定后加入 | `ACCEPTED` |
| D-008 | 双屏首版是一对眼睛；程序化 UI 通过后才批量生成最终美术资产 | `ACCEPTED` |
| D-009 | 当前实板基线由 standalone BKVoice 独占 session/audio；优先验证官方 AI Agent 的替代 profile，通过接话、清理及资源预算后再切换。每个运行 profile 始终只有一个 session/audio owner | `ACCEPTED` |
| D-010 | LLM 只调用能力白名单，不接触 GPIO、寄存器、Shell、裸 Flash 或密钥 | `ACCEPTED` |
| D-011 | 产品提供 PTT 和本地唤醒词两种入口，共用 AP 会话 owner。按用户提供的赛事规则第 4 条，比赛固件只允许“你好，openvela”或“Hello，openvela”；先实现中文官方词的多人通用 INT8 DS-CNN。“小冰”保留为后续非比赛版本扩展，不进入首期适配或比赛可选词。KWS 模型与参数由 App 管理、签名并支持撤销，未验收前保留 PTT | `ACCEPTED` |
| D-012 | 不实现产品 UART/串口通信功能；只保留传输介质无关的 provider seam。既有启动、恢复、平台 console 或外设 UART 不扩展为产品能力，也不作为接话退出门 | `ACCEPTED` |
| D-013 | 优先复用 OpenVela 的公共采播 API、AI Agent 对话和语音通道；补齐供应商协议适配，不复制整套框架。原生 Agent 与 Python Gateway 的运行边界按第 17.5 节验证，不把 C 源码视为 Python 的现成模块 | `ACCEPTED` |
| D-014 | 首条真实问答以赛事提供的 MiMo 资源为候选，先对话、再 ASR/TTS 适配；IndexTTS 保留为 Gateway 后续音色方案，不阻塞首轮问答。赛事资源的适用范围与实际接口能力分别核验 | `ACCEPTED` |

## 4. 产品范围

### 4.1 Contest MVP 必须完成（被 2026-09-09 产品门覆盖）

1. Android 通过近场认领完成 Wi-Fi 和 Gateway 配置；
2. 经官方“你好，openvela”唤醒并自动端点录音，直连 MiMo ASR/对话/TTS 的半双工对话；PTT 仅内部调试；
3. 两块 GC9D01 显示校准正确的监听、思考、说话、离线、Camera 和错误状态；
4. 用户明确触发一次 GC2145 单帧视觉问答，并获得语音与双屏反馈；
5. Android 查看在线、电量、版本、权限，修改音量和有限 `persona_mode`；
6. 断网、Gateway 重启、取消和错误输入均能释放 MIC/DAC/Camera 并进入明确降级；
7. 固件、模型/音色和 UI 资产不含私人源数据，交付物具有版本、hash 和验证记录。

### 4.2 Beta 目标

- 本地唤醒词和 1 秒 pre-roll；
- 2–5 fps、单 viewer、用户持续确认的 MJPEG 预览；
- 拿起/摇一摇/翻转等姿态事件和 NFC 白名单场景；
- Android 侧 OTA、资产更新、诊断摘要、解绑和隐私数据删除；
- 用户主机侧可查看、导出和删除的长期记忆。

### 4.3 非 MVP 与明确不承诺

- 30 fps 手机直播、全双工、边播边唤醒；
- iOS、首版微信小程序、公共云强依赖或板端公网端口；
- 舵机、行走等运动控制；CN10 马达振子作为有界提示外设纳入本轮板级验证；
- 在 BK7258 上训练/运行扩散生图、声码器或大型 VLM/LLM；
- 未经硬件证明的高 8 MiB PSRAM、复杂 UIKit、DMA2D/RGB scanout；
- 未授权真人身份、声音、肖像或聊天内容的复制和训练；
- 把软件密钥存储表述为 hardware-backed/TEE。

## 5. 系统架构与控制边界

```text
                         User-owned Gateway
                  ASR | VLM | LLM | TTS | Memory
                    ^                         ^
                    | companion-v1 TLS/WSS    | console-v1 HTTPS/WSS
                    |                         |
+-------------------+------------------+      +--------------------+
| AIDK AI Toy / BK7258                 |                           |
|                                      |                    Android App
| CP: Wi-Fi/BT controller, OTA, health |                           |
| AP: session, audio, UI, vision       |<-- provision-v1 BLE ------+
|                                      |
| capability + permission + arbiter    |
|   |      |       |       |           |
| audio  LCDs   camera  sensors/NFC    |
+--------------------------------------+

后续微信小程序 ---------------- console-v1 ----------------> Gateway
```

执行路径固定为：

```text
user/LLM request
  -> App capability allowlist
  -> permission + expiry + generation check
  -> product state/resource arbitration
  -> public NuttX/OpenVela ABI
  -> Chip lower-half
  -> Board physical binding
```

Board 只描述器件、总线、引脚、电平、设备节点和板级资源关系；Chip 只拥有 SoC/SDK controller、
DMA/cache/PM 和 lower-half；人物、模型、UI、Gateway 和会话策略只属于 App/Gateway。

## 6. 状态定义

Master Plan 只使用以下状态：

| 状态 | 含义 |
|---|---|
| `ACCEPTED` | 需求或设计已经明确确认，不代表实现完成 |
| `IMPLEMENTED` | 源码和相应无硬件测试完成，尚不能称实板通过 |
| `BUILT` | 目标配置构建和 manifest 校验通过 |
| `BOARD_VERIFIED` | 指定物理板、构建身份和用例得到机器可读证据 |
| `EVALUATED` | 模型/资产完成约定客观指标和人工评测 |
| `SELECTED` | 经评测、授权和签发，可进入产品 |
| `IN_PROGRESS` | 有工作产物，但退出条件未满足 |
| `NOT_STARTED` | 尚无实现或可复验证据 |
| `BLOCKED` | 已记录外部阻塞，无法继续取得有效进展 |

构建、设备注册、READY 日志、mock、emulator 和历史其他板证据都不能自动升级为
`BOARD_VERIFIED`。

## 7. 2026-09-02 基线快照

| 子系统 | 当前状态 | 已有证据 | 关键缺口 |
|---|---|---|---|
| BK7258/AIDK 平台 | `IN_PROGRESS` | CP/AP、外设、网络、OTA 和构建交付基础已合并 | 当前产品组合的全外设、PM 和长稳回归 |
| 傻妞身份/离线语音 | `IMPLEMENTED` | persona、voice-pack parser/player、host tests | 实板资产播放、产品 UI 与最终签名资产 |
| Voice session | `IMPLEMENTED` | `companion-v1`、turn/media/capture/PTT owner、cancel/timeout/overflow host tests；aggregate-only `capture-test` 已构建 | 公共按键、真实 sink、TLS/WSS 和 `capture-test` 实机非零 MIC 数据 |
| Gateway AI | `IN_PROGRESS` | 授权音色评测和协议方向已建立 | ASR/LLM/VLM 统一服务、流式接口、模型 profile 加载/撤销 |
| 授权音色模型 | `EVALUATED` | IndexTTS-2.5 zero-shot BASE 在同门槛盲听中胜出；4 个候选内容门均通过 | 授权/披露复核、签名、撤销和 Gateway product manifest；当前 LoRA 不作默认 |
| 双屏产品 UI | `NOT_STARTED` | `/dev/fb0`、`/dev/fb1` 板级路径与产品设计 | 实板左右映射、renderer、动画、30 分钟长稳 |
| Camera 产品能力 | `IN_PROGRESS` | `/dev/video0`、VGA/30 MJPEG 源码和构建 | 历史 `-ENOMEM` 定位、稳定出帧、单帧 owner、上传和回收 |
| Android | `ACCEPTED` | 完整专项计划 | 测试手机基线、工程、fake Gateway、BLE/Gateway 实现 |
| Product BLE provisioning | `IN_PROGRESS` | [安全决策及输入格式](shaniu-provision-security.md)；Android 认领入口、权限/扫描、TLS/ATT、SPV1 事务及提交前回执持久化；板端 TLS/认领/整包文件提交主机联测、SCB1→现有语音配置校验；CP 私有 LittleFS/AP RPMsgFS 配置及 AIDK CP/AP 构建 | bootstrap 供应、广播/按键窗口 worker、Wi-Fi/Gateway 试连与文件提交的运行期接线、Android↔mbedTLS 互通及 20 次实板闭环 |
| KWS | `IN_PROGRESS` | 官方中文词训练审计/INT8 导出入口、上游 microfrontend、TFLM runner；32 KiB/1 秒 pre-roll 和免提端点状态机已有宿主验证 | 真实多人语料/有效模型、AP MIC owner 与 Gateway pre-roll 接线、能量门限标定、目标完整构建、签名、FAR/FRR 和实板验收 |
| Low-fps video | `NOT_STARTED` | Camera MJPEG 与 Android 方案 | 单帧门、Gateway 转发、背压、隐私和并发验证 |
| Product release | `NOT_STARTED` | 配对 OTA/恢复/交付工具基础 | 三端版本矩阵、签名资产、APK、端到端证据包 |

这张表是日期化快照；任何状态改变必须附源码/配置、测试或实板证据，不靠口头更新。

## 8. 六条工作流

### W1：平台与板端基础

责任域：Chip、Board、CP/AP、启动、网络、音视频 lower-half、PSRAM、PM、存储、OTA。

主要交付：

- AIDK 产品 profile 的 resolved config、CP/AP ELF 和 build manifest；
- MIC/DAC、双屏、Camera、SC7A20H、MFRC522、电池、按键、LED、SD NAND、P9 马达振子的设备证据；
- Wi-Fi/BLE 并存、低压休眠恢复、资源峰值和长稳证据；
- 可恢复 direct 诊断包及正式签名 release/OTA 包。

### W2：傻妞设备 App

责任域：persona、状态机、PTT、session、资源仲裁、UI、视觉、权限、离线降级。

主要交付：

- `shaniu` 产品身份和合成披露；
- 半双工 voice turn、cancel、重连和离线 fallback；
- 双屏 eye renderer、情绪映射和隐私/错误覆盖；
- `vision.snapshot`、motion/NFC/battery 语义能力；
- 不包含 Board 私有头、引脚、SDK 私有调用或服务器凭据。

### W3：User Gateway 与 AI

责任域：设备会话、ASR、VLM、LLM、TTS、记忆、Android API、认证、背压和可观测性。

主要交付：

- 单一设备 `companion-v1` TLS/WSS owner；
- ASR → LLM/tool policy → TTS 的首 chunk 流式路径；
- JPEG/VLM 与有限 emotion/tool 结果；
- Android `console-v1` HTTPS/WSS；
- 可验证的 `gateway-model-profile-v1`：固定 backend/model lock、prompt profile、运行时、
  16 kHz/mono/S16 delivery tuple、评测状态、签名和撤销状态；
- 本地可查看/导出/删除的记忆和无正文 telemetry。

### W4：原生 Android

责任域：BLE 认领/配网、Gateway 控制台、状态、心情、视觉、隐私、更新和解绑。

主要交付：

- Kotlin/Compose App、fake BLE/Gateway 和自动测试；
- `provision-v1`、`console-v1` 客户端；
- snapshot 与受控 MJPEG viewer；
- Android Keystore token、权限说明、签名 APK 和 hash。

### W5：模型、音色和 UI 资产

责任域：KWS、ASR/VLM/LLM/TTS 选型、授权音色、离线语音、眼睛素材和签发。

主要交付：

- 数据授权、去重、隔离、评测和撤销记录；
- 冷/热首包、RTF、显存、正确率/相似度和盲测；
- `EVALUATED` 后才能比较，`SELECTED` 后才能签入产品 manifest；
- KWS 资产必须绑定 model hash、词表、frontend/输入 tuple、arena 上限、阈值、FAR/FRR
  噪声集、签名与撤销；产品必选交付是 App-owned KWS inference runner，先用选定模型和 TFLM
  在独立 `kws-app` profile 验证；Media Trigger 只是可选生命周期/事件 adapter，只能在独立
  `media-standard` profile 验证，不能被写成另一种推理 runtime 或第二个产品 owner；
- 程序化眼睛先行，最终 PNG 经人工选择后转为有界 RGB565 sprite/调色板/RLE。

当前音色结论冻结为：IndexTTS-2.5 zero-shot BASE 是 `EVALUATED` 的主候选，私有 prompt
profile 固定 `emotion_alpha=0.25`、`duration_factor=0.94`；三组实验 LoRA checkpoint 均未在
盲听中胜过 BASE，不得因机器相似度或“已训练”而替换默认候选。该结论只完成模型评测门，
没有自动完成授权、披露、签名、撤销或 Gateway runtime 选择门。

### W6：安全、质量与交付证据

责任域：权限、凭据、证书、签名、隐私、测试矩阵、版本和发布物。

主要交付：

- `provision-v1` 安全 ADR、设备认领、撤销和重放保护；
- firmware/model/voice/UI/APK 独立版本、签名、hash 和回滚；
- host/build/Gateway/Android/实板/故障注入/长稳的分层证据；
- 不含私人正文、原始语音、图片、权重、密钥和识别性路径的发布包。

## 9. 依赖关系与并行边界

```text
W1 audio baseline ---------> W2 wake/capture ------> W3 direct MiMo voice
          |                         |                         |
          +-------------------------+-------------------------+--> M2 voice gate

W1 BLE substrate ----------> provision-v1 ADR -----> W4 Android pairing --> M3

W1 camera -----------------> W2 vision.snapshot ---> W3 VLM ---> W4 image UI --> M4

W1 dual LCD ---------------> W2 renderer ----------> W5 selected assets ------> M4

M2 + M3 + M4 -------------> W6 security/long-run/release ---------------------> M8
```

可并行：Android A0 fake Gateway、Gateway mock、UI 资产 brief、host protocol tests。

M7-KWS、M7-Memory 和 M7-Assets 是三个独立 gate：互不阻塞，也不要求全部完成后才能进入 M8。
KWS 或长期记忆未通过时，Contest MVP 分别保持 PTT 或不声明长期记忆；只有实际进入发布包和产品
说明的资产必须先通过对应 `SELECTED`、签名和撤销门。

不可并行共享：同一物理 AIDK、串口/下载工具、同一 build output、同一 GPU 训练/推理实例和同一
Android 测试机。Camera、双屏、音频和 PM 的组合验收必须由一个总控按固定镜像串行执行。

## 10. 里程碑

### M0：计划、边界和可复现基线

状态：`IN_PROGRESS`

交付：Master Plan、专项计划、三协议职责、代码路径边界、当前改动独立交接、AIDK direct build
和 host/layer/manifest 基线。

退出门：所有现有改动归属清楚；没有把用户文件混入提交；当前能力状态与证据一致；下一刀只
有一个 runtime owner。

### M1：离线身份、音频与双眼基础

状态：`IN_PROGRESS`

交付：傻妞身份、合成披露、签名 voice pack、程序化双眼、明确的离线/错误/隐私状态。

当前诊断纵切新增固定 `bkvoice tone-test`：AP 通过公共 `media_player` 播放 1 kHz、500 ms、
低幅度 S16LE 非语音 PCM，复用既有 DAC/PA owner，并对 partial write、断链及清理错误失败关闭。
这只形成可构建和可主机测试的实板入口；听感、PA 时序与重复资源回收仍属于硬件门。

退出门：合法/非法 voice pack 门禁；100 次播放无资源泄漏；两屏物理左右确认；30 分钟低刷新
不影响 supervisor 和音频 deadline。

### M2：PTT 端到端语音

状态：`IN_PROGRESS`

交付：按键事件、公共采播链路、真实网络 provider、Gateway ASR/LLM/TTS、流式下行和 cancel。
当前执行顺序见[第 17.5 节](#openvela-mimo-execution)：先复用和适配 MiMo，固定回复保留为
传输回归夹具；不再以扩建固定回复服务或选定最终 IndexTTS 音色作为真实问答的前置条件。

退出门：50 次连续 turn；MIC 有非零样本/能量；MIC→DAC 顺序正确；断网、Gateway 重启、超时、
取消和旧 generation 均回收；记录首包/P50/P95、heap、stack 和 deadline。

### M3：Android 认领与控制台

状态：`NOT_STARTED`

交付：Android A0/A1/A2、产品 GATT、Wi-Fi/Gateway 配置、状态/权限/音量/persona mode。

退出门：20 次认领/错误密码/重连/解绑；设备本地确认；无明文凭据；Android 不在线时设备仍可
运行；服务端和设备端同时执行权限检查。

### M4：单帧视觉与双屏反应

状态：`IN_PROGRESS`

交付：Camera owner、JPEG chunk、Gateway VLM、语音回答、emotion tag 和 Android snapshot。

单帧纵切：`bkvision-v1` 已实现 CP 命令/AP Camera owner，通过 `/dev/video0` 的 V4L2
MMAP 路径捕获一帧并只返回宽高、fourcc、有效字节数、sequence 和 JPEG SOI/EOI 完整性。
帧内容不经 RPMsg、不记录、不落盘，捕获带有界超时并在所有已进入的阶段逆序回收。该实现
已在 `18.6.230+290` 通过同次启动三次及正常重启后一次有效 JPEG 拍照和关闭，见
[GC2145 频率投票修复记录](aidk-gc2145-jpeg-frequency-fix.md)；100 次场景与泄漏验证仍待完成。
JPEG chunk、Gateway VLM、语音/双屏反馈及 Android snapshot 尚未接入，不能把这一诊断纵切
标记为 M4 完成。

后续连续录像适配升级为 `bkvision-v2`，新增 `bkvision record <1..60秒>`，由 AP 以三个
MMAP 缓冲持续采集并保存无声 MJPEG AVI。CP/AP 干净构建、协议/文件/存储互斥主机测试
及合成视频独立解码通过；v232 已完成实板 1/10/60 秒录像及随后拍照，10/60 秒录像
平均交付约 6.7 fps，文件导出解码和长期资源释放尚待验收。它不改变 snapshot
不落盘的行为，也不代表 Gateway 实时视频已接通，详见[连续录像适配](aidk-continuous-recording.md)。

退出门：100 次成功/取消/超时；有效 JPEG；无 fd/heap/Camera owner 泄漏；真实 Camera 指示覆盖
占用期；未知情绪回退 `neutral`。

### M5：姿态、NFC、电池与旅行场景

状态：`IN_PROGRESS`

交付：拿起/摇一摇/翻转事件、NFC 白名单场景、电量降级、手机可选位置授权和离线待同步。

当前纵切：只读 `bkhealth-v1` 已实现电池状态、毫伏电压和 raw-first 片上温度查询；无电芯
放电曲线时不输出百分比，无单芯片 25 摄氏度参考值时不输出摄氏温度。该纵切仍需实板读取
证据。`bknfc-v1` 已实现只返回 `present=yes/no` 的 NFC 在场查询，协议不包含 UID、卡号或
卡片内容，且不把在场结果当作身份认证；仍需实板完成无卡/有卡重复扫描。姿态事件、NFC
白名单策略和低电动作策略尚未完成。

退出门：传感器只发布语义事件；NFC 不作为唯一强认证；低电禁止高耗能动作；位置撤销立即生效；
任何传感器故障不阻塞语音、OTA 和 supervisor。

### M6：Android 低帧率视频

状态：`NOT_STARTED`

交付：640×480 MJPEG 2–5 fps、单 viewer、Gateway relay、Android viewer、背压和自动停止。

退出门：30 分钟无无界队列或 heap 增长；锁屏、后台、松开、断线、低电和超时均停止；隐私指示
同步；通过并发门之前与 voice turn 互斥。

### M7：三个独立 Beta gate

状态：`IN_PROGRESS`（资产评测和 KWS 基础实现已有进展；长期记忆仍为 `NOT_STARTED`）

以下历史 gate 描述已被 2026-09-09 产品要求覆盖；官方唤醒、离线恢复、认领安全、视觉和 OTA
均是当前交付范围，不能再标为可选或后续。失败时必须保留明确未完成状态。

#### M7-KWS：本地唤醒

状态：`IN_PROGRESS`；实现入口和输入契约见[语音应用说明](bkvoice-authorized-voice-app.md#111-本地唤醒)。

交付：App-owned KWS inference runner、所选唤醒词模型/词图、1 秒 pre-roll、签名和撤销 manifest。
比赛固件仅接受官方“你好，openvela”或“Hello，openvela”；先实现前者，英语版本另行训练验收。
产品名仍为“傻妞”。App 只选择符合赛事规则且已安装、已验收的唤醒词资产。
“小冰”保留在后续非比赛版本的扩展队列，复用同一前端、runner 和触发接口，但单独训练、
签发并验收模型；不能仅改显示字符串。比赛 profile 的词表白名单同时约束 App 选择和资产
安装，不允许通过设置或下载模型启用“小冰”。官方词是产品入口，PTT 仅内部调试。
唤醒后进入同一 AP 语音会话，由 VAD 静音判定或最大收音时长结束上行；
PTT 由松键结束。空唤醒、取消与超时都必须释放资源。半双工播放期间暂停 KWS，
播放完成后恢复监听；边播边唤醒仍需另行通过 AEC 和并发验收。

先过 config/build 门：目标产品 profile 明确启用选定 TFLM runtime 和 App runner，保存对应
resolved config；实际输入固定并验证为 16 kHz/mono/S16，记录 MIC/AEC physical path、模型/
词表/frontend/阈值/arena provenance。Media Trigger 只有生命周期 API；只有选择独立
`media-standard` profile 时才给同一 runner 增加 adapter，不能把“树里存在”写成已有 BK7258
模型 plugin。vendor Wanson 的“嗨阿米诺/拜拜阿米诺”静态词图也不能替代所选唤醒词模型。

实板退出门：在安静、音乐、车内、户外风噪、远场、不同人声和扬声器回放集上记录 FAR/FRR、
P50/P95、CPU、arena/heap 和 1 小时连续运行；触发后 1 秒 pre-roll 连续，PM 恢复后仍满足门槛。
任一门未过时产品标记官方唤醒未完成，不得用 PTT 作为产品替代；不能用 vendor 串口出现一次
`recognized` 代替验收。

#### M7-Memory：长期记忆

状态：`NOT_STARTED`

交付：Gateway 侧可查看、导出、删除和撤销的长期记忆，以及设备/Android 的有限控制入口。

退出门：来源、用途、保留期和同步状态可追踪；删除与撤销后服务端、cache 和设备待同步副本均
不可继续使用。未通过时只关闭长期记忆能力，不阻塞 KWS、资产选择或 Contest MVP/M8。

#### M7-Assets：最终模型、音色和 UI 资产

状态：`IN_PROGRESS`

交付：最终 TTS/音色和 UI 资产 `SELECTED` 清单及签名、版本、有效期和撤销 manifest。

退出门：只有通过统一评测和人工选择的资产才能进入发布包；当前 IndexTTS-2.5 zero-shot BASE
仍只是 `EVALUATED`。未完成的可选候选从发布能力矩阵移除，Contest MVP 继续使用已验收的签名
voice pack 和程序化双眼，不能用“已有 checkpoint/图片”代替 `SELECTED`。

### M8：安全、长稳与比赛交付

状态：`NOT_STARTED`

交付：正式固件 release/OTA、Gateway 部署包、签名 APK、签名模型/音色/UI manifest、演示脚本和
证据索引。

M7-KWS 或 M7-Memory 未完成不阻塞 Contest MVP/M8：发布版保持 PTT、关闭未验收的长期记忆，并在
能力矩阵中明确标记未包含。M7-Assets 只约束实际随发布包交付或在产品说明中声明的资产。

退出门：24 小时目标组合长稳；掉电/弱网/错误证书/重放/升级失败注入；可回滚；不破坏 `sys_rf`；
发布包不含私人数据；最终说明只列 `BOARD_VERIFIED` 与 `SELECTED` 能力。

### M9：可选扩展

状态：`NOT_STARTED`

微信小程序、唱歌、全双工、Opus、高 8 MiB PSRAM、复杂 UIKit、局域网直连和未来电机均在 M8
之后单独立项，不得反向阻塞 Contest MVP。

## 11. 三个核心用户旅程

### 11.1 首次开机

```text
设备进入未认领态
-> Android 发现
-> 板端按键/双屏确认
-> 安全传递 Wi-Fi/Gateway 配置
-> 关联/DHCP/Gateway 注册
-> Android 与双屏同时显示成功
-> 手机离线后设备仍进入 IDLE/OFFLINE
```

### 11.2 日常语音

```text
本地 KWS 检出官方唤醒词“你好，openvela”（PTT 仅内部调试）
-> 同一个 AP 会话 owner 接受触发
-> 双眼监听 + MIC 指示
-> 语音入口 VAD 检出静音/达到收音上限
-> interrupt/join/release
-> 上行结束
-> Gateway ASR/LLM/TTS
-> 首 PCM chunk 下行
-> 双眼说话 + DAC
-> 完整 release 回到 IDLE，按配置恢复 KWS 监听
```

### 11.3 “傻妞，帮我看一下”

```text
明确请求
-> 双屏 Camera 确认
-> reserve + 单帧 JPEG
-> 立即 stream-off/release
-> Gateway VLM + LLM
-> answer + bounded emotion
-> 双屏反应 + TTS
-> 原图默认删除
```

## 12. 协议与数据所有权

| 契约 | 两端 | 数据 | 唯一 owner |
|---|---|---|---|
| `companion-v1` | AIDK ↔ Gateway | PCM、JPEG、turn、窗口、取消、错误 | AIDK App + Gateway session |
| `provision-v1` | Android ↔ AIDK BLE | 认领、Wi-Fi/Gateway 配置、结果 | AIDK provisioning App |
| `console-v1` | Android/小程序 ↔ Gateway | 状态、设置、权限、视觉和管理 | Gateway |
| `bkvoice-pack-v1` | 签名资产 ↔ AIDK | 离线 PCM 和授权/披露 manifest | AIDK App |
| `gateway-model-profile-v1` | Release pipeline ↔ Gateway | backend/model lock/profile/runtime/delivery/评测/签名/撤销 | Gateway release pipeline |
| product asset manifest | Gateway/Android/AIDK | 模型、音色、UI 的版本/hash/撤销 | Release pipeline |

私人原始聊天、原始语音、图片、embedding、训练 checkpoint、模型权重和密钥只保留在获授权的
用户主机或密钥系统中，不进入 Git、固件、APK、默认 telemetry 或公开证据。

## 13. 测试与证据矩阵

| 层级 | 证明什么 | 不能证明什么 |
|---|---|---|
| Host unit/fake | parser、状态机、协议、超时、取消、错误回滚 | 真实驱动、时序、RF、音质和画面 |
| Layer/config gate | Chip/Board/App 依赖与配置闭包 | 运行时资源和硬件功能 |
| Target build/manifest | 源码可链接、角色和交付输入一致 | 板上可用、数据有效和长期稳定 |
| Gateway/Android mock | API、UI、重连和错误模型 | BLE/Wi-Fi/Camera 实际链路 |
| 实板单功能 | 指定镜像下单外设功能 | 组合并发、PM 和长稳 |
| 实板组合/故障注入 | 产品状态机、资源、隐私、恢复 | 量产一致性和长期现场数据 |
| Release verification | 版本、hash、签名、布局和回滚输入 | 未执行的硬件用例 |

每条验收记录至少包含：物理板、profile、build identity、固件/资产版本、操作步骤、机器可读
结果、失败边界和证据路径。不同板、旧镜像或历史 profile 的 PASS 不得替代当前 AIDK 产品证据。

## 14. 关键指标

### 可靠性

- Voice：50 连续 turn；离线 voice pack 100 次播放；
- Camera：100 次单帧成功/取消/超时；
- BLE provisioning：20 次认领/重连/解绑；
- UI/video：双屏 30 分钟、视频 30 分钟；
- Release candidate：24 小时目标组合运行。

### 性能

- 记录 voice ASR/TTS 冷/热首包和端到端 P50/P95；
- 记录 20 ms 音频 deadline、MIC→DAC 切换、Camera 首帧和 UI 刷新耗时；
- KWS 单独记录 FAR/FRR、触发 P50/P95、CPU、tensor arena/heap 峰值、1 秒 pre-roll 和
  PM 恢复后首个有效 deadline；
- 记录 internal SRAM、PSRAM、heap min-free、线程 stack high-water、fd/mqueue 和网络窗口；
- 指标在首次硬件基线后冻结阈值，不能在没有数据时伪造数字目标。

### 隐私与安全

- MIC/Camera 指示覆盖真实 reserve 到 release；
- 凭据、token、图片和私密正文不进入日志；
- L2/L3 请求有权限、过期、generation、重放和撤销检查；
- 固件、模型、音色、UI 和 APK 版本可独立识别，必要时独立回滚或撤销。

## 15. 发布物

Contest release 至少包含：

1. 经过验证的 CP/AP 配对固件、build/release manifest、hash 和烧录/恢复说明；
2. Gateway 可复现部署定义、依赖锁定、API schema 和不含私密权重的模型 manifest；
3. 签名 Android APK、版本、hash、权限用途和兼容设备清单；
4. 签名 voice/KWS/UI 资产 manifest，不包含训练源数据；
5. Host/build/Gateway/Android/实板/长稳的证据索引；
6. 一条从首次认领到语音、视觉、离线、恢复的演示脚本；
7. 已知限制、未通过能力和回滚步骤。

## 16. 风险登记

| 风险 | 当前判断 | 应对与退出证据 |
|---|---|---|
| MIC/PTT 产品闭环未通过 | 高影响，v328 已有按键与网络接线，但 Gateway 连接超时，缺少完整 turn 证据 | 复用现有接线，验证真实模型回复和 MIC/DAC 换向，再取得 50-turn 证据 |
| 摄像头与新增外设组合回归 | v321 单项基线已保留，后续共享资源改动可能引入回归 | 复用既有帧率、冷断电与读回基线；只在相关时钟、DMA、电源或并发改变时补对应回归 |
| BLE 配网泄露凭据 | 高影响，产品协议未设计 | 独立安全 ADR、本地 possession、会话保护、无明文日志/抓包证据 |
| 双屏刷新挤占音频 | 中高影响 | 程序化低刷新、脏矩形、30 分钟 UI+audio deadline |
| Wi-Fi/Gateway 弱网 | 高概率 | 有界窗口、取消、重连、离线 fallback、P50/P95 和故障注入 |
| 多 owner 状态冲突 | 高影响 | standalone BKVoice 与 AI Agent 二选一，三协议唯一 owner |
| 私人数据进入 Git/发布包 | 不可接受 | 路径隔离、source audit、release 内容扫描和人工复核 |
| 功能过多拖垮 MVP | 高概率 | M9 可选项不阻塞 M0–M8；每阶段只有明确退出门 |
| 构建被误当实板完成 | 已发生过的认知风险 | 强制状态词与日期化 BOARD_VERIFIED 证据 |
| 缺少已验收的官方唤醒词模型 | 现有 Wanson 固定词图不能满足赛事词要求，TFLM runner 通过不等于模型有效 | 按 M7-KWS 训练、签发和验证官方词的多人通用模型；“小冰”留待非比赛扩展，不等待供应商定制词图 |
| Media Trigger 被误当成现成 KWS | 高影响，当前只有 API/生命周期，未见 BK7258 模型 plugin | 先证明 App-owned TFLM runner；Media Trigger adapter 只在独立 `media-standard` profile 验证，不引入第二 owner；保存 resolved config 与实板 FAR/FRR |
| KWS defconfig/output 漂移 | 高影响，历史 resolved config 关闭 Media/Trigger/CMSIS-NN，不能代表未来产品 profile | 以指定 build identity 保存 resolved config、模型/runner manifest 和板测证据 |

## 17. 立即执行队列

当前优先级以第 17.5 节为准：复用 OpenVela，完成 MiMo 真实 PTT 问答并进入 Android 联调。
现有改动继续按外设和工具拆分验证、提交；马达只属于实际连接它的 AIDK。
第 17.1–17.4 节保留此前执行和验收记录，不把旧日志或源码检查升级为新的实板通过。

### 17.1 当前源码与验收基线

- 2026-09-07 已重新获取远端主线 `openvela/dev-ai-contest-2026`，当前为 `3e694fb`；
  主工作区已对齐该提交并进入 `fix/bk7258-aidk-board-dependencies`。旧 `782e019` 分支
  保留，未合入的语音/外设变动保留为工作区差异；摄像头、SDIO 使用已合并版本。
- 摄像头/V4L2、SDIO 写入修复已经合入。保留 v321（`18.6.321+381`）恢复包，以及
  `out/bkvision-perf-v321/` 的冷断电、读回与解码证据，作为后续组合回归的参照。
  后续只有相关时钟、DMA、共享电源、文件系统或并发发生变化，才补对应回归。
- 用户本轮确认 CN10 接有马达振子，并提供 P9 驱动电路。当前
  `bk7258_board_config.h` 已定义 `BK7258_BOARD_PIN_MOTOR=9`，`HAS_MOTOR` 和
  `CN10_MOTOR_CONNECTED` 已改为 1；Kconfig、HIL README 已同步，驱动实板验收尚未完成。
  现有 AIDK 用户 LED 绑定 P40、按键绑定 P8；源码检查未发现该绑定直接用 P9 测 LED。
- 正常产品 HIL 已移除依赖旧诊断日志的 `dmesg` 容量用例，保留 6 项真实串口检查。
  容量单独通过 v323 AP 块设备与 Windows MSC 核对：126877696 bytes / 247808 个
  512-byte sector。该几何结果不能替代整盘读写或冷断电保持验证。
- 依赖来源已重新确认：`nuttx/openamp/open-amp` 和 `libmetal` 是官方 manifest 中的
  独立 Git 项目，当前均无源码修改，并非私改缓存。通用 NuttX fallback 的 upstream zip
  与当前 OpenVela RPMsg API 不匹配。已撤回两头文件候选补丁，采用
  `nuttx/dependencies.lock.json` 固定官方提交并用 `git archive` 重建。当前没有证据要求
  修改 OpenAMP/libmetal 源码。AIDK、T5-Board、T5AI-Core 的 CP/AP 冷构建已通过；
  AIDK v322/v323 已确认启动，AP、CPU2、RPMsg 正常。其他两板仅有构建证据。
- `gateway/shaniu` 当前是 loopback TLS/WSS 的确定性音调回复服务，尚无 ASR/LLM/TTS。
  板端 `bkvoice status` 仍标记 PTT event、sink、transport 未安装；已有逻辑测试不等于
  物理按键到真实 Gateway 的接线完成。Android 已有 HTTPS/WSS 客户端工程，实机联调另验。

### 17.2 马达振子的板级约束

用户提供的电路为：P9 经 R51（1 kΩ）驱动 Q2（MMBT3904）基极，R53（10 kΩ）下拉；
Q2 集电极连接 CN10 低端，马达高端经 R46（15 Ω）接 LDO_3V3，D8 为续流二极管。
由该电路可知 P9 高有效、低电平停止；这不构成转速、电流或温升已经验证的证据。

- P9 归属马达，禁止纳入通用 LED、GPIO 扫描或回环测试；接入状态与驱动验收状态分别记录。
- 先完成上电、初始化失败、取消、超时、休眠及复位后的停止路径，再进行短脉冲验证。
  测试与产品请求均需有界时长；脉宽、间隔和最大连续工作时间由首次实测冻结。
- 复用 NuttX GPIO/定时器能力，审查现有 lower-half 是否足够。AP 作为马达动作和计时的
  唯一 owner；Board 描述引脚、电平和供电，Chip 负责 GPIO/PM 机制，产品层只提交有界动作。
- LDO_3V3 与 LCD、NFC、SD NAND 共用。停止马达只释放自己的动作与电源投票，不能直接
  拉低 P52 切断其他外设。实板验证短脉冲、重复启停、电压波动，以及存储/显示/NFC 并发。
- 首版采音期间暂停振动，避免机械噪声进入 MIC；若产品以后要求同时工作，新增串扰验收。
  马达动作属于明确选择的主动 HIL 用例，普通状态检查不能启动马达。

### 17.3 验证与提交批次

固件按下表顺序执行；工具、Gateway 和 Android 的主机工作可独立推进，实板始终只有一个
操作者。每行可以拆成多个小提交，共享 Kconfig/Make/CMake 文件按本批必要 hunk 暂存。

| 批次 | 范围与提交边界 | 提交前退出条件 |
|---|---|---|
| B0 主线与板级契约 | 保留真实差异并对齐 `3e694fb`；更新 CN10/P9 装配事实、马达测试排除规则、SD NAND 容量契约 | 已合入内容去重；板级事实、配置和 HIL 断言一致；不以启用能力标志代替驱动验收 |
| B1 可复现依赖 | 锁定官方 manifest 的 OpenAMP/libmetal Git 提交，从 `git archive` 重建；维护依赖锁和隔离构建入口 | 无工作目录拷贝、无旧 role 输出时生成 AIDK CP/AP；两端 API/资源表 ABI 与 RPMsg 建链实测通过；仅在确证官方实现缺口后维护源码补丁 |
| B2 看门狗与启动 | 必要的 CP WDT/启动顺序/恢复修复；不夹带临时 IRQ 与重启追踪 | 平台和复位主机用例通过；实板正常喂狗、受控超时、复位原因、CP/AP 恢复分别有证据 |
| B3a MIC 与 AEC | MIC lower-half、recorder 生命周期、MIC1/MIC2 参考路由；AEC 修复单独提交，采集探针留在测试配置 | 静音/近端说话/远端播放可区分；MIC1 为主麦、MIC2 为 AEC 参考；20 ms 帧截止期、启停回收通过。AEC 另有开关对照、参考延迟、削顶与残余回声测量，不能用非零样本代替效果验收 |
| B3b PTT 与语音会话 | 物理 P8 按键事件、有界控制队列、真实 WSS transport/sink、取消及资源释放 | 真实按下/松开、快速重按、超时、断网、重连、背压；连续 50 turn 和 30 分钟运行；MIC 释放后再启 DAC，失败后可再次发起。Gateway 可先用受控固定回复，真实问答另验 |
| B4 马达振子 | P9 绑定、单 owner、有界动作及停止路径；与显示/LED 提交分开 | 第 17.2 节的短脉冲、取消、复位、供电和并发验证完成；采音期间不误振动 |
| B5 显示 | 双屏资源、左右映射、显示协议/资产与 renderer | 两屏独立图样和左右映射人工确认；启动缺资产/坏资产可恢复；语音与显示 30 分钟并发；若影响共享电源或搬运路径，补摄像头/存储回归 |
| B6 电池与温度 | 电池、温度各自小提交；保留原始量、单位与校准状态 | 电压与仪表对照，充电/满电/断开状态真实切换；温度有参考测量。没有标定依据时不宣称温度精度或电量百分比准确 |
| B7 加速度计 | 标准 sensor 路径、板级轴向与产品 motion 服务 | 六面静置、重力幅值/符号、时间戳、实际运动、休眠恢复；WHO_AM_I 或注册成功不足以验收 |
| B8 NFC | UART 外设传输、读卡生命周期与 presence 服务 | 无卡/已知卡、移入移出、重复读取、超时与断电恢复；普通日志不输出 UID |
| T 工具 | HIL 下载工具和 debug skill 显式调用限制分别提交 | 主机契约测试；明确指定板型/设备身份/串口，错误目标被拒绝；debug skill 仅明确点名触发。真实下载复用某个固件批次的独占板窗口，避免重复刷机 |
| G Gateway | 保留固定回复协议回归，按第 17.5 节优先接入 MiMo 真实 ASR/LLM/TTS；鉴权和部署随接入验证 | `make -C gateway/shaniu test`；双向窗口、取消、断连、证书失败；设备接入须有明确部署/凭据策略，不能直接放开 loopback 限制。真实语音链另验首包、流式取消、故障降级 |
| A Android | 保留现有真实 HTTPS/WSS 客户端；工程、控制面、实机问题独立提交 | 单元测试和 `assembleDebug`；小米 10 安装、Keystore、真实 TLS/WSS、断线重连和设备状态回报。BLE 配网按专项协议门后续接入 |

B1 的共享依赖以及 Chip/common 改动，在提交前补齐 AIDK、T5-Board、T5AI-Core 对应 CP/AP
干净构建回归；各板的硬件状态分开记录。只从锁定官方 Git archive/已维护补丁重建，不把展开目录、
zip、缓存版本漂移或整个 SDK 打包进源码提交，也不因看到差异就预先修改 SDK/NuttX。

### 17.4 执行入口和交付证据

已运行以下主机入口并通过；这些结果不能代替实板及物理验收：

```sh
make -C tests/host/bk7258 run-platform run-reset-marker
make -C tests/host/bk7258 run-voice-capture run-voice-ptt run-voice-media-recorder \
  run-voice-turn run-voice-turn-audio run-voice-gateway run-voice-session run-voice-wss
make -C tests/host/bk7258 run-display-pack run-display-rpc
make -C tests/host/bk7258 run-health-core run-motion-core run-nfc-core
make -C gateway/shaniu test
```

Android 在独立临时源码副本执行
`./gradlew :app:testDebugUnitTest :app:assembleDebug --offline --no-daemon` 已通过：
42 tests、0 failures/errors/skipped；APK 已生成，尚未安装到测试手机。
Gateway 12 项协议/TLS 测试通过；HIL 下载工具 14 项主机契约通过。板测继续使用当前维护的构建/下载入口和
`tests/pytest/test_bk7258/`，B0 的过时容量契约已修正。COM8 已通过 Windows PnP 确认为 CH340 UART0，
存储验证使用 `18.6.323+383`，当时由 HIL 自动刷入并确认；`apctl health` 为 HEALTHY，
faults/recoveries/consecutive=0/0/0。v323 修复 AIDK 两端漏选 USBMODE_RPMSG 导致命令未编入的问题。
新录像 10 秒记录 298 帧（29.8 fps），三次 AP 读回及 MSC 副本的 FNV 同为 `645f35a5`，
但主机仅解码 297 帧并报 MJPEG overread，尚未通过录像完整性验收。8.2/9.2 MB 文件的
Windows 复制分别耗时约 156/174 秒，读性能异常不能归因为 RPC 回复丢失。

v323 验证时读取的原 v321 录像 FNV 为 `37aa5528`，与保留原件 `f081c022` 不同。旧文件内两个
128 KiB 区间逐字节等于新录像的两段；文件簇范围无重叠，原始卷读取也确认相同内容，且
整机软件复位后保持。只读 FAT 检查另发现多份旧测试文件的交叉链接和长度错误，未运行
修复或格式化。没有 v323 写入前的整文件/原始扇区快照，尚不能判定异常发生于哪次操作，
也不能将它归因于新增代码、MSC、SDIO 或硬件。已提交的 SDIO、MMCSD 和录像源码在
`3e694fb` 到 `25da215` 之间没有后续差异；这不等于两个集成固件的全部输入一致。

一份旧测试录像 `video-2801343a-00000003.avi` 因文件损坏未能备份，已按此前清理授权删除；
没有删除模型。原 v321 基线、当前两份主机录像、四段原始扇区及软件复位后的副本均保留在
`out/shaniu-plan-20260907/` 和 `out/bkvision-perf-v321/`，不进入源码提交。

每批按“审查真实差异 → 剔除临时诊断 → 主机检查 → 构建 → 对应板测 → 小范围暂存并提交”
推进。提交说明必须给出问题、行为变化、准确测试结果、构建身份和剩余物理验收限制；
验收证据留精简摘要、必要日志与 hash。沿用既有 fork/中文 PR 交付边界，由用户创建远端 PR。
临时探针、IRQ/重启追踪、原始日志、私人语料、模型、密钥和构建输出不进入提交。

B0/B1、T、Gateway、Android 和 B2 启动/整机复位，以及 B4 的 FF 维护补丁、
通用驱动、板级绑定、应用命令已形成独立提交。发布分支为
`fork/fix/bk7258-aidk-board-dependencies`；14 个提交已通过 PR #101 合入上游，
合并后基线为 `25da215`。上游 rebase 后的提交 ID 改变，提交树与原 `9fa922b` 完全一致。
Gateway mTLS 也已合入，上游当前基线为 `9ea6de9`。后续在原工作区的
`fix/bk7258-physical-ptt` 分支推进，未提交改动完整保留。
B2 的 `reset reboot` 实测恢复 AP/CPU2/RPMsg 健康；AP-only restart 被 Wi-Fi
生命周期保护以 `-EBUSY` 拒绝，不能记为 AP 单独重启通过。当前 v326 已完成标准
wdog 受控停喂 -> rtc_watchdog(0) -> AP/CPU2/RPMsg 恢复；配置及共享检查已提交。

B4 的 v324（18.6.324+384）已通过自动下载、启动、能力查询、停止、超长请求拒绝、
P9 激活/自动到期读回和冷却期拒绝。P9 脉冲期间读到 `0x00000003`，板端延时后、
显式 stop 前读到 `0x00030001`（输出锁存位为低）。这不是示波器脉宽测量。
FF 驱动 15 场景、AP 服务 10 场景、FF upper-half ASAN 测试通过；
AIDK、T5-Board、T5AI-Core CP/AP 干净构建通过。马达仅适用于已连接 CN10/P9
的 AIDK；T5-Board 和 T5AI-Core 未连接马达，其 CP/AP 最终配置未启用马达或
bkhaptic，ELF 中也无对应驱动和命令。两者在本批仅做共享代码编译回归。
v324 来自集成工作树，
包含尚未提交的其他外设候选，不能作为纯发布 HEAD 的全功能整包验收。
本次临时签名私钥已删除，只保留公开签名产物。

B4 尚缺人工振感、供电波动、采音串扰及并发验收，故第 17.3 节整行仍未完成。
MIC raw PCM 5 秒标准采集和 watchdog 实板复位恢复已通过 v326 验证；
54c605d、6cf1d89、9fa922b 分别提交音频生命周期、任务上下文喂狗和 RPMsg drivercheck。
这三笔已随 PR #101 合入上游。AEC/PTT 和其余外设未满足各自退出条件，继续留在工作区。
PTT 的 P8 `/dev/gpio1` 归 CP 所有。v328 已接入独立按键 RPMsg 通道、AP 会话 owner
和 RX 队列，并在实板完成 mTLS/端点/可信时间的 RAM 装载及连接超时后的清除。
当前 Gateway 连接仍超时，尚无物理按键到 MIC/Gateway/DAC 的通过证据；手机安装和
人工听感验收也未取得。不能以主机模拟或凭据装载成功冒充端到端验收。
存储修复已有通过结论：v321 的 10 秒/301 帧录像，经重复读取、软件复位和一次完全断电，
FNV 始终为 `f081c022`，主机副本逐字节一致且 301 帧全部严格解码。这个验收不撤销。
单独保留 v323 的后续内容变化/解码异常待复现项，不将其写成“此前存储从未完成”，也不
将它当成已证实的代码回归。长时、多文件及组合压力尚不在 v321 的通过范围内。

后续 v327（18.6.327+387）已自动下载并确认 AP/CPU2/RPMsg 健康。motion/NFC 客户端
与服务端补齐发送拥塞重试、连接代次和 namespace unbind 清理；真实源码 host 各 19 场景
通过 UBSan。MFRC522 官方 read 忽略选卡错误的问题以 contactless/0001 维护补丁修复，
只在隔离 NuttX 应用；无卡和选卡失败故障注入通过，官方 checkout 未改。
v327 连续三次加速度计采样时间戳递增，单一静置姿态重力模长约 9.87–9.90 m/s²；
两次 NFC 查询均报告 present=no。电池查询为 charging、4059/4060 mV；温度 raw=541/542，
未标定故不报告摄氏度。这些是查询路径证据，六面/动态、已知卡移入移出和仪表对照仍未完成。
两块 framebuffer 的官方绘制和新固件麦克风回归亦通过，最终 AP 零故障；临时签名私钥已删除。
详见 [v327 外围 RPC 验证](../../verification/bk7258/2026-09-07-aidk-peripheral-rpc.md)。
新外围服务改动继续留在工作区，按对应退出门再提交。

此前的固定回复接线阶段保留为验证基线；下一执行顺序由第 17.5 节更新为 OpenVela 复用、
MiMo 真实问答和 Android console-v1 联调。其他外设和 AEC 的独立验收继续保留，
不作为 Android/Gateway 接口联调的前置条件。首版保持半双工。
BLE 承担 App 认领和配网；PCM/JPEG/视频走 Wi-Fi。IndexTTS 位于 Gateway 算力端，
AP 负责流式音频和设备执行回报。v328 的隐藏串口装载用于当前实板联调，
不代表 App 配网或持久设备注册已经实现。

KWS、长期记忆、最终音色资产、BLE 配网和手机预览继续遵循各自既有里程碑，
未通过的能力保持关闭，不扩大本轮提交范围。

2026-09-07 的组件复用接入将 KWS 自写前端替换为现有 TFLM microfrontend，
同源批处理/流式特征、合成数据 INT8 导出和真实 TFLM host 推理已通过。
新增 KVDB 非秘密偏好适配和 `bkvoice prefs`，复用现有 CP→AP endpoint；
API、CLI host 测试及现有目标参数下的对象编译通过，尚未开启板级后端。
`desired_volume/persona` 仅是设置值，尚不改变音频增益或 Gateway 人物状态。

复用审计发现两个必要前置：KVDB FILE 的短写/EINTR/零写处理有缺陷，
UnQLite 使用 `OMIT_JOURNALING` 且缺失项错误码尚未归一，需选定后端后修复并验证持久化；
当前 NuttX SMP 不支持所需的认证配对，AI Agent 配网示例又依赖另一套 Bluetooth
Framework API，不能直接代替 `provision-v1` 的认领、物理确认和凭据保护流程。
完整 Media 的 stop/join/release 和资源预算也需独立验证，本轮维持现有 AP owner。
这些源码进展不构成 BLE 配网、掉电持久化或正式唤醒的实板验收。

<a id="openvela-mimo-execution"></a>

### 17.5 OpenVela/MiMo 复用与当前执行顺序

2026-09-07 决策更新：先完成“按键说话 → 识别 → 对话 → 播放回答 → 回到空闲”的真实链路。
以下是待执行计划；现有源码支持、主机测试通过和实板通过分别记证据，不互相替代。

**复用边界和唯一 owner**

- 官方 `packages/ai_agent` 已有 voice channel、公共 recorder/player 接入、message bus、
  LLM/tool loop 和回复播报。优先在 AP 的最小替代 profile 验证，复用现有
  `BK7258_APP_AGENT` 生命周期绑定和公共采播 bridge；不新写一套录音器或播放器。
- 产品基线仍为 `BKVoice → companion-v1 → gateway/shaniu`。替代 profile 中关闭 BKVoice
  session/audio owner，再启用 Agent；完成资源、取消、离线和故障验证后才能迁移产品。
  若 Agent 的资源或生命周期尚不满足实板要求，继续当前 BKVoice 路线完成首条问答。
- 官方 Agent 是 C/NuttX 应用，当前 Gateway 是 Python 服务。Gateway 接入 MiMo 需要自身的
  provider adapter；不能把官方 C 模块直接称为已复用的 Gateway 后端。Agent 路线通过
  Gateway 的模型代理接入云端；现有 BKVoice 路线由 Gateway 编排 ASR/LLM/TTS。
  同一会话只选择一条路径，不能同时在 AP 和 Gateway 运行两套对话编排。
- 两条路径均保持 AP 掌握物理 PTT、MIC/DAC 仲裁、设备权限和 UI；Gateway 掌握供应商密钥、
  模型 profile、长期记忆和云端调用；Android 负责认领、配网和设置。MiMo 密钥不进入 App、
  固件、配置示例或日志。Gateway 内部 provider 接口不改变 Android `console-v1`。
- 替换前列出入口、线程/缓冲上限和删除集合：仅移除被新 owner 完整接管且已无消费者的
  session/voice provider；仍用于控制、Camera、OTA 或已选基线的协议不能随之删除。
  不保留两份可变会话状态，也不为复用强制启用完整 Media、MQTT 或 Bluetooth Framework。
  公共包确需补能力时，以团队仓可复现补丁/overlay 维护，保留上游来源，不直接改官方 checkout。

**供应商适配范围**

| 部分 | 首选接入与已有复用点 | 必须补齐和验证 |
|---|---|---|
| 对话 | MiMo OpenAI-compatible `/v1/chat/completions`；优先验证 `mimo-v2.5`，`mimo-v2.5-pro` 作为质量/延迟对照。官方 `cmd_llm.c`、`llm_proxy.c` 已有 MiMo preset、自定义 URL 和通用 chat/tool 处理 | 显式配置当前 endpoint/model，不沿用旧 `mimo-v2-flash` 默认；验证鉴权、响应、上下文、工具白名单和取消。其他厂商 preset 只表示代码入口，不能据此宣称实测支持；当前 Claude preset 不等于原生 Anthropic 协议适配完成 |
| ASR | `mimo-v2.5-asr` 的 chat 请求中上传一个 `input_audio`，先使用 WAV | 首轮在松开 PTT 后提交有界完整录音；BKVoice 路线由 Gateway 将上行 PCM 封装为 WAV。固定时长/字节上限、超时和空录音行为；`stream:true` 是识别结果的 SSE 输出，不等于边录边上传 |
| TTS | 先验证 `mimo-v2.5-tts` 内置音色；待播文本按接口放入 assistant message，流式 PCM 从 SSE 的 `choices[].delta.audio.data` 解码 | 处理 SSE 任意分片、Base64、结束和取消，核实实际采样率/声道/格式；Gateway 转成板端的 16 kHz/mono/S16LE，再按 20 ms 分帧。当前协议固定 640 字节，只给 TTS 尾帧补静音，不裁尾音、不填充输入录音 |
| Agent 语音 provider | 复用 `voice_channel.c`、`audio_capture.c`、`audio_playback.c`；现有 `voice_asr.c`/`voice_tts.c` 的流式入口直接委托火山实现 | 增加可替换的 ASR/TTS provider 能力与生命周期接口，区分整段 ASR 和实时上行；保留 start/finish/abort 及 TTS cancel，不以“改 URL”替代协议适配 |
| 后续音色 | 保留 Gateway IndexTTS 与 MiMo voiceclone/voicedesign 的候选位置 | 先完成内置音色问答，再比较授权、音质、首包和成本；既有 IndexTTS `EVALUATED` 结论保留，未通过资产签发与实板验收前不升级为 `SELECTED` |

协议依据：[MiMo ASR](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition)、
[MiMo TTS](https://mimo.mi.com/docs/en-US/api/audio/tts)及
[TTS 使用说明](https://mimo.mi.com/docs/usage-guide/speech-synthesis-v2.5)。
采样率、输入上限、实际可用模型和赛事 endpoint 的音频能力仍需接入验证。

用户说明资源由小米赛事赠送；候选 Base URL 为 `https://token-plan-cn.xiaomimimo.com/v1`。
[赛事说明](https://openvela.csdn.net/6a6180bf662f9a54cb936a05.html)确认有 Token Plan 资源，
但未证明定制后端使用的豁免；[普通订阅规则](https://mimo.mi.com/docs/zh-CN/tokenplan/Token%20Plan/subscription)
有此用途限制。因此按赛事发放条款核对实际使用范围，不直接断言赠送资源禁止接入，
也不把另购普通 API 作为代码适配前置。当前计划不包含凭据值或已获豁免的声明。

**执行和验收**

| 顺序 | 工作与产出 | 退出条件 |
|---|---|---|
| R0 复用与 provider 契约 | 固定官方源码 identity；列出 Agent 最小 profile 的 channel、入口、唯一 owner、代理路由、缓冲/线程预算和替换集合；以已知非私人文本验证 MiMo 对话请求格式 | 得到可构建的互斥 profile、可重放的脱敏协议夹具和真实接口验证记录；构建/接口各自标状态。选定 R1/R2 的单一路线；硬件预算未过不切换当前 BKVoice 产品基线 |
| R1 真实语音 provider | 按 R0 选定路线补 MiMo 整段 ASR、对话和流式 TTS：BKVoice 路线在 Gateway 编排，Agent 路线复用官方对话层并补语音 provider seam。只实现所选路径，固定回复用于故障回归 | 分别验证实际语音识别、可变文本回答、可解码 PCM；拒绝超长/损坏音频，断流和取消有界；记录供应商 PCM tuple、首包和重采样结果 |
| R2 AIDK 物理 PTT | 修复已有 Gateway 连通性，给选定唯一 owner 接入 PTT 和眼睛状态；先 10 轮冒烟，再完成 M2 的 50 轮及 30 分钟运行 | 按下收音、松开释放 MIC、收到回答后播放、完成回到空闲；断网/服务重启/超时/取消/旧 generation 不泄漏资源，记录端到端 P50/P95、堆栈和 deadline。固定音调不能代替真实问答证据 |
| R3 Android 首次联调 | 基于已工作的真实链路接 `console-v1` 状态、音量/persona 生效；修复并启用合适的 KVDB 后端，再完成安全认领与 BLE 配网 | 手机实际设置能在板端/会话生效，断电读回与失败回滚通过；凭据配置和设备注册闭环，不把隐藏串口装载当成 App 配网 |
| R4 独立扩展（可并行） | 官方词多人 KWS；随后非比赛版本“小冰”；IndexTTS 音色、OTA/资产、视觉传输按既有里程碑推进，不等待 R3 全部完成 | 各自通过模型、资源、权限与实板门才开启。“小冰”、最终音色、全双工 AEC 不阻塞首条 PTT 问答或比赛交付 |

R0 的接口/预算确认完成后即进入 R1，不为列举更多框架继续延长调研。对话协议验证与
Gateway 连通性排查可以并行；物理端到端验收需要两者都通过。已有摄像头约 30 fps、v321
存储一致性证据继续复用；只有变更触及共享时钟、DMA、电源或并发时才补对应回归。

2026-09-07 R0/R1 执行更新：核对官方 Agent 提交
`41723c61725c4e845bfee724f3ad2fafc416b6e1` 后，首条问答选定现有 BKVoice→Gateway 路线。
`voice_channel.c` 的流式语音仍直接注册火山，`audio_capture_close()` 丢弃公共 recorder
stop/close 结果且返回 void，语音通道还打印转写正文；这些先于资源预算构成替代缺口。
因此本轮不启用 Agent profile，不声称完成其构建或实板门；保留官方公共采播 API 与现有 bridge。
该选择没有新增 AP/CP owner、公共命令或固件配置，删除集合为空；只替换 Gateway 所选回复来源。

`gateway/shaniu` 已增加显式 MiMo provider：有界 PCM/WAV ASR、关闭 thinking 的短文本对话、
SSE TTS、FFmpeg 流式重采样和固定帧下行。供应商凭据仅从 Gateway 的 0600 文件读取。
现有固定回复仍为默认夹具；取消、超时、窗口和错误关闭共用同一连接 owner。
真实 MiMo 请求和本机 WSS 问答已通过，WAV 输出实测 24 kHz/mono/S16；
完整实板 PTT、原始 PCM 的板端速度/听感及 50 轮门仍待运行验证。
`make -C gateway/shaniu test` 的 31 项测试通过，包含本地 HTTPS ASR/chat/TTS 到真实 WSS、
鉴权/重定向/截断错误、取消/超时/下一轮、尾帧、首包早于 EOF、FFmpeg 满管道退出及重采样。
验证期间发现并修正了 FFmpeg 输入探测延迟和取消后等待读取管道的问题。
本地模拟测试不能替代云端或实板证据。没有新固件构建、刷机或 SDK/NuttX 修改。

用户提供凭据后，本机客户端经生产 Gateway 调用真实 MiMo，完成 1 轮问答、352 帧下行，
首音 2830 ms、协议错误 0。首条技术短句识别未完全匹配，第二条普通短句编辑距离为 0；
尚无整体识别率或 P50/P95 结论。实测 gzip 响应解压问题已修复，31 项回归通过。
COM8 可打开，但未取得 `bkvoice status` 响应，暂未装载设备配置或开启物理录音。
细节见 [MiMo/Gateway 真实验证](../../verification/bk7258/2026-09-07-mimo-gateway-live.md)。

<a id="feature-completion-execution"></a>

### 17.6 功能完善计划与实施批次

2026-09-07 用户授权：按以下计划持续实施。沿用现有 BKVoice/AP 唯一音频会话 owner、

本节中的旧 PTT 长按 3 秒、PTT 作为首条语音 gate，以及“唤醒未过则继续 PTT”等表述均为
历史执行记录，已被 2026-09-09 产品要求标记为 superseded；保留日期和证据，不得据此判断当前产品完成。
Gateway 云端服务和原生 Android；首条主线不等待框架迁移或最终音色训练。
本节细化 R2–R4 的执行批次，里程碑及验收状态仍统一维护在本文。

| 批次 | 实现与复用范围 | 完成条件与依赖 |
|---|---|---|
| F1 板端语音闭环（首要） | 恢复 COM8 启动/状态响应，核对当前镜像；接通 Wi-Fi 与受控 mTLS；复用现有 PTT、公共 recorder/player、双眼状态和 MiMo provider | 先完成 10 次真实按键问答，再做 50 轮与 30 分钟运行；测首音 P50/P95、MIC/DAC 回收、取消/断网/重连。当前阻塞是无串口响应；不得以主机 WSS 通过替代 |
| F2 连续对话（可立即实施） | Gateway 增加每个连接独立的有界短期上下文；按既有五种 persona 设计设置入口；复用 MiMo 请求、流式 TTS 和连接取消逻辑 | 首先完成多轮上下文：正常轮进入历史，取消/失败不进入；会话间隔离、断连清空、超限裁剪通过。persona 的 App 写入与生效在 F3 完成；不把短期上下文称为长期记忆 |
| F3 Android 控制台与心情 | 对接已有 Kotlin `console-v1` 客户端；Gateway 实现设备绑定、鉴权、状态快照/事件、音量/persona/取消操作；设备回报实际状态 | 首次联调先做在线/监听/思考/说话状态与取消，再做真实音量、五种 persona。重复请求幂等、旧 generation 拒绝、断线补状态；App 显示实际生效结果，不能只改 desired 值。实体设备反馈依赖 F1 |
| F4 认领、配网与持久配置 | 复用适合当前 host 的 BLE GATT、Wi-Fi 与 KVDB；先闭合后端短写/提交语义，再接 Android 认领与配网；设备凭据由认领流程供应 | 20 次发现/配网/错误密码/取消/重连/解绑；配置提交失败可恢复、掉电后可读；凭据保护和本地确认通过。F3 的受控联调无需等待 BLE 全部完成，交付前需移除对隐藏串口配网的依赖 |
| F5 官方唤醒（独立推进） | 复用 TFLM microfrontend/INT8 DS-CNN runner，准备多人语料、训练“你好，openvela”；补连续监听、pre-roll、VAD 和与 PTT 共用的会话入口 | 目标构建、独立说话人测试、连续负例 FAR/FRR、远场/噪声/扬声器回放、1 小时资源和 PM 恢复通过。首版播放时停唤醒；英语官方词另验。“小冰”保留到后续非比赛版本 |
| F6 视觉与屏幕 | 复用已验收摄像头/V4L2、SDIO 和双 framebuffer；先显式拍照→Gateway VLM→语音/眼睛反馈→App 图片，再做受控 MJPEG 预览 | 单帧 100 次成功/取消/超时回收；预览先 2–5 fps、单 viewer，后台/锁屏/断线停止；30 分钟运行后再提高传输帧率。录像约 30 fps 不等于手机传输已支持 30 fps |
| F7 OTA、音色与长期记忆 | 固件 OTA 复用 CP/AP 配对升级；补 App 更新状态、版本服务、签名资产下载/回滚；Gateway 后续接 IndexTTS 和可关闭、可删除的长期记忆 | 升级成功/失败/断电回滚且设备唯一数据保留；音色经授权、比较、签发和实板播放后才启用；长期记忆须独立授权、按设备/用户隔离、导出/删除验证。三项分别交付，不互相阻塞 |
| F8 组合体验与发布 | 实测 AEC、双屏刷新与音频、传感器事件、电池/温度、NFC；整合 App 首次配网→对话→视觉→更新→恢复演示 | AEC 做同条件开/关对照；全双工仅在其声学和并发门通过后开启。T5-Board/T5AI-Core 不测马达；AIDK 按实际接线验收。正式交付前再跑受影响板型构建与完整回归 |

依赖主线：F1 → F3 的实板联调 → F4 的首次使用闭环；F2 可在等待板端时先完成。
F5 的语料/模型、F6 的 Gateway/App 协议工作可独立进行，实板和 GPU 同时只有一个操作者。
F7 按更新、音色、记忆分别推进；先保持默认 MiMo 音色和半双工可用，再做体验优化。
不在缺少板端、手机和训练数据时承诺固定完工日期；每批通过即更新实际证据和下一缺口。

**本轮立即实施 F2 的短期上下文。** 可变历史由单个 Gateway 连接持有，限制最近 4 个完整
问答及总计 8192 字符；ASR/LLM/TTS 生成结果先待提交，只有 Gateway 成功发出本轮
`TTS_END` 后才进入历史。这表示网关已发送完成，不表示板端已播完；设备播放错误需丢弃
最近一轮上下文。取消、超时或错误丢弃待提交轮；连接关闭清空历史，不写文件、不跨连接恢复。
模型只得到固定 system policy 加成对 user/assistant 历史，不把转写提升为 system 指令。

F2 保留唯一服务命令 `python -m shaniu_gateway`，把共享无状态回复回调改为每连接创建的
会话对象，由现有连接 owner 调用完成/取消/关闭；移除旧回调注入入口及对应调用，避免两种
历史 owner 并存。公开 wire 协议、板端 owner、供应商密钥来源不变。
跨设备持久会话、自动情绪推断和 App persona 已生效不属于本轮完成声明。

每批提交按现有授权范围整理最小改动集，先区分已合并代码和工作区候选；临时探针、密钥、
模型私有素材、原始音频及日志不提交。更新主计划及日期化验收记录，不创建第二份任务清单。

**2026-09-07 实施结果：** F2 短期上下文已实现，Gateway 37 项测试通过；真实 MiMo 经
本机 WSS 连续两轮完成，第二轮携带第一轮上下文并正确引用测试信息，协议错误为 0。
首音分别 2470/1937 ms，仅为两个样本。详见
[多轮对话验证](../../verification/bk7258/2026-09-07-mimo-multiturn.md)。
F1 仍需恢复并核实 COM8 状态，最近一次读取无响应，本轮未重测串口；F3–F8 尚未按本节
完成验收，不能把主机测试解释为实板或 Android 全链路完成。

**F4 前置进展（2026-09-07）：** KVDB FILE I/O 缺陷已整理成团队维护补丁，
12 个故障/正常子场景及既有偏好 API/CLI 测试通过；上游未打补丁时其中 9 个场景失败。
详见 [KVDB I/O 验证](../../verification/bk7258/2026-09-07-kvdb-file-io.md)。
补丁尚未接入板级构建，持久化保持关闭。掉电恢复是下一独立实现门：当前 FILE 使用
O_TRUNC、commit 无同步；NuttX VFS 覆盖 rename 先删目标，不能以临时文件替换直接宣称
原子提交。本项不重开已通过的 SDIO 存储一致性基线。

**F4 恢复实现进展（2026-09-07）：** 继续复用 UnQLite 原生事务日志作为持久化候选，
已补日志开启、显式提交/失败回滚、errno 归一和 DIRECT 错误反馈；真实故障注入又定位到
引擎忽略同步错误，已另存最小引擎补丁。主机真实数据库的同步失败与同步点中断恢复通过，
见 [UnQLite 恢复验证](../../verification/bk7258/2026-09-07-kvdb-unqlite-recovery.md)。
目标文件锁、目录同步/flush、资源预算和补丁构建集成仍待闭合，板级持久化继续关闭。
持续执行已获授权，以上缺口不要求用户逐项再次发出“继续”。

**F2/F3 人物风格实施边界：** 保持现有五个 persona wire 名称；Gateway 的单连接会话
持有当前风格，固定 system policy 加白名单风格文本。既有服务命令增加启动默认风格选项，
会话仅在无进行中/待提交回复时切换，下轮生效且保留有界历史；拒绝任意 system prompt。
不新增另一套对话入口，不改变 emotion，不把默认配置当成 App 已获权的控制请求。
Android 的鉴权设置入口尚未连接前，只声明服务配置/会话能力，不声明手机设置已生效。

**人物风格实现结果（2026-09-07）：** 五种风格已加入 MiMo 请求，服务默认风格参数与
连接内切换能力完成；Gateway 40 项测试通过。验证下轮生效、保留历史、会话隔离、
进行中/待提交拒绝切换和固定 policy 保留。见
[人物风格验证](../../verification/bk7258/2026-09-07-gateway-persona.md)。
Android 鉴权控制端点尚未接通，人物情绪/音色质量没有据此宣称通过。
音量源码核实已有 `media_policy_set_stream_volume` 桥接；后续直接接该公共入口，
不重新实现 DAC 增益路径。

**人物风格真实接口进展：** 五种配置各用同一非私人输入调用生产 MiMo provider，
5/5 请求成功且文本可解析，单次 796–3799 ms；只有单样本，不代表风格质量或延迟分位数。
见 [人物风格真实接口验证](../../verification/bk7258/2026-09-07-gateway-persona-live.md)。

**F4 提交状态反馈边界：** 新增目录同步/日志删除故障注入已通过，修复引擎的错误吞掉。
日志删除后的同步失败会隔离句柄，重开后可能是新值，设置反馈必须读回协调，不能保证
所有提交错误都恢复旧值。见
[目录提交验证](../../verification/bk7258/2026-09-07-kvdb-directory-commit.md)。
已完成的 128MB 容量目标按用户确认收尾，当前主计划已登记为持续执行目标；COM8 最新
只读探测仍为 0 字节，未复位或刷机，实板闭环等待控制台恢复。

**音量生效实现进展：** 已把偏好音量接到下一轮 PTT 回复的公共 media policy；范围查询、
设置和读回通过后才播放，失败保留清理路径。轻量 player bridge 补齐范围/档位查询，
不新增 DAC 控制路径。开/关偏好主机测试、8 个故障/正常场景、6 个量化边界以及两个
AIDK AP 对象编译通过，见
[偏好音量接线验证](../../verification/bk7258/2026-09-07-voice-preference-volume.md)。
板级偏好仍默认关闭；需要后端启用、完整镜像和实板播放后才能声明音量验收通过。

**F4 目标端预检进展：** 事务引擎、KVDB 后端及 DIRECT 接口已在临时配置下用 AIDK AP
工具链编译通过。现有任务 TLS 和文件锁桶均为 0，正式启用需要解析新的配置并完整链接。
NuttX 目录 fsync 当前为空成功，FAT 删除自身刷新缓存，掉电语义仍需实板验证；不能将
Linux 恢复测试视为目标端保证。见
[KVDB 目标端预检](../../verification/bk7258/2026-09-07-kvdb-target-preflight.md)。
本预检阶段未改板级配置或刷机。

**F4 构建消费进展：** CMake 已接入团队补丁，按偏好开关在输出目录替换 KVDB/UnQLite
编译源，官方 checkout 保持不变；重复配置及真实引擎恢复测试通过。见
[构建消费验证](../../verification/bk7258/2026-09-07-kvdb-build-integration.md)。
板级配置仍关闭；下一步闭合持久路径、挂载与 SD NAND/MSC 介质所有权，然后完成目标
配置解析、完整链接和实板读回。此前临时对象编译不替代这些步骤。

**F4 介质接线进展：** 偏好读写已接共享 media volume，占用期间短时挂载，关闭数据库、
卸载后才释放；失败保留状态并在下次重试清理。生命周期和排他测试、三个 AIDK AP
对象编译通过，见
[偏好介质接线验证](../../verification/bk7258/2026-09-07-preferences-storage-lease.md)。
下一步是实际 Kconfig 配置与完整链接、实板读回；播放前读取遇录像/MSC 占用仍会失败，
并发体验与偏好缓存策略尚未闭合。板级偏好仍未启用。

**F4 完整链接进展：** `drivercheck_ap` 诊断配置已启用偏好后端，完整 MCUboot 链接验证
通过；最终配置、三个补丁副本编译源及 AP ELF 的 KVDB/TLS/文件锁符号均已核对。
AP 裸二进制 856424 字节，见
[完整链接验证](../../verification/bk7258/2026-09-07-preferences-full-link.md)。
生产 `openvela_ap` 仍未启用偏好。COM8 本轮状态查询仍为 0 字节，无刷机或实板读回；
软件继续完善播放期间已加载偏好的使用与失效策略，实板闭环等待控制台恢复。

**F3/F4 播放音量进展：** 已确认音量进入偏好层缓存，播放不再每轮读盘；成功设置更新，
不确定结果失效，显式偏好查询刷新。主机缓存/播放生命周期测试及完整 MCUboot 构建通过，
见 [播放音量缓存验证](../../verification/bk7258/2026-09-07-playback-volume-cache.md)。
首次加载与失效重读仍依赖介质；实板并发和 PTT 闭环未通过，Android 鉴权控制接线仍待完成。

**F3 设备寻址进展：** Gateway 已支持操作员证书登记，以验证过的 mTLS 叶证书绑定设备 ID，
HELLO 后上线、重复连接拒绝、断开/超时释放。46 项 Gateway 测试通过，见
[设备绑定验证](../../verification/bk7258/2026-09-07-gateway-device-bindings.md)。
这是控制寻址基础；Android bearer 授权、console-v1 端点及板端状态上报仍未接通，
不能将预登记等同于 BLE/App 认领。

**F3 Android 状态表达进展：** 首次连接尚未上报的音量/充电状态改为显式未知，协议、
状态存储和界面已贯通；43 项 Android 单元测试及调试 APK 构建通过，见
[Android 未知状态验证](../../verification/bk7258/2026-09-07-android-unknown-state.md)。
尚未安装手机；该阶段的 HTTPS 端点待办已由下述批次接续，不以固定默认值替代板端上报。

**F3 HTTPS 控制接线进展：** Gateway 已实现独立 console-v1 HTTPS/WSS 入口、设备范围
bearer 授权、快照/事件和真实 MiMo 人物设置，包含持久 generation、revision 检查及请求去重。
Python 与 Android 共用协议向量；51 项 Gateway 测试、44 项 Android 测试及 APK 构建通过，见
[HTTPS 控制验证](../../verification/bk7258/2026-09-07-console-https.md)。
未知遥测保持未知；操作员授权不等同 BLE 认领。远程取消、音量和 OTA 尚未接线，人物
设置尚未持久化；下一步推进取消与板端控制，实板 PTT 和手机端点部署验收仍未完成。

**F3 远程取消接线进展：** 板端 companion-v1 已修复同轮迟到 CANCEL 在 IDLE 被拒绝的问题，
允许正常结束或本地取消后同一非零轮次的幂等取消，仍拒绝旧轮、重放和未就绪会话。
新增主机测试先复现失败，修改后 `run-voice-companion`、`run-voice-session` 和
`run-voice-gateway` 通过；会话测试确认完成后的迟到取消不重复释放 DAC，并可进入下一轮。
通过唯一 CLI 在既有隔离诊断输出中完成 MCUboot 增量构建，未刷机。
当前 TTS_END 同步排空播放；此修复不证明能抢占排空等待。后续需接通带轮次校验的
Android/Gateway 取消请求及实际停止确认，console 取消入口尚未开放。

**F3 Gateway 取消发送进展：** `GatewayConnection.cancel_turn(turn_id)` 已能按精确轮次
停止 provider、清空 PCM 并发送实际 companion-v1 CANCEL；与上行状态处理串行，旧轮调用
不影响新轮。取消交叉到达的上行 PCM 保留序号、身份、窗口和时长上限检查后丢弃，不进入
模型；下一轮恢复正常处理。53 项 Gateway 测试通过，包含本地 mTLS 真连接上的取消帧、
provider 清理与下一轮继续，以及协议重放/旧轮拒绝。此处仅证明发送，不是板端停止 ACK；
Android 请求轮次关联及 HTTPS 取消入口仍待接通，未新增实板验收。

**F3 HTTPS 取消入口进展：** 已接 Android 现有 `turn.cancel` 空参数请求；console revision
随新轮次推进，旧快照取消被拒绝，即便新轮开始后尚未轮询快照也不会误取消。
每设备写请求串行化，并发同 ID 重试共享回执、只发一次 CANCEL。本地 HTTPS/mTLS
集成覆盖实际取消帧、并发重试和旧快照保护，54 项 Gateway 测试通过。Android 受理提示
明确为“取消已发送；尚未确认板端停止播放”。物理停止 ACK 与排空抢占仍待实现验收。

**F3 板端取消回执进展：** 串行会话清理成功、线程退出且回合空闲后发送同轮 ACK，
完成后的迟到取消也可确认；清理失败无成功回执。ACK 在 IDLE 保留轮次，发送前验证会话
及轮次。协议/Gateway/会话主机测试通过，包含停止失败注入；AIDK 诊断 MCUboot 增量构建
通过。Gateway 在收到同轮 ACK 前显示 cancelling，普通 ACK 不确认，54 项 Gateway
测试通过。Android 已接此等待状态；物理音频停止效果和同步排空抢占仍未验收。

**F3 立即停止接口进展：** 官方 `frameworks/multimedia/media/include/media_player.h`
明确规定 `media_player_close(handle, 0)` 立即停止，参数 1 等待完成。团队播放器此前忽略
该参数，现已按参数选择是否排空；准备失败清理也不排空，正常 `media_player_stop` 行为
保持。AIDK 诊断 MCUboot 增量构建通过，SDK/官方框架未改。取消仲裁仍需切换到立即关闭
路径，尚不能宣称播放抢占完成。本轮 COM8/115200 查询依旧 0 字节，未刷机。

**F3 取消立即关闭接线进展：** 回合仲裁仅在正常 TTS_END 清理时排空；取消、超时、
故障及恢复走停止/释放。音频适配的停止与释放复用 `media_player_close(handle, 0)`，
关闭成功立即清空句柄，失败保留重试所有权。回合、音频生命周期、会话与 PTT 主机测试
通过，新增关闭连续失败及恢复测试确认无排空调用，正常播放排空测试仍通过；AIDK
诊断 MCUboot 增量构建通过。未刷机或测量停止延迟。已进入正常 TTS_END 同步排空的
串行处理仍不能被随后排队的取消抢占，这个边界继续处理。

**F3 取消确认超时进展：** 5 秒未收到同轮 ACK 时，console/Android 转为
cancel_unconfirmed，明确停止结果未知；不清除待确认轮次，迟到有效 ACK 可恢复。
55 项 Gateway 测试及 Android 测试/APK 构建通过。源码核对确认 TTS_END 在 Gateway
锁内同步排空，RX 线程只排队；直接从 RX 线程关闭播放器会越过会话校验及清理所有权，
因此未采用该做法。同步排空抢占与实板停止延迟仍待闭合。

**F3 非阻塞播放完成改造边界（待实施）：** 已核对官方
`frameworks/multimedia/media/include/media_player.h` 和 `client/media_graph.c`：buffer 模式
通过 `media_player_close_socket()` 结束输入，并由已注册事件回调报告
`MEDIA_EVENT_COMPLETED`。团队播放器已有回调登记，但缺少 close_socket 结束输入实现；
语音适配目前未登记播放器完成回调。

- 对外仍使用现有 bkvoice/console 命令；播放 API 复用官方结束输入及完成事件，不新增
  SDK 私有取消接口。
- 播放适配拥有待播放缓冲、EOF 和底层完成事实；完成通知只发布结果，不从回调销毁
  播放器或修改 PTT。异步工作退出及回调静默必须先于句柄释放。
- 回合仲裁拥有等待播放完成状态和活动 token；runtime 串行处理完成、取消、超时，
  完成事件须校验会话/轮次。已经取消或换轮的迟到完成事件不得结束新轮或重复释放。
- 替换 TTS_END 中同步排空的调用链，正常尾帧仍须完整播放；取消调用立即关闭，并在
  清理成功后确认。原有显式同步 stop 的其他消费者不在本次删除范围。
- 验证需覆盖结束输入后主循环可处理取消、尾帧完成、关闭与回调交叉、清理失败重试、
  陈旧完成事件，以及最终实板播放/取消延迟。当前仅完成接口与所有权核查，未实现或
  验收该改造；不将既有主机取消测试当作此项通过证据。

**F3 播放器 EOF 后端进展：** 团队播放器已实现 `media_player_close_socket()`：后台
提交尾帧并等待完成，等待期间释放锁；立即关闭请求中止，清理/释放前 join，回调尚未
退出不得释放句柄，回调自关闭拒绝为 EDEADLK。EOF 后拒绝继续写入，重复结束输入不
重复建线程。同步 stop/close 对已有 EOF 工作先收尾，避免重复提交最终缓冲。
新增真实实现主机测试覆盖结束标记、完成前取消及回调与关闭交叉，纳入 host run；
播放器/录音适配测试及 AIDK 诊断 MCUboot 构建通过。语音回合尚未调用此异步 EOF
接口，下一步接入完成事件与串行回合处理；本轮没有实板验收。

**F3 TTS_END 异步接线进展：** 语音音频适配已登记完成事件，TTS_END 发出标准 EOF 后
进入 DRAINING 并返回；原同步排空清理分支已移除。runtime 每轮在接收队列处理后轮询
完成结果，后台只发布结果，停止/释放和状态切换仍由串行回合所有者执行；异步失败
清理后断开网络会话。无 TLS 配置由相同服务线程轮询。等待期间取消可立即关闭并 join
后台，关闭前回调静默及下一次 prepare 清除旧结果防止跨轮完成污染。
回合、音频、会话、PTT 与音量主机测试通过，包含等待期间取消、关闭失败恢复、正常
尾帧完成和迟到结果不结束下一轮；AIDK 诊断 MCUboot 构建通过。此为主机/构建证据，
尚未刷机测量真实取消延迟或完成整机验收。

**F1/F3 正常播放完成回执进展：** 会话轮询在异步播放结束并成功清理后发送同轮 ACK，
等待期间不发送，重复轮询不重复发送。Gateway 分开记录发送完成和 playback_confirmed，
取消/错误清除正常播放待确认记录，重复 ACK 不重复计数。会话主机测试、55 项 Gateway
测试及 AIDK 诊断 MCUboot 构建通过；TLS 夹具按临时证书 notBefore 做有限等待，未放宽
证书校验。Windows 枚举确认 COM8=CH340、COM16 存在，未发现 BK Loader 进程；已请求
开发板完全断电上电以恢复日志，本轮未烧录或取得新的实板功能证据。

**F2 上下文确认进展：** 已将 MiMo 历史提交移至对应正常播放 ACK；发送 TTS_END 仅
保留待确认对。下一轮开始而旧轮未确认时丢弃旧记录，重复 ACK 不重复提交，播放错误
仍可撤回最近一轮。新增本地真实协议/模拟 MiMo 集成验证未确认轮不入上下文、确认轮
进入下一次请求；连接隔离与播放错误回滚测试保持通过，共 56 项 Gateway 测试通过。
旧固件不发送完成 ACK 时仅保留独立轮次，不假定旧固件已播放；新固件实板连续对话
仍待验证。本轮没有外部模型调用、烧录、提交或推送。

**F3 App 播放状态进展：** console 在 TTS_END 后继续显示 speaking，正常播放 ACK 后
才回到 idle；5 秒未确认显示 playback_unconfirmed，Android 明确停止结果未知。
等待播放/取消确认时人物切换返回 409；已确认空闲时取消返回 409，保护已完成的上下文。
真实本地 HTTPS/模拟 MiMo 测试覆盖等待、超时、迟到 ACK、上下文提交及空闲取消保护；
57 项 Gateway 测试和 Android 测试/APK 构建通过，未实板验收。

**F3 音量协议进展：** C/Python 已支持可选 HELLO 能力位图及音量 GET/SET/REPORT，
包含百分比、结果及原请求序号校验；空 HELLO 的旧固件不开放音量控制。
新增协议边界与重放/重连测试通过，C companion/gateway/session 回归、Python 8 项协议
及 16 项 MiMo 测试通过，AIDK drivercheck CP/AP MCUboot 增量构建通过。
板端已接入可选音量回调，有回调才声明能力；音频后端启用 PREFERENCES 时复用媒体
策略设置与读回接口，按策略档位换算百分比，运行期设置跨语音轮次保留、不写偏好数据库。
回报绑定请求序号；驱动错误/非法读回标为未知，保留连接。读回是策略层结果，不是
独立 DAC 寄存器或声学测量。新增量化、设置失败、跨轮次保留及分发测试通过。
Gateway 已接入请求匹配、忙拒绝、5 秒超时和断线清理；迟到/无关回报不改变状态。
console 快照查询音量，volume.set 等待板端确认并缓存回执，成功快照显示量化读回值；
未知时 Android 滑条保持禁用。Android 增加音量错误码及运行期设置提示。
62 项 Gateway 测试通过，含本地 HTTPS→WSS 查询、设置、确认前等待、超时回执幂等
及再次查询恢复测试；Android 音量错误回执解析测试和 APK 构建通过，超时文案明确
生效结果未知。实板声音变化、重新连接与重启行为仍需验收。
本轮 COM8 115200 状态查询实际返回 0 字符，端口已释放，尚无新的实板启动证据。
本批无烧录、实板验收、提交或推送。

**F4 当前增量（2026-09-07）：** 已实现 TLS 内 SPV1 认证、板上确认门、递增序号和
分片整包上传；SCB1 包含 Wi-Fi/Gateway/CA，复用 BVC1 的 DER/密钥匹配校验。
新增纯校验接口不设置系统时间；正式加载保持原行为。文件提交保存完整配置、修订号
及事务 ID，覆盖短写、同步失败、提交未知和损坏文件拒绝；异步提交断线后保持未知，
不得中断存储后释放其上下文。私有存储 worker 已接产品启动，独立持有提交数据；
阻塞 fsync 时轮询/取消不会等待文件完成，结果不确定时禁止刷新或停止来清除状态。
网络试连回滚和按键/广播窗口仍未接入。
AP 异步 Wi-Fi API 复用已有控制 worker；CP 可在固定 SD NAND 主拓扑之外挂载
CSV 既有 persistent_data LittleFS，AP 通过 RPMsgFS 访问；未修改分区容量、格式化或
将私有目录导出 MSC。构建不能证明目前板上目录已经可用。

Android 总览已接“认领设备与 Wi-Fi 配网”：系统文件选择器导入认领凭据/CA，
运行时权限、30 秒定向服务扫描和设备选择后调用实际 GATT/TLS 连接。
界面不保存 Wi-Fi 密码；APPLY 前同步保存公开设备/事务 ID，保存失败禁止发送；
未确认结果跨 App 重启保留并阻止重复提交。已接只读 QUERY 和“核对上次提交结果”
入口：复用 TLS/秘密认证及本地确认；恢复窗口没有写入后端，拒绝 BEGIN/DATA/APPLY。
仅匹配已提交事务或明确空配置时清除 App 回执，其他结果保留未知。板端窗口与存储查询
回调尚待产品 owner 接线，不能视为实机恢复闭环。62 项 Android 单元测试/APK 构建、真实 mbedTLS
主机联测及 AIDK CP/AP 增量编译通过；网络步骤仍为测试回调，非实板配网。
当前 ADB 无设备，未取得新的手机/板端互通证据。本批未烧录、提交或推送。

### 17.7 当前未闭环总清单（2026-09-07 源码复核）

本节汇总 F1–F8，补齐此前逐批反馈遗漏的接线和验收项。第 7 节及早期 M2/M4 等
日期化记录属于历史快照，不能用其中的旧帧率、Android 状态或摄像头诊断故障替代当前
状态。已合并的 128MB SD NAND 一致性及摄像头帧率基线继续复用，不重开 1GB 支线。
下表“待实现/接线”表示当前产品路径未闭合；“待实测”不表示测试已经失败。

| 编号 / 主线 | 尚需完成的具体内容 | 当前证据及闭合条件 |
|---|---|---|
| U01 / F1、F3 手机入口 | 小米 10 的 ADB 授权、APK 安装启动、权限拒绝/重试、旋转/后台/锁屏/重连；真实手机 SSLEngine 与 AIDK mbedTLS 互通 | 已实际识别 Mi 10 / Android 13，ADB 授权通过；APK 0.5.0-a1 安装成功，主 Activity 冷启动 Status: ok、633 ms，UI 树确认主界面和配网按钮。MIUI 模拟输入权限在用户设置/重启后复测 KEYCODE_WAKEUP 成功；已解锁并进入配网页面，SCAN/CONNECT 权限读回为 granted=true，扫描启动与 30 秒结束提示已实测；发现并修复超时后仍显示扫描中的文案。扫描中回到桌面停留 3 秒再返回已实测显示扫描暂停。权限拒绝/重试、事务取消、TLS 与三端功能仍待实测。手机已由用户取走，App 日常调试改用现有 API 36 模拟器。修复并实测系统栏安全边距、隐私/更新返回设置、设置返回首页、配网返回首页，以及无设备时的添加入口；65 项 JVM 测试通过。模拟器用于界面和客户端流程，不替代真实 BLE/NFC、板端音频和三端验收 |
| U02 / F1 实板语音 | 核对当前板端镜像与启动；PTT→真实 MIC PCM→MiMo→扬声器；检查采样率、播放速度/听感、释放资源 | 真实 MiMo 的本机客户端验证已存在；当前组合未完成 10 次按键问答、50 轮、30 分钟及首音 P50/P95。COM8 当前已恢复响应；18.6.329+389 已烧录并确认启动，CP/AP/CPU2 健康；Wi-Fi 联网及 BVC1 RAM 供应通过。临时放行后手机 TCP 通过，头部采集确认 NAT 来源为 192.168.1.7；板端 CONNECT 仍超时，同窗口未见其连接包，Gateway HELLO 未建立。临时防火墙规则已删除。后续 AP proc 读取尝试后 COM8 暂无回应，软件 reset reboot 未取得启动回读，用户报告重上电后仍未取得 NSH；COM8 CH340 与原生 COM16 PnP 状态正常，控制台恢复待处理。源码核对发现语音使用普通 socket，而 AP Wi-Fi/HTTPS OTA 已使用 psock 避免共享描述符表争用；语音 psock 候选已完成正式 CP/AP 构建、5 项 TLS 与 9 项并发主机测试，并生成同已安装信任根的 391 OTA，尚未安装或证明根因 |
| U03 / F2、F3 对话及控制 | 多轮上下文、播放 ACK、取消/迟到回执、断网补状态；人物模式与音量实际生效 | 源码和主机测试已有，仍需手机→Gateway→板端验收；不能把 App desired 状态或 Gateway 发完音频等同板端播完 |
| U04 / F4 身份供应 | 每设备证书/私钥、认领 secret 和所有者 bootstrap 的生成、安装与读取；Gateway 设备注册及 App 所有者凭据绑定 | 已新增 BPI1 证书/私钥/secret 供应、原子身份安装及启动读取；`voice pairing` 生成 mode-0600 激活资料，App 一次导入设备 pin、证明和 Gateway 路由。身份经持久化后才 bind 配对 owner。主机测试及诊断/正式配置构建通过；真实 BPI1 串口上传完成但提交返回 EXDEV：产品代码误将 RPMsgFS 类型要求为 LittleFS，修复已构建待安装。`voice pairing --resume` 校验并重用原激活资料，主机测试确认上传字节一致且不覆盖原文件；私有持久化、Gateway 注册与 App 所有者凭据绑定仍待闭合，MiMo 密钥只留 Gateway |
| U05 / F4 窗口和按键 | 产品 worker 调用 pair start/step/confirm/close；本地开窗、二次确认、超时、停止广播、断开连接；与 PTT 按键归属互斥 | AP 20 ms 语音任务接入配对 owner，持久化身份供应/启动加载后实际调用 bind；3 秒松开首次认领、8 秒松开只读查询，认证后需新按键确认，120 秒窗口及断线清理。主机状态机/TLS 与 CP/AP 构建通过；实板窗口、按键与手机互通尚未验收 |
| U06 / F4 网络事务 | SCB1→BVC1；异步 Wi-Fi 试连→受认证 Gateway HELLO；成功再调用文件提交；失败/取消恢复旧网络及语音配置 | 产品 network owner 已串接 SCB1→BVC1、AP Wi-Fi 独占试连租约、真实语音 Gateway HELLO 和异步文件提交；取消/确定失败执行网络及语音回滚，不确定落盘保持隔离。实际控制辅助函数及产品回调主机测试覆盖错误密码、Gateway 拒绝、取消和提交时断线；实板网络事务仍待验收 |
| U07 / F4 私有持久化 | 首次私有目录初始化、CP 挂载/AP RPMsgFS 就绪与重试、启动加载配置、损坏处理；可信时间恢复/刷新 | 文件任务仅在已挂载片内 LittleFS 上创建私有目录，不自动格式化；身份与配置分开保存，runtime 重试挂载就绪、加载身份并恢复已提交连接。复用官方 NTP 单次客户端与 DHCP DNS 供给，在重启 Gateway TLS 前获取新时间；保留证书日期校验。NTP 为普通网络校时，不能称为认证时间。持久化/恢复/校时主机测试及正式配置构建通过；实板只对已核验全 FF 的片内私有区显式初始化 LittleFS。AP 的 RPMsgFS 类型兼容修复待安装；真实介质掉电和坏配置仍待验收 |
| U08 / F4 回执恢复 | 将只读恢复窗口接实际 storage receipt；已认领设备只开放本地确认后的查询；提交时断线/掉电/手机进程退出后核对 | 8 秒本地手势连接真实 storage receipt；App 导入身份后发现未决事务会进入核对路径，不再次索取 Wi-Fi 密码。不同事务及存储错误保留未知。身份供应调用已接通；提交时断线、掉电及手机进程退出的实板组合仍待验证 |
| U09 / F4 解绑与重配 | 本地授权解绑、旧所有者/令牌撤销、旧 bootstrap 失效或轮换、换 Wi-Fi/Gateway 的配置更新及回滚 | 当前首次认领默认拒绝 already_claimed；只读查询不是重配或解绑。20 次认领/错误密码/重连/解绑总验收尚未完成 |
| U10 / F3、F8 状态与权限 | 电量、充电、固件版本、情绪和权限来源；手机授权→Gateway→设备能力执行与撤销 | console 仍返回未知电量/版本/情绪、未授予权限；`claimed=True` 来自受控注册连接，不能当 BLE 所有权证据。后端实际 mutation 仅取消、音量、人物模式，其他操作返回 unsupported_operation |
| U11 / F5 官方唤醒 | 多人通用“你好，openvela”有效模型、语料/独立说话人评测、MIC owner、pre-roll/VAD、播放时暂停/恢复 | 前端/runner/训练入口已有；50 帧 pre-roll、时间戳断层、连续语音/静音、空唤醒及最大时长已有纯 App 宿主验证。未完成正式模型、MIC/session 接线、门限标定、目标资源、FAR/FRR、远场/噪声、1 小时和 PM 恢复验收。英语官方词另验；“小冰”仅后续非比赛扩展 |
| U12 / F6 拍照理解/屏幕 | 显式触发→单帧上传→Gateway VLM→语音/双眼→App 图片；统一 Camera 占用与隐私指示；屏幕映射/状态/动画 | 复用已合并 V4L2/显示基础；尚需产品链路和 100 次成功/取消/超时、双屏 30 分钟并发及资源回收证明 |
| U13 / F6 视频预览 | Gateway relay、Android viewer、2–5 fps 单 viewer、背压和前后台自动停止 | 不把本地约 30 fps 录像称为手机视频传输；30 分钟运行、锁屏/断线/低电停止和与语音互斥门未闭合 |
| U14 / F7 OTA | 发布目录/版本与板型匹配、App→Gateway→板端升级、进度/重启确认、失败和断电回滚、唯一数据保留 | 底层配对 OTA 可复用；App 已按来源版本和空闲状态开放本地确认入口，Gateway 只向具备能力的绑定设备发送 manifest SHA-256，板端已有严格序号/摘要/阶段回报协议。三端 host 测试覆盖拒绝、超时、迟到回报和终态冻结；尚未接设备受信 HTTP 源、跨重启事务恢复及实板回滚，因此不能称为端到端升级完成 |
| U15 / F7 IndexTTS | Gateway provider 与默认 MiMo 切换、流式/取消/资源限制、授权音色选择、签名和撤销 | 历史离线评测不等于产品接入或实板播放验收。先保持默认 MiMo 声音，不阻塞基本问答 |
| U16 / F7 长期记忆 | 独立授权和可关闭开关、按所有者/设备隔离、存取策略、查看/导出/删除与撤销 | Gateway 已实现默认关闭、按设备隔离的私有 SQLite 记忆，仅在板端播放 ACK 后提交；撤销/删除清空，失败关闭写入。Android 只提供关闭和清除，不提供远程开启。81 项 Gateway 测试及 Android 构建已通过；所有者本地开启、查看/导出和三端实机验收仍未闭合 |
| U17 / F8 外设及体验 | AEC 同条件开关对照、音频/双屏/相机/存储并发、低电策略、姿态事件、NFC 场景、故障隔离和长稳 | 外设驱动或只读查询通过不代表产品策略通过；T5-Board/T5AI-Core 无马达，AIDK 按实际接线验收；首版继续半双工 |
| U19 / F4、F8 NFC 辅助连接 | 手机 HCE↔板端 ISO-DEP/APDU、前台触碰发现与 BLE 配对衔接；后续触碰交互 | 小米 10 实查支持 NFC/HCE；曾为 BLE/UI 联调临时关闭 NFC，当前已恢复开启，USB 常亮按授权保留；现有 MFRC522 驱动尚无 ISO-DEP/APDU 产品链路。按基础 PTT、App 控制及配网认领联调之后推进；不把随机 UID 当身份，不因贴近跳过证书、认领证明或本地确认。新增入口尚未实现，原有 NFC 外设基线不变 |
| U18 / F8 产品交付 | 将诊断配置已验证能力整合进正式配置；清理临时诊断后精确提交；APK/固件/Gateway 版本、签名资产、恢复及完整演示 | PTT、VOICE_TLS、PROVISION_GATT、CP 私有 LittleFS、双核 RPMsgFS 与单次校时已纳入正式 openvela_cp/openvela_ap，隔离构建通过并核对最终配置。已生成独立临时签名信任链并完成 389 全量烧录及启动核验；390 是沿用已安装 MCUboot 信任根的待装签名 OTA。实板验证、其余产品能力整合、精确提交及三端发布仍未完成 |

复核入口：`bk7258_product_lifecycle.c`、`bk7258_provision_pair.c`、
`bk7258_provision_storage.c`、`bk7258_voice_runtime.c`、`bk7258_voice_config.c`、
`gateway/shaniu/shaniu_gateway/console.py` 与 `server.py`、Android `MainActivity.kt`
及 `provision/`、AIDK 两组 AP defconfig。CodeGraph 发现后以当前源码调用点确认；
本次没有重新跑宽范围测试或把既有测试重复记为新验收。

用户已指出现有界面过于原始且暴露联调参数。首页第一版已在小米 10 安装启动并检查：
单一“添加傻妞”主要操作、陪伴/心情/设置三入口；地址/令牌/诊断信息移入仅调试包可见
的开发者联调页。配网页面已拆为找到设备→连接 Wi-Fi→确认结果三页，普通入口不挂载开发者参数视图；
仅开发者联调入口且调试包允许导入凭据/CA、配置服务地址。小米 10 已验证第一页
无技术字段/密码框、无提前下一步按钮、启动扫描、手动停止后恢复查找及返回首页。
未选择设备不能推进；身份/服务资料未就绪时停在第一步，不收集 Wi-Fi 密码，不伪造
添加成功。第二/三步已构建，但真实身份供应、选中设备后的配网/结果页仍未实机走通。
最终身份/证书/服务配置由供应与认领流程承担；人物素材、心情/相册及完整引导验收仍待完成。

**下一步执行顺序：**

1. U01 小米 10 ADB 可用后安装现有 APK，先验启动、权限和控制台；不等待 BLE 全功能。
2. U02/U03 接实板 PTT 和三端控制，复用已有 MiMo、音频及摄像头/存储基线。
3. U04–U09 接完整首次使用流程：身份→本地窗口→网络试连→私有提交→重启/回执恢复→解绑重配。
4. U10/U12 补真实遥测、权限、拍照理解和双屏反馈；U11 的语料/模型工作按资源独立推进。
5. U13–U17 按既定 Beta 边界分别验收，再做 U18 产品整合和发布；不因某个 Beta 项未过阻塞基础语音交付。

## 18. 尚待输入，不虚构日历

依赖结构和退出门已经确定，但日历排期还需要以下真实输入：

- 可持续使用的 AIDK 数量，以及刷机、恢复和独占实板时间窗口；
- 历史已指定小米 10；当前系统版本、连接可用性与调试 APK 安装条件待现场核对；
- Gateway 部署在同局域网主机、移动电脑还是远程主机；
- MiMo 赛事资源的实际调用配置与适用范围；后续 IndexTTS/KWS 的 GPU 可用时段；
- 实际参与开发、测试和美术的人数与每周可用时间；
- Contest 截止日期、演示环境和网络限制。

这些输入确认后，才能把 M0–M8 转换成周计划、责任人和日期。缺少输入时只使用依赖顺序，
不把估算日期写成承诺。

## 19. 执行与交付判据

用户已授权按计划开始实施，本文进入 `IN_PROGRESS`。第 18 节尚待输入仅约束相关部署、
日历和物理验收，不阻塞已授权且不依赖这些输入的实现。

每批完成后更新实现状态、实际证据和剩余风险；主机、构建、刷机、实板与用户体验验收分别
记录。比赛交付必须覆盖 Contest MVP、协议与设备身份边界、M0–M8 对应退出门；M7 的三个
独立 gate 和 M9 扩展保持各自边界。提交前核对当前远端基线与精确改动集，不整体纳入脏树。
执行过程中不静默扩张范围，也不把计划获批视为功能验收通过。


## 2026-09-10 chip-board 整改恢复点

构建归属已迁回 chip，三个内核 wrapper 按 C 保留并增加版本/源码/配置门禁；
三板六个最终 clean ELF 调用链及对象重定位检查通过，固件 bin 与整改前基线一致。
源码与构建分层检查覆盖 Dolphin，正反例通过。实板仍为旧 411；新固件未部署，
三板运行、触摸、SDIO 回归未完成，不能作为产品验收。
详细命令、哈希索引、实际阻塞和恢复顺序见
[chip-board 整改报告](chip-board-wrapper-review-20260910.md#整改实施检查点2026-09-10)。
本轮未提交或推送，保留原有工作区成果。


## 418 全量下载及 AIDK 实板回归（2026-09-10）

用户明确要求“直接全量下载不要管那个规范”，覆盖当前整机读回前置。采用哈希匹配的
历史同板 accepted base，不冒充当前备份；保留新的独立 BL1/MCUboot 签名、布局、
回滚计数及8MiB校验。生成并通过 COM8 HIL 自动软件复位全量写入 `18.6.354+418`，
无需人工按键；Flash PASS、实板 pair=confirmed/counter418。

证据根目录 `out/chip-board-remediation-20260910/download418/`：
`release/release.json` 标识产物；全量 BIN SHA256
`56f38e601807580c2cc011906025d91c01597133f7f69fd5d6659e48edd306e1`。
`hil-run/result.json` 保留下载结果，`boot-status.log` 证明本轮启动；
`signed-elf-audit/result.json` 核验新签名 CP/AP 的 wrapper 调用边；
`kernel-live.json/log` 保留十轮实板计数：AP tick 8683→11134、CPU2 heartbeat
790→1013、IPI 813→1036，全程递增，CP sleep/ps 响应；故障/恢复0/0。
双屏服务 READY、SD NAND 默认资源读取成功，电池/运动/NFC命令响应。
物理屏幕映射、标定、已知卡片、SDIO写读压力与另两板仍未验收，不能称三板目标完成。
本次使用 Windows COM8 真实命令及相同计数断言，未宣称 Linux pytest fixture 已执行。
签名临时私钥已删除，仅保留公开验证证据；未提交或推送。

此前“新固件未部署/需当前备份”的 AIDK 阻塞已由本次明确授权及实际部署解除；
恢复入口以本节为准。下一步为确认另外两板物理路由并执行对应回归，AIDK保持418。
