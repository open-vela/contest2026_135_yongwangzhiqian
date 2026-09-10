<!-- SPDX-License-Identifier: Apache-2.0 -->
# 源码许可证与来源记录

## 审计范围

本记录覆盖 Git 已跟踪的 `*.c`、`*.cpp`、`*.h`、`*.S`、`*.s`、`*.ld`、
`*.py`、`*.sh` 和 `*.ps1` 编译/可执行源码，AI 对话日志不作为源码统计。
2026-08-31 按当前拟提交工作树（包含拟提交的未跟踪新增文件，排除删除项、`logs/`、
`memory/` 及忽略的 SDK/工具链/构建产物）复核结果为：

- 非测试源码 370 个，其中 369 个声明 Apache-2.0 SPDX；唯一例外是保持初始化原样的
  `app/hello_app/hello_app_main.c`，由仓库根 `LICENSE` 管理；
- `tests/host/bk7258/`、`app/testing/bk7258/` 和 `tests/pytest/` 共 169 个测试源码，
  Apache-2.0 SPDX 覆盖 169/169；
- 合计 539 个，Apache-2.0 SPDX 覆盖 538/539，另有上述一个明确模板例外。

本轮新增源码在创建时声明 SPDX；既有源码若只缺机器可读标识，则在保留原版权与
完整许可正文的前提下补齐。任何从 SDK 或外部仓提取的协议/初始化序列均在下表固定
仓库、版本、路径和许可证，不因改写为 NuttX 组织形式而省略来源。

## 来源分类

| 范围 | 来源与许可处理 |
|---|---|
| `nuttx/drivers/contactless/isodep.c`、`nuttx/include/nuttx/contactless/isodep.h` | 团队 Apache-2.0 实现，激活参数依据 ISO/IEC 14443-4:2018 第 5 节（公开预览）与 NXP AN12057 Rev. 1.2（2026-07-03）；不复制外部协议栈代码。 |
| `nuttx/drivers/contactless/mfrc522.{c,h}`、`nuttx/include/nuttx/contactless/mfrc522_frame.h` 与 `nuttx/patches/contactless/0001-*` 至 `0005-*` | 基于 `https://github.com/open-vela/nuttx` 提交 `76354c637858ecb0aa4601629327acb6f44a26bb` 的 `drivers/contactless/mfrc522.{c,h}` 和 `include/nuttx/contactless/ioctl.h`（Apache-2.0），保留上游许可。团队差分提供错误传播、CRC_A 帧交换与超时控制；定时器行为参照 NXP MFRC522 Rev. 3.9（2016-04-27）手册 8.5、9.3.3.10 节，不复制手册正文。 |
| `app/bk7258/bk7258_voice_kws*`、`tools/bk7258/_lib/voice_kws.py` 和对应 host tests | 本仓 Apache-2.0 适配；直接编译工作区 `apps/mlearning/tflite-micro/tflite-micro` 的 `tensorflow/lite/experimental/microfrontend/lib`，调用同树 `tensorflow/lite/micro/{micro_interpreter.h,micro_mutable_op_resolver.h}`（Apache-2.0），不复制前端或推理实现。本轮验证的实际 TFLM 提交为 `94f7cee178aeceb492a074b4e092db2706d7c9c2`；manifest 跟随 `openvela/dev-ai-contest-2026`，并非外层 Make 下载回退值 `cfa4c91…`。固定点 FFT 由上游 `kiss_fft_int16.cc` 编入现有 `apps/math/kissfft/kissfft` v130 的 `kiss_fft.c`、`tools/kiss_fftr.c` 及头文件，许可 BSD-3-Clause（Mark Borgerding，见该目录 `COPYING`）。候选 metadata 保存实际前端源/头内容哈希；发布时保留这些上游许可。训练采用 TensorFlow/Keras 2.15.1；真实语料与权重的许可和验收随资产独立提供。 |
| `frameworks/patches/kvdb/0002-unqlite-explicit-journaled-commit.patch` | `open-vela/frameworks_system_utils` 提交 `5e582301ffa1401d1e48d49cea46edeba97b822e` 的 `kvdb/unqlite.c`、`kvdb/direct.c`，Xiaomi Apache-2.0；团队差分恢复日志和显式提交。 |
| `external/patches/unqlite/0001-propagate-commit-sync-errors.patch` | `https://github.com/open-vela/external_unqlite` 提交 `25731ab0e2a4aa119df1329f799cd571a794720c` 的 `unqlite.c`，Symisc BSD-2-Clause；保留原版权/许可和 CRLF，仅维护同步错误传播差分，主机测试应用于临时副本。 |
| `frameworks/patches/kvdb/0001-file-handle-partial-interrupted-io.patch` | 源自 `https://github.com/open-vela/frameworks_system_utils` 提交 `5e582301ffa1401d1e48d49cea46edeba97b822e` 的 `kvdb/file.c`（Xiaomi Apache-2.0）。团队维护 I/O 语义差分，host 测试只在临时副本应用；未修改官方 checkout 或启用板级持久化。 |
| `app/bk7258/bk7258_preferences.[ch]` | 本仓 Apache-2.0 产品配置适配，调用 OpenVela `frameworks/system/utils/include/kvdb.h` 的公开 API；KVDB 实现保留在原仓，未复制存储后端。配置后端与掉电验收尚未闭合，默认不启用；不存储凭据。 |
| `app/hello_app/hello_app_main.c` | 来自本仓初始脚手架提交 `8987bbc`，并在 `7d9c26c` 统一为 team 135；本轮按该基线逐字恢复，不为许可证格式单独改写模板。仓库根 `LICENSE` 为 Apache-2.0。 |
| `app/bk7258/*.c` | 13 个 BK7258 命令与 3 个 Agent 生命周期/轻量音频桥接源均由本仓创建；命令从 hello 模板目录分离到独立产品应用，Agent 源则从板层迁入应用层。全部适用 Apache-2.0。 |
| `boards/bk7258/*/include/board.h` | 本仓提交 `eaef241` 创建的三个最小板级转发头，不复制其他 NuttX 板实现；适用仓库默认 Apache-2.0。 |
| `nuttx/drivers/lcd/gc9d01.c` | 初始化序列源自 Beken BK-AVDK v3.1.1.9 的 `components/bk_peripheral/src/lcd/spi/lcd_spi_gc9d01.c`（Apache-2.0）；本仓重写为传输无关、可上游化的 NuttX LCD 驱动，未暴露 SDK 私有面板对象。 |
| `nuttx/drivers/lcd/ili9488_rgb.c` | 初始化序列源自 `tuya/TuyaOpen-T5AI` 固定提交 `13379b63e07e78770fb4d0bffe36db2754658132` 的 `tuyaos/tuyaos_adapter/src/test/test_dvp/lcd_ill9488.c`（仓库根许可证 Apache-2.0）；本仓实现仅保留通用寄存器序列和传输回调，并复用官方 NuttX `ili9488.h` 命令定义。 |
| `nuttx/drivers/input/gt9xx.c` | 基线逐文件来自 manifest 工作区 `open-vela/nuttx` 固定提交 `76354c637858ecb0aa4601629327acb6f44a26bb` 的 `drivers/input/gt9xx.c`（Apache-2.0）；本仓仅补齐标准 `TSIOC_GETMAXPOINTS` ioctl，并以独立 Kconfig/build gate 替代而非同时链接官方实现。GPIO、复位、电源和 bitbang-I2C 实例策略仍由物理板绑定提供。 |
| `nuttx/drivers/sensors/sc7a20.c` | 设备 ID、寄存器、量程和 ODR 语义源自 manifest 固定的 Beken BK-AVDK v3.1.1.9 提交 `cb080de1655d579c7593ecf504c440997c4c137b` 的 `ap/components/bk_gsensor/gsensor_sc7a20.c`（仓库根许可证 Apache-2.0）；本仓重写为硬件无关寄存器 transport 和标准 NuttX uORB sensor lower-half，未复制 SDK 线程、私有回调或 I2C/GPIO 实例策略。 |
| `chips/bk7258/bootloader/` | BL1、BL2、链接脚本及板级 MCUboot 配置/ABI 由本仓提交创建。BL2 在构建时链接工作区 `apps/boot/mcuboot/mcuboot` 的固定上游源码；仓内文件只是 BK7258 启动、Flash map、安全计数和最小配置适配，不包含上游 bootutil/TinyCrypt 实现副本。两侧均为 Apache-2.0。 |
| `chips/bk7258/common/bk7258_os_adapt.c` | 本仓面向 NuttX 编写的 SDK OS 适配层，文件原有完整 ASF Apache-2.0 许可正文；本轮仅增加 SPDX。 |
| `chips/bk7258/ap/bk7258_sdio.c` | SDIO TX DMA 启用及通道归属检查依据 manifest 固定的 Beken BK-AVDK v3.1.1.9 提交 `cb080de1655d579c7593ecf504c440997c4c137b` 的 `ap/middleware/driver/sdio_host/sdio_host_driver.c` 与 `ap/include/driver/dma.h`（Apache-2.0）；复用 SDK 公开 API，未复制私有 DMA 状态机。SDK 开关来自本仓 ap-aidk profile 并由规范 SDK rebuild 入口生成匹配静态库。 |
| `chips/bk7258/common/bk7258_sdk_partition.{c,h}` | 本仓实现的 SDK 语义分区 ID 与生成布局行之间的显式转换；枚举顺序对齐 manifest 固定的 Beken BK-AVDK v3.1.1.9 提交 `cb080de1655d579c7593ecf504c440997c4c137b` 所产出 SDK profile `chips/bk7258/bk_idk/armino_as_lib/versions/v3.1.1.9/cp-aidk/include/partitions.h`（生产源的 `cp/include/driver/flash_partition.h` 通过 `<partitions.h>` 消费该枚举），未复制 SDK Flash 实现。上述 SDK 头及本仓实现均为 Apache-2.0。 |
| `chips/bk7258/include/eth_mac*.h`、`lan8742.h` | 来自 manifest 固定的 Beken SDK v3.1.1.9 Ethernet 公开头。原文件保留 Beken 版权和完整 Apache-2.0 正文；其中 `lan8742.h` 与 SDK 相同，其余仅有换行或已注明的 NuttX 符号兼容调整。 |
| `docs/platforms/bk7258/hardware/t5ai-core/probe/*.{c,ld}` | 本仓提交 `56b303e` 创建的历史实板探针源码，适用仓库默认 Apache-2.0。 |
| `boards/bk7258/build/vendorsetup.sh` | 本仓提交 `eaef241` 创建的构建环境适配脚本；随完整板目录映射进入 OpenVela 的 vendor 树，适用仓库默认 Apache-2.0。 |
| `tools/windows-hardware-debug/**/*.{cpp,ps1}` | 本仓硬件调试工具，由 2026-07-31 至 2026-08-03 的调试与 BLE 验证提交创建；10 个文件在本轮前已声明 Apache-2.0 SPDX。 |

Beken SDK 由 [`contest2026_135_yongwangzhiqian.xml`](contest2026_135_yongwangzhiqian.xml)
固定在提交 `cb080de1655d579c7593ecf504c440997c4c137b`，其根 `LICENSE` 和上述
Ethernet 公开头均声明 Apache-2.0。MCUboot 由 openvela 工作区的 `apps` 项目提供，
其上游目录保留独立 `LICENSE` 和 `NOTICE`。

BK7258 主机测试的更细分类见
[`tests/host/bk7258/PROVENANCE.md`](tests/host/bk7258/PROVENANCE.md)。第三方项目、预构建工具、
生成输出及历史材料继续适用各自声明；SPDX 补齐不改变其版权归属。

## Gateway 语音服务

Gateway 的自有协议、MiMo 适配、外部依赖及测试来源独立维护于
[Gateway 来源说明](gateway/shaniu/SOURCE_PROVENANCE.md)；模型协议适配不包含第三方源代码副本。

## KVDB 构建接入

`app/bk7258/bk7258_preferences_storage.*` 及配套主机测试为本项目 Apache-2.0
实现，复用既有 `bk7258_media_volume` 占用接口及 NuttX mount/umount 公共接口。

`frameworks/cmake/kvdb_patches.cmake` 为本项目 Apache-2.0 构建接入代码，
仅在输出目录消费 `frameworks/patches/README.md` 列明的 framework/UnQLite
维护补丁；生成副本保留原 Apache-2.0 / Symisc BSD-2-Clause 许可，不另复制上游实现。

## BLE GATT 通知维护补丁

`nuttx/patches/bluetooth/0001-gatt-report-notification-enqueue-result.patch`
派生自 OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`
的 GATT 源码/头文件，保留其 BSD-3-Clause 许可；相应 host harness 为本项目
Apache-2.0 实现。官方 NuttX 工作树不作修改，补丁仅应用到隔离构建副本。

## 认领 TLS 与 GATT

`nuttx/patches/bluetooth/0002-expose-gatt-connection-lifecycle.patch` 将同一
OpenVela NuttX 基线的 `wireless/bluetooth/bt_hcicore.h` 既有连接回调结构和
Host 函数声明公开到 `include/nuttx/wireless/bluetooth/bt_gatt.h`，保留上游
BSD-3-Clause 许可，不复制 Host 实现或改变 SDK。

`app/bk7258/bk7258_provision_tls.[ch]` 和
`tests/host/bk7258/test_provision_tls.{c,py}` 为本项目 Apache-2.0 实现，调用
工作区 `apps/crypto/mbedtls/mbedtls` 的公开 mbedTLS 3.4.0 API（Apache-2.0，
官方 apps `crypto/mbedtls` 从 `ARMmbed/mbedtls` 的 v3.4.0 发布包引入）；
不复制密码算法，不改上游源码。主机 gate 使用该目录默认主机构建配置，
只对上游 PSA helper 的 missing-prototypes 保留 warning 而不升级 error；
产品源仍使用 `-Werror`。身份由 OpenSSL 生成临时 P-256 测试证书，结束删除。
板端使用 AIDK 的实际 mbedTLS 配置另做镜像构建，主机配置不作为目标验收。

`app/bk7258/bk7258_provision_owner.[ch]` 和
`tests/host/bk7258/test_provision_owner.c` 为本项目 Apache-2.0 实现。
复用现有 AP 语音任务、CP 按键租约及 provision pair/storage API，
不复制 SDK 的按键或 BLE 示例状态机。

`app/bk7258/bk7258_provision_gatt.[ch]` 及
`tests/host/bk7258/test_provision_gatt.py` 为本项目 Apache-2.0 实现，使用
NuttX 公共 GATT/UUID/锁 API 与团队维护的定向通知接口；不包含 SDK 私有设备对象。

`android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/`
及相应 host tests 为本项目 Apache-2.0 实现，调用 Android/JVM 公共 JSSE、
X509Certificate 和 MessageDigest API，没有复制密码库或上游 Bluetooth 实现。
测试身份由本机 JDK keytool 临时生成，测试结束删除，不包含真实设备凭据。

## 摄像头与 SDIO 录像适配

- `nuttx/drivers/video/gc2145.c`、公开头文件及板级回调为本项目 Apache-2.0
  实现。控制寄存器依据 GalaxyCore GC2145 CSP DataSheet V1.0
  （2013-12-01）第 22、23、32、35 页；默认值参照固定 SDK 的
  `ap/components/bk_peripheral/src/dvp/dvp_gc2145.c` 初始化表。
- `chips/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio-tx-start.patch` 和
  `ap-dvp-register-errors.patch` 派生自固定 Beken 提交
  `cb080de1655d579c7593ecf504c440997c4c137b`（Apache-2.0），分别修复 SDIO
  FIFO 中断掩码顺序/判定和 DVP/GC2145 寄存器错误传播。规范 rebuild 仅为
  `ap-aidk` 在临时克隆应用，未修改固定 SDK checkout。
- `nuttx/patches/{video,mmcsd,fs}` 是针对 OpenVela NuttX
  `76354c637858ecb0aa4601629327acb6f44a26bb` 的 Apache-2.0 修复，涵盖
  V4L2 scalar 控制初始化/编号、MMCSD 传输限制/完成与 FAT 错误传播。
  保留为维护补丁，应用步骤见 [补丁说明](nuttx/patches/README.md)。
- `app/bk7258/bk7258_vision_*`、`bk7258_media_volume.*`、I2C 资源引用计数和
  配套宿主回归是本项目实现，使用 Apache-2.0。
- BK7258 HardFault 复位原因 `0x11` 复用清单固定的 Beken SDK v3.1.1.9
  `cp/include/components/system.h` 中 `RESET_SOURCE_HARD_FAULT`（Apache-2.0）；
  芯片层保留编译期 ABI 校验，自动复位策略复用 NuttX
  `BOARD_RESET_ON_ASSERT`，不修改 SDK 复位实现。
