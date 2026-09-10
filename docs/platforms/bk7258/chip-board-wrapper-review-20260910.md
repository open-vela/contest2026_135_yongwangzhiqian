# BK7258 chip / board / wrapper 审查（2026-09-10）

范围：当前工作区三板app配置、board/chip源码、Make/CMake，以及刚生成的六个
CP/AP direct开发ELF。不改运行源码，不刷写。编译/符号存在不证明实板行为正确。

## 已确认的问题与整改顺序

1. **构建归属未收束（优先整改）**：`boards/bk7258/common/CMakeLists.txt:3`
   要求SDK输入，`:123`起枚举SDK库并加入链接；对应
   `boards/bk7258/common/scripts/Make.defs:149`起也解析SDK库。
   `CMakeLists.txt:88`及`Make.defs:102`声明分区SDK --wrap，但真正实现已在
   `chips/bk7258/common/bk7258_sdk_partition.c:119`，注释“board archive”过时。
   建议将SDK依赖闭包、链接和兼容包装声明一起移到chip构建层；板仅选择布局和
   硬件绑定。迁移前后需比较链接输入/顺序和符号，不能只凭移动文件认定等价。
2. **内核包装需要单独收敛（当前在用，风险较高）**：
   `chips/bk7258/CMakeLists.txt:679`包装arm_doirq/nxsched_resume_scheduler，
   AP还包装nx_bringup。`cp/bk7258_vectors.c:319`修改异常帧CONTROL位并捕获
   调度恢复寄存器，`ap/bk7258_ap_smp.c:1329`在nx_bringup后放行CPU2。
   这是NuttX架构/调度适配，不是SDK二进制兼容。须核对标准异常入口与SMP启动
   生命周期，再落到适当arch扩展点或可维护NuttX补丁；不能直接删除现有保护。
   本轮未证明这些包装导致死机，也未确定无包装的等价替代方案。
3. **层级检查覆盖不足**：`tools/bk7258/_lib/layers.py:24`仅纳入C/C++后缀，
   `:116`固定扫描boards/bk7258、app/bk7258、chips/bk7258。
   因此无法发现第1项构建依赖问题，且app/dolphin不在产品扫描范围。
   建议补构建依赖及实际应用目录覆盖；保持精确规则，不用宽泛关键词制造误报。
4. **V4L2格式兼容放错抽象层（当前三板未启用）**：
   `chips/bk7258/ap/bk7258_dvp.c:414`把H264别名为JPEG_WITH_SUBIMG，
   `:525`包装video_register并替换vops。这是NuttX capture能力补偿。
   若重新启用，应优先补标准capture H264能力及回归，再去除别名包装。
   当前六个ELF/link flags均没有video_register包装，不将它作为当前故障原因。

## 未发现板层直接SDK调用

本轮板级C/H检查未发现直接Beken SDK头、类型或调用。GT1151通过NuttX gt9xx、
i2c_bitbang和公开chip pinmux；SDIO通过chip lower half及NuttX mmcsd绑定。
这些板级引脚、地址、复位与实例绑定应保留在board。不能把bk7258_*公开芯片
接口误判成bk_* SDK接口。此结论不等于所有板级算法都已穷尽审查。

## wrapper是否可以使用

普通chip接口函数封装SDK调用是正确边界；链接器--wrap只应作为有依据的兼容手段。
例如AP电源/时钟SDK调用经chip PM客户端转发到CP，维护单一共享寄存器所有者，
有保留理由。Wi-Fi初始化malloc清零包装已有线程和初始化窗口限制
（common/bk7258_os_adapt.c:2734），但应通过分配故障/并发证据验证，不能称为
原生NuttX能力，也不能不加区分地推广为通用包装模板。

GNU ld仅改写未定义符号引用；同编译单元已解析引用可能绕过--wrap。
因此链接命令有标志、ELF有函数，仍不能证明所有目标调用都被截获。需要进一步
检查归档重定位/最终反汇编，并覆盖错误返回、所有权、并发及升级兼容。
参考：[GNU ld官方说明](https://sourceware.org/binutils/docs-2.39/ld.html)。

## 验证和证据

- `python3 tools/bk7258/bk7258.py verify layers`：本轮实际运行通过；覆盖限制见第3项。
- 从三板app__openvela_ap的实际build.ninja提取--wrap，再用ARM nm读取六个ELF。
- 工作区 `out/bk7258-layer-review-20260910/link-options.json`和
  `elf-wrappers.json`保存准确构建路径、标志和实际保留符号。
- 只做审查与文档记录；未修改运行实现、SDK或NuttX，未提交推送。

## 整改实施检查点（2026-09-10）

**状态：自动化整改已落实，真实板级回归阻塞，整体未完成。** 上文保留初始审查，
其“未修改”“覆盖不足”等结论描述整改前状态，以下为本轮实际结果。
未提交、推送、刷写，也未改 SDK / NuttX 运行源码。保护了原有产品配置和未提交成果。

a. 基线：团队仓库 HEAD `32d36292bc4c6c5ce9f55d2edc14c4d2ef18667e`；
NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`；GNU Arm 10.3-2021.10。
实际六配置均 `-Os`、`CONFIG_LTO_NONE=y`、非 HIPRI、flat ARMv8M；CP 非 SMP，AP 双核。
证据根目录（相对 OpenVela 工作区）为 `out/chip-board-remediation-20260910/`。
`baseline/baseline.json`、`workspace.diff/status` 和逐板/核快照保存修改前版本、
工具链、命令、配置、ELF/bin/map/ninja、符号表、反汇编及哈希，不以最后构建覆盖基线。

### 构建归属和最终入口

| 依赖/符号 | 声明和实现责任 | 消费方及最终入口 |
| --- | --- | --- |
| SDK profile 库闭包、私有 libs 路径 | chip CMakeLists.txt / Link.defs | CMake nuttx_add_extra_library；Make EXTRA_LIBS，送最终 NuttX 链接 |
| bk_flash_partition_* wrap | chip CMakeLists.txt / Link.defs；common/bk7258_sdk_partition.c 实现 | CP SDK 未定义引用，经最终链接 wrap 到 chip 实现 |
| arm_doirq / nxsched_resume_scheduler | chip 内核兼容构建声明；CP/AP vectors 实现 | NuttX 异常入口和调度恢复调用，见六 ELF 矩阵 |
| nx_bringup | chip 内核兼容构建声明；ap/bk7258_ap_smp.c 实现 | AP nx_start；CP 保持原生调用 |
| 板型、布局、外设实例 | board 配置、初始化和生成布局输入 | 公开 chip 接口；Make 仅 include chip/Link.defs 公开入口 |

boards/common 不再枚举 SDK 库或持有分区 wrap 参数；Make/CMake 均已调整。
Make 公开入口用于把 chip 链接闭包传到顶层链接，不把 SDK 类型或头文件透传到 board。
第一阶段仅收回构建归属，未同时改变内核控制流。

### 三个内核 wrapper 的逐项处置

三项均选择 **C：保留为显式内核兼容机制**，不是普通 SDK 封装，未声称可以安全删除。
原始引入可追溯至 `eaef241`；当前源码足以确认兼容行为和调用时序，但不能单凭注释
证明历史异常返回为零的根因。本轮没有重新制造该历史故障，也没有进行删除后的实板试验。

- `arm_doirq`：异常入口需在 dispatcher 前约束 BASEPRI，再在返回时处理选中异常帧。
  当前 arm_doirq 会在返回前清理 TCB 的 xcp.regs；chip 保留对应恢复指针和失败停机路径。
  IRQ handler/ack 扩展点不能直接覆盖 dispatcher 前后这一语义；不将其迁到 board。
- `nxsched_resume_scheduler`：在 xcp.regs 清理前捕获选中任务帧，修正线程返回的
  CONTROL.SPSEL，以及实际扩展 FPU 帧需要的 FPCA；AP 按 CPU 保存状态。
  它和上一项存在明确配合关系，不能当独立计时 hook 随意替换。
  当前版本没有证实等价的正式帧交接 hook，保持每条目标边仅一次原实现调用。
- `nx_bringup`：AP 在原生 bringup 后释放 CPU2 idle 并等待其调度解锁。
  当前 nx_start 在 nx_bringup 返回后再 sched_unlock；up_cpu_start 属于更早的
  SMP 启动阶段，board_late_initialize 又属于不同初始化任务阶段，不能直接等价替换。
  CP 不需要此兼容，保持未包装。未修改 CPU2 握手、异常或调度控制流。

维护约束已落实为代码：`chips/bk7258/kernel_compat.json` 固定 NuttX 版本和九个
相关源文件哈希；统一 builder 同时检查 canonical 源码及实际复制构建树，拒绝未审查漂移。
`common/bk7258_kernel_compat.h` 和配置门禁拒绝活动 wrapper 未审查的 LTO/HIPRI 模式。
CMake 分开内核/SDK wrapper 声明。升级 NuttX、优化方式或相关源文件时需重新审查和
更新契约，并重跑六 ELF 与实板回归；不得自动接受新哈希。
这些是已实施的维护措施，**physical_validation 仍明确 pending**。

### 本轮验证及复现

对 aidk_ai_toy、t5_board、t5ai_core 分别运行（每次产生 CP/AP）：

```sh
python3 tools/bk7258/bk7258.py build --board BOARD --boot direct \
  --workspace /home/lijian/project/open-vela/out/bk7258-plan-validation --jobs 8 --clean
```

CP 实际配置为各板 `configs/app`，AP 为 `configs/openvela_ap`，未用其他配置替代。
`stage1/build-results.json` 和 `final/build-results.json` 各记录三次成功 clean 构建。
第一阶段及最终六份 nuttx.bin 均与保存基线字节一致，配置一致；
`final/link-equivalence.json` 确认库顺序和 wrap 集合一致。最终产物和哈希分别存于
`final/<board>/<role>/`、`final/artifacts.json`。这是构建等价证据，不是实板验收。

`final/elf-audit-with-relocations/matrix.json` 对六个真实 ELF 检查：

| 构建组合 | arm_doirq | nxsched_resume_scheduler | nx_bringup |
| --- | --- | --- | --- |
| AIDK CP / T5-Board CP / T5AI-Core CP | exception_common 经 wrapper 到原实现 | arm_doirq 经 wrapper 到原实现 | nx_start 直接原实现，预期不包装 |
| AIDK AP / T5-Board AP / T5AI-Core AP | exception_common 经 wrapper 到原实现 | arm_doirq 经 wrapper 到原实现 | nx_start 经 wrapper 到原实现 |

检查目标地址、恰好一次调用边、直接绕过和递归；六组合均通过。对应对象文件与
readelf 重定位保存在审计目录，确认链接前目标未定义引用及 wrapper 的 __real_* 引用，
最终反汇编确认其解析结果。最终 ELF 无保留重定位，因此另存真实构建对象；基线未保存
对象的情况明确记为 not-saved。未断言 sched_switchcontext 存在同单元绕过。
间接跳转的全部动态目的地、异常上下文正确性仍需要运行证据，不能由静态审计证明。

复现 ELF 检查：

```sh
python3 tests/host/bk7258/test_kernel_wrapper_elf.py \
  --baseline-json ../out/chip-board-remediation-20260910/baseline/baseline.json \
  --source current --output ../out/chip-board-remediation-20260910/final/elf-audit-with-relocations \
  --tool-prefix "$PWD/prebuilt/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-"
python3 tools/bk7258/bk7258.py verify layers
python3 tests/host/bk7258/test_bk7258_layers.py
python3 -m unittest discover -s tests/host/bk7258 -p test_kernel_compat.py
python3 tests/host/bk7258/test_kernel_wrapper_elf.py --self-test
```

分层 gate PASS（sources=515、kconfig=249、原有 legacy-exceptions=2）。
新增 board/app 构建文件、所有 app（包含 dolphin）、SDK 路径/库/符号、wrap/real 检测；
保留公开 chip/NuttX 合法接口正例，越界源码/构建负例及注释/引用边界测试。
未新增整目录豁免。检查器脚本 PASS；内核契约 4 项、workspace 3 项、package 13 项、
ELF parser 2 项通过，日志位于 final。unittest discover 对函数式 layers 文件收集为零，
因此实际验收采用其显式 main 脚本，未把零测试结果记为通过证据。

### 真实设备与未完成项

COM8 经 PnP 确认为 CH340；Win32_SerialPort 漏报该端口，已用实际 UART 纠正。
COM8 当前 AIDK 仍为 `18.6.347+411`，未安装本轮镜像。只读 status 显示 AP READY、
CPU2 SCHEDULER_ONLINE，连续 AP SysTick、CPU2 heartbeat/IPI 递增；
证据为 `hardware/aidk-current-runtime.json/log`。CPU2 SysTick 为零符合当前主核路由，
不作为故障判定。以上只证明旧设备当前存活，不计入本轮三板验收。

可执行新回归：`tests/pytest/test_bk7258/test_kernel_compat_hil.py`，通过编译及收集
（1 项），**尚未在新固件执行**。通过官方真实 UART fixture 验证板名、十轮 AP/CPU2
进展和 CP 调度，不注入，不擦写；需先建立实际安装固件与 ELF 的绑定证据。
Linux UART 可用后，在工作区 tests/scripts 执行：

```sh
BK7258_KERNEL_HIL=1 BK7258_KERNEL_HIL_ELF=实际CP_ELF \
pytest script/test_bk7258/test_kernel_compat_hil.py \
  -D 实际UART0 -B 实际板名 -U cp -P 实际配置目录 \
  -L 新日志目录 -F /data -R target -M serial --junitxml=新日志目录/kernel.xml
```

当前阻塞：

1. 仅 AIDK 身份/COM8 已确认，T5-Board、T5AI-Core 未确认实际连接和下载/恢复路由。
2. AIDK 当前整机恢复备份尚缺两份一致读回；此前不同方式自动读回均失败。
   需要在 loader 读回等待窗口提供物理 RESET/CEN，不能用 CH340 DTR/RTS 假装复位。
   旧 HTTPS OTA 两次 EIO 证据仍未闭合，不盲目重复作为本轮部署路径。
3. 本轮产物是 direct 开发镜像，尚未形成并安装对应安全发布包。未核实恢复路径前
   不把这些 ELF/bin 当作可直接覆盖的 8MiB 整机包。
4. 新固件启动/中断/调度/多核，以及触摸、SDIO 真实操作回归均未验收。

恢复顺序：确认三板与路由及当前恢复备份 → 生成/核对对应部署包及安装证据 →
逐板执行上述内核回归与触摸/SDIO实际操作，保存版本/步骤/串口证据 → 更新验收矩阵。
不需要重新做已通过且未改变的构建归属迁移。goal 工具仍被旧未完成目标占用；
本报告作为恢复入口，不虚报旧目标或本轮目标完成。

### 用户授权下载后的当前预检

已核对 HIL AIDK profile 和 AUTOMATION.md：自动 download 软件复位可用，
但当前只有 direct CP/AP 产物，尚缺本机完整读回以构造设备绑定全量镜像。
本次 COM8 实测仍411/confirmed、faults/recoveries=0/0、AP READY、CPU2 ONLINE；
没有发送复位或 Flash 写入。新证据 `out/chip-board-remediation-20260910/download-preflight/`。
需要协调 loader read 的物理 RESET/CEN 窗口后取得当前备份；不是板子卡死，
也不是再次申请下载授权。未执行重复自动 read 或用历史整机包替代当前产物。


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


## 418 开发路径首轮审查（仅审查，2026-09-10）

范围仅为 AIDK/COM8 已安装的 18.6.354+418；不扩大到另两板、其他产品或全仓清理。
本节合并调用关系、产物规范草案、规则分级、脚本候选和资源影响五项成果。
本轮只修改本文，不构建、不签名、不下载、不访问串口、不删除文件、不修改规则或密钥。
证据中的实板状态来自418下载后的既有日志，不是本轮重新采集。
CodeGraph 返回 auto-sync DISABLED，并混入 .repo/manifests 的旧源码；本审查改用
当前团队源码定向读取，不刷新索引或据旧图认定调用关系。

### 1. 实际路径与首要阻塞

证据根目录 E 为 OpenVela 工作区 `out/chip-board-remediation-20260910/download418/`。

| 实际步骤 | 当前权威实现/调用 | 418证据及判断 |
| --- | --- | --- |
| 板型与配置 | boards/bk7258/aidk_ai_toy/openvela.conf → app / openvela_ap；bk7258.py build → _lib/build.py:1508 | E/build.log；实际 mcuboot、--clean、jobs8、rollback-floor418；不是默认增量测量 |
| 编译 | _lib/build.py:815 _role_build → 官方 build.sh/CMake；:1574起依次CP/AP，:1605起BL2/BL1 | E/release/evidence/build-manifest.json 绑定ELF、配置、SDK、工具链及公钥指纹 |
| 签名和全量包 | bk7258.py:548 _release → trust.signed_release → package/materialize | E/release.log、verify-package.log、verify-trust.log、release/release.json；不是调用HIL生成包 |
| 设备基底 | _release:592、product.load_base_evidence；:628提取persistent_data；:728物化全量 | E/authorization.json 明确授权跳过当前读回；沿用历史同一设备accepted base，不能推广为任意同板型基底 |
| 下载 | HIL bk7258_hil_download.py run → prepare → BK Loader | E/hil-run/command.json、result.json；COM8/8MiB/460800/reset reboot/fast-link1；无RTS/DTR |
| 启动/运行 | 一次性Windows串口调用，复用已有status命令和计数断言 | E/boot-status.log、kernel-live.json/log；418 confirmed、AP/CPU2正常；未执行Linux pytest fixture |

已有日志记录CP/AP分别“18 seconds”“23 seconds”，只代表官方子构建计时。
HIL started/ended为04:39:29.491469→04:42:00.504899 UTC，约151秒，包含握手和擦写。
未记录统一总耗时，不能计算完整耗时节省，也不为补统计重跑。

**已确认的首要阻塞是签名可持续性，而非串口下载能力。**
E/acceptance.json记录两把本次临时私钥删除；E/release/release.json记录BL1指纹
`266246b1015b81b4a0a7253cd2e981cf52bba5b19a5ad739f16df2b59963f419`，
应用/MCUboot指纹 `7d146f0a9929dbf0c250f7f8fce4427f20df8113ca1f417ef2b3e2f8de36aaf2`。
这些是包和对应构建证据，结合下载哈希和418启动建立关联，不冒充硬件信任锚读回。

信任职责已确认（_lib/trust.py:218、:550、:648）：

- BL1密钥签署描述BL2的manifest_a/b；其字段包括地址、安全计数及BL2摘要。
  bootloader/boot_bl1_manifest.c:172起核对公钥哈希并验签。
- MCUboot应用密钥共同签CP/AP，另签更新catalog；生成的BL2表只有1个key，
  见实际releases/mcuboot/trust/bk7258_mcuboot_public_key.c:24-27。
- AP内嵌catalog公钥来自同一MCUboot公钥（trust.py:253起）。所以仅保留其他旧版
  私钥，不能推定可以签发418受信的新镜像或catalog。
- signed_ota_pair只签CP/AP（trust.py:648），不要求BL1私钥；BL1私钥丢失本身
  不能单独推导为应用OTA失效。本次另有应用私钥已删除的明确事实。

**推断**：若无相同应用签名身份的其他合法副本/签名服务，现有签发路径无法生成418
受信的新OTA。**待验证**：是否有用户管理的合法签名备份/服务；本轮未全盘搜索秘密，
也未验证OTP、调试锁或ROM生命周期状态，不能断言不存在任何恢复或迁移路径。
优先保持418运行和其现有信任关系；先判定是否需要迁移及合法条件，不预定迁移。

### 2. 产物规范：已有保护与最小缺口

已确认：外部全量BIN为 `operator-aidk_ai_toy-v18.6.354+418.bin`；包为
`firmware-aidk_ai_toy-v18.6.354+418-full.bkpack`。release.json已绑定最终BIN及包的
哈希/大小、板型、布局、签名指纹和基底证据；包manifest包含images/erases/security。
不是拿签名前哈希作为下载文件哈希。

- **保留**：bk7258.py:312拒绝已存在release目录，:674起复核签名前后handoff，
  :710起校验包；这些有实际保护作用，不能作为“重复检查”直接删除。
- **已确认缺口**：_write_build_manifest（build.py:1134起）没有团队完整源码提交、
  dirty摘要和产品/实际配置名称字段；角色build_identity不能替代源码身份。
  本次baseline/workspace.diff也早于后续整改，不能单独重建418全部源码状态。
- **最小建议**：扩展已有build manifest来源字段，release引用其摘要；给现有命名
  增加产品/实际profile/独立产物编号时提供兼容迁移，不新建平行package_info文件。
  首先解决可追溯缺口，不要求在下一次上板前完成全部命名美化。
- **已确认计数耦合**：_release_generation:296读取版本+GENERATION；_release:578起
  使full generation等于编译floor，:617/652将其传作安全计数。三者目前没有独立CLI字段。
  最小建议先明确这个现状，后续核对运行期版本/计数比较再决定拆分，不能只改参数名。
- **数据分类**：418 full包实际包含 `payloads/persistent_data.bin`，release绑定device_id。
  它是设备专属材料，不能与公共OTA混淆或直接公开；未读取或展示payload内容。
- **清单/执行边界**：设备专属full包保留其绑定是必要事实；COM、授权、选择基底和
  执行结果继续放HIL/执行记录。验收引用摘要；418通过不触发重签名或重打包。

### 3. 放行、恢复和规则最小调整建议（未实施）

| 当前证据 | 分类与最小调整 |
| --- | --- |
| AGENTS.md:11-13要求每次full新建密钥、clean并删除临时私钥 | 首要规则冲突。未来改为显式选择既定签名身份；创建、轮换、删除独立授权。不是本轮已授权保存私钥 |
| build.py:1545、:832/864仅clean时清目录；默认依次进入CP/AP与启动构建 | 保留增量默认；“进入构建”不等于每次全部重编。boot构建是否无条件重编、public source原子重写是否引起多余编译，尚未测量，不能声称已节省 |
| AUTOMATION.md整机同设备基底要求与实际418历史基底授权 | 按数据影响和有效恢复材料决定前置；不要求每次应用更新当前整机备份，但身份/校准未知时阻止相关覆盖 |
| HIL prepare重核hash/size/profile，之后Popen把文件路径交给loader | 保留边界复核；尚无原子文件句柄交接证据，存在校验到打开之间替换窗口的推断风险。将封存产物/独占执行纳入最小方案，不称TOCTOU已闭合 |
| HIL有独立8MiB/profile限制，项目布局也定义容量 | 有限重复边界校验可保留；后续由项目输出允许计划、HIL校验未扩大，不能单纯删除HIL尺寸保护 |
| 当前signed_ota_pair已是apps-only；deploy.py负责USB CDC，HIL负责Loader | 保留两种传输职责。418新OTA尚无可用签名身份和实际升级证据，不能把可选路径写成已可用 |

立即放行条件仍是目标、最终文件、擦写范围、组件及信任匹配；未知不能按“未证明危险”
直接放行。应用升级还需核对固件启动迁移，不只看CP/AP写入地址。

恢复方面：现存418已签包可以保留作恢复候选，HIL成功证明当时能软件接管并写入；
不能据此证明硬故障时ROM入口、所有锁状态或更高计数设备也能恢复。
本轮不进行断电、回滚、恢复演练。独立演练后才确认对应恢复能力，不将演练变为每次前置。

### 4. 脚本去留候选与测试开销

| 对象 | 已有用途/证据 | 本轮结论及下一步 |
| --- | --- | --- |
| bk7258.py及_lib/build/trust/package/product/deploy模块 | 418日志及当前入口直接消费 | 保留；不是因入口文件长就拆，也不是为了唯一入口合成巨型文件 |
| HIL下载技能脚本 | 418实际调用；处理Windows路径、复位、退出码/marker | 保留适配价值，未来只收束重复业务策略，不合并为项目私有下载器 |
| deploy_console.py | deploy.py调用；USB OTA后确认版本 | 保留；本次用一次性PowerShell替代属于环境接入差异。其Serial构造未显式预设DTR/RTS，后续复用前须对照AIDK约束核实，未认定导致过故障 |
| 一次性418 PowerShell构建/串口编排 | 原会话调用及E日志，未落仓库长期入口 | 后续优先已有Windows适配/命令，不把本次片段再做成fix脚本或新框架 |
| test_kernel_wrapper_elf.py / test_kernel_compat.py | 418 signed-elf-audit与六ELF证据，明确内核兼容风险 | 保留。与本轮wrapper目标相关，不能列为无关测试；普通产品改动不自动扩展该矩阵 |
| test_kernel_compat_hil.py | 官方pytest子目录，文档引用；418未实际运行该fixture | 保留已有入口候选，下一步解决环境接入而非复制fixture；不再新增一份同义计数测试 |
| 三个未跟踪test_sdio_*probe/sdk_busy文件 | 与历史SDIO诊断相关，当前418编译/签名链不消费 | 仅列待审清理候选；CLI执行/pytest自动发现和外部调用未查全，不批准删除 |

此次只查当前入口及其直接消费者，未全面审查CI、定时任务、其他仓库和外部自动化；
所以**没有任何脚本被认定为已可安全删除**。外围去留审查不阻塞签名和日常更新问题。
新增测试今后需具体行为/不足/行动影响三个理由；418已有主机/实板证据可复用，
不通过另建测试来证明这份审查文档正确。本轮没有执行测试。

### 5. 资源影响与待决事项

本轮只对相关目录使用du（占用近似值，不等于可释放量）：
E约44MiB；同级baseline约202MiB、stage1约185MiB、final约238MiB。

- E是当前设备418签名产物和回归证据，保留；同设备基底及其绑定证据保留且不公开。
- baseline/stage1/final包含三个阶段的六ELF/map等证据。后续可评估去重，但不是现在删除，
  必须保留各阶段摘要及对应可取回内容。单纯bin一致不代表所有ELF/调试信息可丢弃。
- 构建缓存和历史角色目录没有本轮依赖/占用清单，未纳入删除决定；不扫描TTS/声音数据。
- clean管理构建树（build.py:499、1545）；未发现该路径删除外部长期私钥。
  418私钥删除来自临时操作和AGENTS要求，不能错误归因于clean实现。

待决事项按优先级：

1. **先确定418现有签名身份是否仍可合法调用。** 缺的是哪个身份已明确；是否存在
   外部备份/服务未确认。如存在优先继续使用；如不存在，再判断迁移是否必要，核实
   当前设备信任/防回滚/恢复入口后提出独立操作，不自动生成新钥或再次刷板。
2. 如需持续保存开发密钥，由用户确定授权位置、访问主体、备份与删除责任；本轮无新增授权。
3. 下一次应用更新使用USB CDC还是其他已验证传输，需要在签名能力解决后做最小真实验证；
   418的Loader成功不替代USB OTA成功。历史HTTPS EIO不因此宣布已修复。
4. 全量更新若需要历史数据回退，应逐区域说明可恢复/回退/不可恢复；418此次覆盖授权
   不自动延续为以后任意设备、任意数据的清空授权。
5. 命名完善、脚本退役和目录去重后置。后续实现先解除1的真实阻塞，再完成一次相关
   增量构建→既定签名→必要更新→确认，不能先展开外围清理或大型测试体系。

审查结论：418当前运行和HIL路径有既有证据；规则导致的签名身份不可持续是优先问题。
暂不认定必须迁移、存在可用替代签名路径、可安全删除任何脚本，或完整恢复能力已验收。


## 418 持续签名能力与计数专项核对（只读，2026-09-10）

上一轮审查已收口。本节只核对418已安装产物链、已知签名位置和计数；不再扩大清理。
本轮没有构建、测试、下载、串口/OTP访问、密钥生成/导出/签名、文件删除或规则修改。
使用已有E=工作区out/chip-board-remediation-20260910/download418/，只补本文。

### 已安装产物证据链

重新只读计算E/release/release.json所指全量BIN及bkpack的SHA256，均匹配封存记录。
HIL command.json/result.json绑定同一BIN哈希、COM8和实际8MiB写入；boot-status.log、
kernel-live.json记录418/confirmed。没有新的Flash读回，结论是下载/启动/产物一致性，
不是本轮硬件信任锚直接测量。

从E/release/evidence/build-manifest.json解析原CP/AP/BL1/BL2 ELF引用，再核对其哈希：
CP 6de26e88fb64807175ff70940428fb48868d8041d92c4234b4bab319aee8560f；
AP 53c8ab3479b20a2faba5397e39c471a30e2ff0fe5d389f6f7686dae3fdd49fef；
BL1 af7c65d3464774b58a8646a63cffe0819aaf90e258eea7767e2f875468a76ddd；
BL2 c220d18bd9029355370167da98ab7b3f26334bf6ab25b4b34abb1a0d7e831d32。
四份实际ELF均匹配；未用当前defconfig替代这些产物。

使用已有pyelftools只在内存读取公开符号，不导出密钥文件：

| 受信对象 | 418实际产物证据 | 公钥身份（SHA256 of DER） |
| --- | --- | --- |
| BL1验证BL2的manifest_a/b | BL1 ELF root_public_key的64字节XY与包public DER尾64字节相同；compiled root hash与SHA256(04+XY)相同 | 266246b1015b81b4a0a7253cd2e981cf52bba5b19a5ad739f16df2b59963f419 |
| BL2验证CP/AP | BL2 ELF ecdsa_pub_key直接计算DER摘要，bootutil_key_cnt实际值1 | 7d146f0a9929dbf0c250f7f8fce4427f20df8113ca1f417ef2b3e2f8de36aaf2 |
| AP验证更新catalog | AP ELF bk7258_ota_catalog_public_key_der实际摘要 | 与上述应用身份相同 |

包manifest.security及已有verify-trust.log确认CP/AP共同使用应用身份。
release入口签catalog也使用mcuboot_key。418是full包（full-update.json），不是一个
已发布418 OTA catalog；这里区分“已签全量包元数据”和“AP接受后续OTA catalog的公钥”。
原始XY、04+XY及DER摘要编码不同，不能把这些哈希直接互相比较。

### 限定位置核对结果

| 位置引用 | 本轮读取/检查范围 | 状态 |
| --- | --- | --- |
| /tmp/bk7258-download418-state.json | 仅现有字段/引用/删除标志，不含私钥内容 | ephemeral_private_keys_removed=true；keys字段已被删除 |
| /tmp/bk7258-full418-* | 仅原生成操作的专用前缀目录枚举；不递归搜索其他临时目录或凭据 | 当前匹配目录0。原操作使用bl1.pem、mcuboot.pem；确切随机后缀未保留在当前state/build.log，不能伪造精确路径 |
| E/acceptance.json | 原删除记录 | ephemeral_private_keys_removed=true，与操作记录一致 |
| E/bl1-public.pem、E/mcuboot-public.pem、封存包内security公钥 | 已知公开验证材料引用；未将其当私钥候选 | 可验证身份，不具备签发能力 |
| AIDK openvela.conf、AUTOMATION.md:58-61/113-118、CLI签名参数 | 仅项目明确配置/调用接口 | 配置未给出具体vault/HSM服务或备份URI；文档仅有secret manager原则和变量占位；CLI接收显式本地私钥路径，无已配置服务候选 |

**限定范围内未找到可用的匹配签名路径。** 未扫描其他/tmp密钥目录、家庭目录、
其他项目、浏览器或云凭据，未打开任何私钥。没有发现“已明确配置但访问失败”的
具体外部端点；不能把文档中的vault/HSM字样写成真实不可访问服务。
外部合法副本是否存在仍未知，结论不是“全世界没有备份”。

最小下一步首先是补充两个公钥身份中任何现存合法签名路径的明确引用/访问范围；
若仅找回应用身份，可保留现有BL1/BL2直接准备应用OTA，不必找回BL1私钥或轮换根。
只有BL1身份可用时，才评估签署兼容新应用验签身份的BL2，但现有apps-only入口不能
据此升级BL2；还需明确批准的启动组件更新路径，不默认成立。

若确认不存在对应签名能力，候选是**最小受信组件调整**：依具体受保护根策略，
评估可否保留硬件/BL1根，仅更新必要BL2验签及AP catalog身份；若现有根无法授权该
变更，再核实ROM下载权限/生命周期/恢复路径是否允许修改软件信任配置。
不预定整片重刷、不生成两套新根；现有HIL AIDK profile只支持8MiB全量传输，
尚不能宣称已支持有界启动组件写入。签名变更逻辑范围与下载器实际擦写范围需分别核实。
418仍可运行，原封存包保持不变。无匹配路径不等于必须现在迁移。

### +GENERATION、安全计数与有效下限

已确认发布代码映射（bk7258.py:296、578-590、617-629、648-653）：
version尾部+G → generation；full要求G==build_manifest.rollback_floor；
OTA要求G>=该floor；CP/AP security_counter=G，full BL1 manifest计数也=G。
这属于项目入口策略，不是MCUboot格式必然要求。

418数字证据来自实际包，而非版本名猜测：封存manifest.security.images两份均为418，
bl1_security_counter为418；封存build manifest rollback_floor为418；串口实际报告
active pair counter418。以上分别是镜像计数、编译输入和运行镜像元数据，均不是OTP读数。

更直接的已绑定ELF证据：BL2 boot_nv_security_counter_get在0x28020ba0调用
bk7258_bl2_security_counter_readonly，0x28020ba4比较#418，较小时装载#418。
因此418的BL2实际有效下限实现为max(OTP位计数,418)，image_id不区分CP/AP。
相关源码chips/bk7258/bootloader/bl2/bk7258_bl2_security_cnt.c:58-94；
MCUBOOT_HW_ROLLBACK_PROT与VALIDATE_PRIMARY_SLOT在bl2/include/mcuboot_config/启用。
该后端update只比较，不写OTP，不能声称“此次刷写把OTP计数烧到418”。

BL1签名路径也启用manifest检查；配套生成配置MIN_IMAGE_VERSION=418、OTP_ROOT_POLICY=1。
boot_bl1_policy.c:66-78取软件最低值与只读OTP值的较大值；boot_bl1_manifest.c:59和148
读取/执行检查。其软件下限是生成产物及现行对应源码证据；本轮未另外导出BL1全部
控制流，验证强度与上述BL2反汇编直接证据区分。

**有效范围结论**：存在已证实的418软件下限；OTP原值没有既有独立测量。
成功启动计数418镜像与有效下限不高于418相容，但不作为OTP位图实测或永久状态保证。

此外common/bk7258_ota_rpmsg.c:1403-1412的staged reboot要求新security_counter严格
大于active counter，且image version也严格更高；AP catalog拒绝零计数。
这是当前源码放行条件，未用较低/相同计数在418上试验，也未宣称所有传输只受这一处约束。
所以不能直接套“MCUboot允许独立计数”来让当前项目复用同计数OTA。

独立定义的最小建议：在现有release/manifest字段中分别定义软件版本、不可变产物编号、
应用安全计数、BL1/BL2软件floor；保留当前数值和策略作为兼容基线，不重置、不自动降值。
以后如要允许同安全计数的软件更新，需同时审查catalog、CP/AP配对、staging/reboot及
bootloader比较条件；仅新增--security-counter参数不能闭合。此项不阻塞先查签名身份。

本轮结论：已明确418两种签名职责及应用/catalog共享身份；已核对位置无可用签名路径，
没有具体不可访问服务；计数映射与418软件下限有产物证据，OTP实值及迁移权限仍待验证。
停止扩搜。下一动作仅为获得现存匹配签名能力的明确引用；无此能力再形成最小调整操作，
不能从本报告直接推导出授权轮换、全量下载或计数更改。


## 最小实施决策：恢复持续签名，然后解耦计数（待授权）

工作前提：限定范围内无418匹配签名能力，旧身份查找已收口；有明确新引用才重开。
本节只给实施决策，不生成/导出/使用密钥，不读设备、不改代码规则、不构建或刷写。

### 推荐路径及成立条件

**推荐：在确认设备允许的情况下，使用既有ROM/有线恢复能力，一次建立可持续保存的
开发签名身份；随后回到只更新CP/AP的日常路径。** 这不是让418 OTA信任陌生公钥。
先授权最小只读核查；成立才授权具体组件、数据影响和传输范围。若硬件固定根禁止替换，
此路径不成立，应保持418运行，转向拥有该固定根授权的签名/维修通道，不能绕过验签。

成立依据必须区分：

1. 418 HIL成功只证明当时ROM窗口及写入可用，不证明ROM永远接受任意BL1。
   ROM验BL1的策略/下载锁必须以当前状态和适用启动实现核对。
2. 已核对的BL1实现（boot_bl1_manifest.c:78-117）仅在CM且OTP根哈希全零时使用
   编译软件根；非零则必须匹配OTP根。新公钥及其自签包仅证明内部一致性，不能授权
   替换OTP根或证明ROM接受新的BL1。
3. 软件根可合法替换且ROM允许新BL1时，新BL1按保持不变的验签逻辑接受新签名的BL2
   manifest，BL2再接受新应用签名的CP/AP，AP按新应用身份验证catalog。
   验签算法/检查保持，不通过禁用校验完成换链。

### 最小逻辑变化，不等于必须整片擦写

| 对象 | 推荐处理 | 原因/接受依据 |
| --- | --- | --- |
| ROM、OTP/eFuse、硬件根及生命周期 | 不变、不编程 | 作为操作边界；不把轮换软件身份变成硬件烧录 |
| BL1 | 在上述开发状态获证时更新软件根配置，保留代码/算法/OTP策略 | 旧BL1根无法验新BL2 manifest；此层授权来自ROM/设备开发策略和用户恢复授权，而非新钥自签 |
| BL2及manifest_a/b | 更新应用验签公钥，重签对应BL2描述；保留MCUboot版本、分区和验签策略 | 改BL2会改变摘要，旧manifest不能继续使用；由获准部署的新BL1所信任身份签署 |
| AP | 更新catalog公钥；重新签名 | 不同步AP则后续catalog仍被旧公钥拒绝；由新BL2验签 |
| CP | 尽可能复用已核验原始负载；重新签名并生成匹配元数据 | 现有包要求CP/AP共同身份、版本、计数及pair匹配；不额外改产品行为 |
| pair、签名镜像头/TLV、更新catalog | 按实际负载重新生成 | 内容/身份变化，不能复用旧签名和摘要 |
| 分区布局、SDK、驱动、产品设置、SD NAND | 保留 | 不属于恢复签名能力必需变化；若打包不能保留则先列出具体影响，不自动扩大 |

若能依法使用旧BL1签名身份，则可保留BL1，仅更换BL2应用身份及必要组件；此为有明确
新引用才启用的更小分支，不再等待其出现作为唯一前置。

传输决策：先核对“必要组件范围+工具实际擦除范围”。现有AIDK HIL只接受8MiB single；
不能把它称为有界更新，也不能修改profile假装具备支持。若已有BK Loader能力和设备策略
允许经核验的有界写入，可另行提出最小适配；当前并未证明这条分支可用。
如采用现成HIL整机传输，是为复用已验证运输路径作出的显式选择，不是密码学要求。
那时需单独批准8MiB物理写入，并使用可信同设备数据物化，禁止顺带回退/清空配置。
418之前历史基底覆盖授权不自动延续到本次操作。

### 只读核查清单（未自动执行）

仅需要以下事实决定上述分支，不再次扫描签名目录：

- COM8只读版本、confirmed/manager状态及稳定设备标识：确认仍是418与同一台设备。
- OTP影子中的根哈希、LCS、BL1/BL2计数域：确认软件根路径是否允许，获得实际floor输入。
  只读已核实无副作用的影子接口；不初始化/编程OTP，不以当前defconfig替代状态。
  BL1计数为低位连续1解码，BL2为位总数，不能用同一算法或把418当OTP读数。
- 现有无写入查询能够提供的ROM安全启动/下载锁状态：确认ROM接受BL1的依据及恢复入口。
  若没有可信无副作用查询，不通过擦写试探；明确留下该条件缺口。
- 与本次擦写相交的身份/校准/配置区域的读取能力及恢复材料绑定：决定能否保持设备数据。
  仅在选择有线变更后才需要读取受影响数据，不预设每次应用OTA都要整片备份。

以上是拟授权范围；本轮没有打开串口、读取寄存器或导出设备数据。

### 密钥权限与设备操作分开授权

拟使用两个持续开发签名角色：BL1→BL2 manifest；应用→CP/AP+catalog。
不按AP/CP文件数增加密钥层，不用于生产或自动写入硬件根。
建议私钥目录 `/home/lijian/.local/share/bk7258-signing/aidk-development/`（目前仅提案，
未创建、未探查内容），目录0700、私钥0600、仅当前用户；公钥/指纹可进入构建清单。
需明确批准：创建并持久保存两个身份、后续按已授权版本签名使用；普通clean及失败清理
不得删除。是否允许加密备份及其具体位置另行授权，不为此建立新服务/管理系统。

设备操作授权分两步：先只读上述最小字段；路径成立后提交确定的组件清单、实际擦写
范围、包摘要、数据保留来源和恢复条件，再批准一次部署。若选择现成HIL8MiB路径，
授权明确写8MiB，不能只写“更新密钥”。本决策不视作已获得这些执行权限。

### 与版本/安全计数解耦的衔接

第一阶段仅恢复签名能力：保持现有版本/计数比较和签名角色数，不同时放宽OTA规则。
候选版本/计数需满足当时active、OTP、编译floor及各阶段规则；数值在只读核查后确定，
不是默认419，更不把floor降到0。当前full工具将G和编译floor绑定，若直接复用此工具，
必须把相应floor变化明确计入部署决策，不能悄悄声称floor保持不变。

**418现有OTA目前不能安装任何仅由陌生新钥签署的过渡版本。** 没有匹配身份或已验证
授权机制，提升版本/计数也不能解决签名失败。推荐有线路径绕开的是更新传输前端，
不是ROM/启动验签。它不应被记录成“通过418 OTA迁移成功”。

若将来具备匹配身份而选择418 OTA首个过渡版本，该包必须先用418接受的身份签署，
版本严格大于已安装版本，应用安全计数严格大于active计数并满足实际floor，保持CP/AP
组合及catalog合法。新的比较政策只有过渡固件已验签启动后才能生效；不能让包自行
声明按新规则验收。现有OTA不包含BL2，单独更改AP公钥不能完成下一代应用信任迁移。

第二阶段才做独立定义：软件版本、产物编号、应用安全计数、BL1/BL2软件floor分离；
先保持原有效比较约束，后审计确需改变的比较点。恢复后可首先演示一次按旧严格规则
签发和验证下一次CP/AP OTA；无需等计数解耦完毕才继续日常开发。

### 418旧包恢复边界

部署新信任关系后，418旧CP/AP通常不被仅持新应用根的BL2接受；其版本/计数也可能
不满足新软件floor或运行期严格递增策略。**不承诺418作为OTA降级包可用。**

418完整旧包仍可作为有线启动恢复候选，因为其中带有相互匹配的旧BL1/BL2/manifest/
CP/AP，不需要重新签名；但真正恢复必须同时成立：ROM仍允许这条旧链、硬件根及OTP
计数仍兼容、仍可获得有线入口、目标同一设备且数据回退/SD数据格式兼容。
若只是提高可替换BL1/BL2的软件floor，而硬件限制未变，恢复完整旧链可能同时恢复其
旧软件floor；这是有条件的机制判断，不是已做恢复实验。硬件计数若超过旧包允许范围，
不能靠重写软件绕过。

因此建议不编程OTP、不锁下载、不做不可逆数据迁移，并保存原418封存字节；
待单独授权恢复演练才把“候选”升级为“恢复通过”。新密钥副本解决持续签发，旧包
解决有条件恢复，两者不互相替代。

**当前可供用户决定：**采用“只读确认开发状态→合法有线恢复持续签名→日常CP/AP OTA”
作为推荐路线；先授权列明的只读核查，另决定持续密钥保存权限。设备状态确认后再选择
已有8MiB传输还是有据可行的有界方案。本轮仍只更新文档，未进入实施。


## 开发流程整改最小变更清单（A1–A8，已批准实施）

目标是减少一次普通功能改动到可信验证的无关步骤，不以删文件、测试数量或文档长度衡量。
本表替换上一版，不新增治理范围。418恢复、OTP/ROM、真实密钥接入是独立待决项，
不阻塞A类规则、代码和主机行为整改。以下保留批准边界，实际实施及验收见本节末尾。

| 项目/具体位置 | 最小实施边界 | A类必须取得的行为证据 |
| --- | --- | --- |
| A1：AGENTS.md硬件迭代/信任/委派段；AIDK AUTOMATION.md示例 | 默认增量、定向验证；创建/轮换身份、完整交付验收、恢复演练分开。批准后普通步骤连续执行，到本次验收点停止扩展；越权或关键风险才停，不逐步重提方案 | 实施记录显示普通路径未添加默认clean、备份、keygen或全产品测试；规则与实际调用一致，非仅改文案 |
| A2：_lib/build.py:_atomic_text/_atomic_bytes、_role_build_identity、_build_bl1/_build_bl2；_lib/trust.py公开源生成 | 相同生成内容不替换，保留文件类型/软链接检查与原子更新。依赖现有CMake/Make和角色identity，只重建受影响范围。启动组件按负载、配置、工具链、签名身份、签名覆盖字段、兼容依赖是否变化决定复用，不能只看CP/AP源码 | 复用现有workspace/package_delivery用例验证mtime和变化更新；现有命令观察无修改增量不发生无关重编，局部修改只触发应有依赖。如仍有无依据重建，在原构建链修复后才通过；不把mtime修复本身等同整体增量有效 |
| A3：build.py:_write_build_manifest/load_build_manifest；bk7258.py:_release | 扩展现有清单记录完整源码提交、相关dirty摘要、实际CP/AP profile和产品来源。摘要范围限定构建消费的源码/配置/依赖；排除输出、日志、备份、凭据，不自动打包完整diff。构建来源只写清单，不写所有源码依赖的公共头 | 现有manifest读取/往返/路径独立用例通过；查询和校验不改清单；来源元数据变化不诱发无关源码重编。摘要证明可区分输入，不冒充已归档全部dirty源码 |
| A4：bk7258.py发布命名/_release_generation/_release_output；现有release/package清单 | 增加独立artifact_id，仅创建新产物时显式确定，查询/构建不自动分配。新产物外部命名含产品/chip/board/profile/version/id/kind，内部SDK名及.bkpack格式不变；保留既有读兼容、防覆盖、最终文件哈希。security_counter独立说明职责，本批保留+G兼容映射 | 复用发布防覆盖等用例，覆盖显式ID、旧包可读、非法路径字符、最终文件摘要；多次查询不改变身份、内容或公共生成文件。同version不同ID不意味着设备允许升级 |
| A5：_lib/product.py:load_policy/report_policy/load_base_evidence；_lib/package.py:materialize_full_image；AIDK AUTOMATION.md | 复用layout/release-policy，依据实际擦除、写入及启动迁移影响决定备份。无相交需保留区且无相关迁移，不要求整机备份；有影响则明确区域保留/备份或已授权回退；不可重建数据无可信材料则阻止相关操作 | 复用package_delivery中的base/板型拒绝用例，验证OTA无无关base要求、full真实范围及device-unique处理；未知迁移不能输出无影响。真实数据保持属B类 |
| A6：bk7258.py:_release；_lib/build/trust/package/product；现有HIL prepare/run接口 | 收敛已知调用链的重复策略：板型/布局取board preset+layout，数据策略取release-policy，发布generation只由现有解析入口负责，签名身份取明确输入和清单指纹，最终文件大小/hash取封存release。实际出现的第二份业务推导改为消费该权威结果；末端字节复核保留。Windows/WSL、复位、退出码薄适配保留。不新建入口，不把内部模块塞进巨型脚本 | 对已确认重复项列修改前后消费者和唯一来源；现有对应正反例不退化。无法证明无用的脚本保留；不以零删除作为去重失败，也不以不新增入口冒充已收敛。跨校验/使用边界的重复hash不删 |
| A7：AGENTS.md任务执行约束；现有tests/host/bk7258、tests/pytest/test_bk7258/HIL | 新增测试/探针先一句话说明当前问题、现有方法不足、结果如何改变行动；已有能力够用不再造框架。枚举/构建/hash/日志筛选交工具，模型只看摘要、相关错误和证据位置；必要时用实际可用经济模型，不造路由系统。有效结果按相关输入和环境复用，到验收点停止扩展 | 本批每项验证可对应A1–A8具体行为；无无关框架/程序、全仓凭据扫描、反复全量日志。复用不免除每次写入前必要一致性检查；只据现有日志说明减少的调用，不新增token统计器或虚报比例 |
| A8：AGENTS.md trust；AUTOMATION.md签名职责；_lib/trust.py:signed_release/signed_ota_pair及显式key参数 | 持续身份与构建目录分离，普通操作不得自动生成/替换/销毁；缺指定身份只阻塞相关签名/部署，公钥构建仍可进行。未变且仍有效的签名组件按覆盖字段和依赖复用；应用OTA继续用现有signed_ota_pair，不重签BL1/BL2。禁止仅因CP负载相同复用已变化的版本/TLV/pair签名；不新增key broker/自动搜索 | 现有测试材料验证缺签名输入明确失败且不keygen；不依赖私钥的构建仍工作。相同已签产物读取/验证不重签或覆写；配置/身份/受保护元数据变化则拒绝误复用。实际长期密钥创建/保存/导入和设备接入仍另行授权 |

### 必须保持的具体边界

**产物命名示例**（未来新产物规则，不改418）：
`shaniu-bk7258-aidk_ai_toy-app__openvela_ap-v18.6.354+418-b<artifact_id>-full.bkpack`。
OTA以`-ota.bkpack`结束，整机loader BIN以`-full.bin`结束；不生成不需要的种类。
产品从明确产品选择/输入获取，不能由板型猜；实际profile以清单为准。
旧包和工具要求的内部成员名不改，latest只能作为引用。新名字和清单一并绑定同一字节。

**身份与计数**：version是软件版本，artifact_id是一次不可变产物身份，security_counter
是反回滚输入。本批只完成产物号独立和当前兼容映射显式化，不修改+G驱动的安全计数、
BL1 manifest计数、BL2 floor或设备端严格递增规则，不宣称实际安全计数解耦完成。
由这些字段变化而必须更新的启动组件不能强行复用；保留这项策略限制，不重开418研究。

**启动/签名复用的实现规模**：先复用现有增量依赖、封存release与apps-only签名路径。
同一组件的输入及签名覆盖字段完全相同才复用；版本/TLV依赖/pair变化要重生成关联内容。
不建立新的组件缓存数据库、签名缓存服务或持久化管理系统；若现有工具无法表达必要
复用，只在现有build/trust边界做能由当前证据支撑的最小修改，不能绕过匹配校验。

**A6的已知重叠**：HIL独立profile中的AIDK8MiB限制与项目布局有重叠；本批保留其
运输能力上限，项目参数从既有release取值并一致性核对。不能为了“一处规则”删除
HIL的最后保护，也不在未授权外部skill修改时擅自改它。项目scope内发现的重复业务
推导在原模块收束；已知重复生成文件由A2处理。没有证据支持删除的薄适配不删除。

**真实范围**：目前AIDK HIL是8MiB路径，即使任务叫CP/AP更新也按该实际范围判断。
已有USB OTA是另一条路径，其可用性和实板数据影响不能由Loader成功代替；不虚构
分区下载能力。保留TTS模型、声音数据、设备专属材料、可用固件、凭据、未提交成果。
具体删除、外部技能修改、密钥操作、设备写入均不包含在A类一般实施批准中。

### 执行顺序与收口

批准后A1/A7/A8执行约束先落地，A2完成真实无修改/局部增量行为；再按同一现有清单
完成A3/A4，落实A5/A6。上述A项连续推进，不每完成一项再提交建议方案；相关失败
定位修复，不增加无关验收。额外性能优化或外围清理记录为范围外，不阻塞本批。

A类独立交付要求：不仅规则已改，还必须有表中实际主机行为证据，相关用例通过；
生成文件/重编目标/调用参数有前后结果，签名边界用既有测试临时材料验证，包身份
不被查询修改。不得用已有418实板结果代替本次工具变更验证；不需为计时重刷设备。

B类另列：受信身份部署后的真实增量→签名→必要更新→版本/CP/AP确认、实际数据保持、
下一次OTA持续签发及单独授权的恢复演练。A类完成不代表B类通过；B类待决不阻塞A类。
本轮已完成下列A类实施与主机验证；未删除已有资源，未生成/使用私钥，未访问设备，未提交或推送。


### A1–A8实施与主机行为验收（2026-09-10）

本次范围已收口。A类主机行为验收通过，不代表实板可持续签名、更新或产品验收通过。
418签名能力仍按已有结论处理，未重新调查、读取OTP或尝试恢复。

证据目录：workspace `out/bk7258-workflow-a1-a8/`。`baseline.status`、
`baseline.diff`记录实施前已有工作区变化；未清除这些变化。临时CP源码改动已恢复，
原文件摘要在`local-source-restored.json`。没有新增审查脚本、测试框架或统计器；
仅扩展两个现有host测试文件，构建/哈希/日志统计使用现有入口及一次性工具命令。

| 项目 | 状态 | 已落实内容与行为证据 |
| --- | --- | --- |
| A1 | 已实施并验证 | `AGENTS.md`与AIDK `AUTOMATION.md`删除每轮新密钥/销毁密钥要求，区分普通增量、受影响回归、启动恢复专项。`build-command.json`和本轮构建日志使用同一工作目录、公钥及floor，无`--clean`、备份或keygen步骤。 |
| A2 | 已实施并验证 | `build.py`、`trust.py`、`layout.py`生成内容相同不替换，仍拒绝软链接/非法类型；BL2以已有合法binary大小作为首次copy_size估值，保留最终大小收敛与边界检查；生成配置默认开启NuttX既有`CONFIG_LIBC_UNAME_DISABLE_TIMESTAMP`，显式profile设置优先。`final-no-change.json/log`：0对象重编、0个被观察的生成输入/对象/ELF/BIN变化。`local-cp-change.json/log`：仅`bk7258_motion_main.c.o`重编；AP、BL1、BL2未变；恢复源码后对应对象重新生成。 |
| A3 | 已实施并验证 | 原build manifest升级`/3`，记录项目commit、限定源码摘要/dirty、实际CP/AP profile、显式product；NuttX/apps记录Git基线及实际工作树变更摘要。复制型NuttX使用canonical Git与实际work-tree比较，关闭索引自动刷新；不导出diff。现有SDK、工具链、配置及公钥指纹继续关联。`/2`读取兼容测试、复制源码变化/索引不变测试通过；`final-no-change.json`同时确认三个源码仓库索引字节与mtime未变。来源只入清单，最终元数据修改未触发对象重编。 |
| A4 | 已实施并验证 | 新`/3` release在创建时要求显式artifact_id；`--product`可来自build provenance；外部stem含产品/chip/board/实际profiles/version/id/full或ota。已有`/2`调用保持旧名；`.bkpack`内部格式未改。release读取核对identity、generation、build evidence和包名；同版本不同ID仍使用原+G counter。现有防覆盖/损坏拒绝及新增identity负例通过；`query-immutability.json`确认新manifest和418封存release/package的哈希、mtime均未被查询改变。 |
| A5 | 已实施并验证 | `product.operation_impact`复用verified package contract和release-policy，在现有`package flash-contract --transport`查询及release摘要中区分整片BIN与OTA。8MiB BIN的实际擦写覆盖`[0,8388608)`，package覆盖项单独列出；OTA无整机base要求，设备擦除粒度及启动迁移仍标未知。保留same-device材料、base/hash/板型拒绝；未知迁移不输出无条件放行。`query-3.log`为真实418包的只读范围报告，host正反例覆盖full/OTA差异。 |
| A6 | 已实施并验证 | release中的generation解析改为消费`product.version_generation`，不再维护第二份版本解析；release全量物化与摘要共用一次加载的preset/policy；影响查询和release摘要复用同一operation_impact。SDK/HIL薄适配、现有末端hash与目标检查保留，未增加下载入口或删除文件。现有调用方、四板声明fixture、目标/base负例和uint32上限检查通过。 |
| A7 | 已实施并验证 | 执行停止条件、相关测试说明、工具处理机械工作和模型只读摘要已写入原规则。本轮仅26个相关host用例及AIDK当前公钥构建，无全产品、三板矩阵或设备测试扩张；任务完成即停止，不统计或虚报token节省比例。委派复用了两个既有子代理；未另建模型路由系统，也不宣称后台模型路由已独立验证。 |
| A8 | 已实施并验证 | 规则明确持久签名身份的引用/权限，普通操作不得自动生成、替换或销毁。原签名函数显式传入身份、OTA不重签BL1/BL2的机制原已满足并保留；release增加不存在身份文件的前置拒绝，发生在manifest读取和staging创建前。full/OTA缺参数或不存在路径测试确认不启动签名工具、不产生发布目录；真实公钥构建通过。418既有包仅公开验签及只读复用，不触碰私钥、不声称恢复持续签名能力。 |

实际修改文件（仅本轮增量，不等于整个dirty工作区）：

- `AGENTS.md`
- `boards/bk7258/aidk_ai_toy/AUTOMATION.md`
- `tools/bk7258/bk7258.py`
- `tools/bk7258/_lib/{build,layout,trust,product}.py`
- `tests/host/bk7258/test_bk7258_build_workspace.py`
- `tests/host/bk7258/test_bk7258_package_delivery.py`
- 本报告。

关键行为记录：

- `incremental-1.log`、`incremental-2.log`用于定位：先发现生成内容重写及BL2反复改变copy_size，
  随后确认NuttX always-touch uname仍导致CP/AP重链。这些不是通过证据。
- `config-transition.log`为关闭uname时间戳后的受影响配置构建：配置缓存身份变化，CP/AP建立新目录，
  未删除旧构建或全量clean。仅这次配置切换不能当作普通无修改构建。
- `no-change.json/log`及最终`final-no-change.json/log`为通过证据。后者涵盖最终生产代码，
  public-only MCUboot构建退出0，源码仓库索引不变，所监测对象/头文件/链接脚本/ELF/BIN均不变。
- `local-cp-change.json/log`与`local-cp-restored.json/log`验证实际依赖：在已有CP C源文件末尾
  临时加注释后重建，再恢复原字节重建；仅一个CP对象及CP重链产物mtime变化，二进制内容相同。
  这是源码依赖范围实验，不冒充功能修改或实板功能验证。
- `host-tests.log`：`python3 -m unittest tests/host/bk7258/test_bk7258_build_workspace.py
  tests/host/bk7258/test_bk7258_package_delivery.py`，26项通过。新增测试只覆盖本次生成文件行为、
  来源/副本识别、identity读写绑定、实际传输影响及缺签名能力拒绝；其余复用既有正反例。
- `query-0.log`到`query-3.log`：现有CLI验证最终build manifest、418 package、包公开信任证据，
  并输出full-bin影响；4条命令退出0，准确参数在`query-immutability.json`。均为只读，非安装验证。
- `artifact-summary.json`记录最终handoff及配置身份；manifest SHA256：
  `b38780aa2e571578de61f8b2f40b0b0b9aec6b66f60c654561aa389c19080c11`。
  目标AIDK/app + openvela_ap，boot=mcuboot，floor=418仅复用本次已给定构建输入，未推断OTP。
  实际命令保存在`build-command.json`。该构建不是新签名发布包，也未改变418封存包。
- `git diff --check`通过。没有提交、推送、设备操作、资源清理或私钥操作。

保留机制及限制：

- 角色identity、现有CMake/Make依赖、发布防覆盖、最终文件hash及执行边界复核原已满足，未削弱。
  启动组件按实际内容/配置/公钥/受保护字段/依赖复用；本次BL1/BL2的无变化复用有构建证据。
  封存签名产物复用经只读验证；新签名生成及设备接受性不在本批权限内，未执行。
- +G到计数/floor的现有兼容关系保留，未完成安全计数策略解耦；新ID不承诺允许安装。
  新`/3`不带artifact_id会拒绝发布；旧`/2`不新增身份字段的调用仍兼容。
- 来源摘要是对应已说明源码范围及实际依赖工作树的证据，不是自动归档dirty源码或秘密；
  manifest来源变更本身不令已编译对象失效。显式重新启用uname时间戳会按NuttX机制恢复重链。
- A5输出是可核查的数据影响描述，不替代设备绑定、授权、真实迁移评估或HIL执行检查。
  full-bin不能解释为仅更新CP/AP；没有在本轮增加新的分区下载能力。
- B类仍待验收：长期签名身份实际部署、实板更新及版本确认、真实数据保持、持续OTA、授权恢复演练。
  本轮不再以418恢复为前置，也不以A类通过替代上述B类证据。


### 开发复现与工具职责收口（2026-09-10，本轮增量）

本轮基于已发布 `b2584355ed0e508eefb289b0d9ce5557a2ea78d3` 及既有dirty工作区。
只处理用户指定的五项，不重启418恢复、外围资源清理或三板验收。

| 项目 | 当前结果与证据 |
| --- | --- |
| README/SOP签名口径 | 已修正。普通构建使用批准公钥；发布引用持续受信身份，创建/轮换/销毁等独立授权。当前CLI仍是PEM路径接口，不声称已实现HSM适配；版本计数保留现行映射。两份文档定向diff检查通过。 |
| 开发与比赛复现 | 已补remote/revision的local manifest覆盖示例，不复制linkfile、不改比赛XML。实际`repo manifest -r`导出团队checkout为`b2584355…`，声明仍继承`openvela/dev-ai-contest-2026`；SDK仍锁定`cb080de1…`。未提交Dolphin不在该提交中。示例XML匹配现有项目名，未安装示例或执行sync。 |
| CLI业务职责 | `artifact_identity`、`artifact_stem`、`release_identity`、`load_release`及交付编排迁入现有`_lib/product.py`。CLI仅传入显式参数与验签回调并打印交付结果。`build.validate_provenance`改为公共合同校验接口，build/product/测试共用；不保留第二套schema推导。哈希仍分块读取，原有目标、路径、counter、材料化、末端校验及防覆盖保留。 |
| 工具/产品归属 | 在现有`tools/bk7258/README.md`记录构建/包/信任/交付/部署/产品辅助工具职责。保留_lib及有价值的适配。Dolphin、Shaniu保留团队仓内目录，不新增ttsindex项目或拆仓，不改frameworks/external映射。 |
| 定向验证 | 交付21项、工作区6项、layers pytest 5项通过；CLI layers通过。新增唯一编排回归复用已有合成包，比较CLI与模块输出字节、确认校验回调及防覆盖/拒绝行为；不代表真实签名或设备验收。 |

本轮实际修改：`README.md`、直接引用的
`docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md`、
`tools/bk7258/{README.md,bk7258.py,_lib/product.py,_lib/build.py}`、
`tests/host/bk7258/{test_bk7258_package_delivery.py,test_bk7258_build_workspace.py}`及本报告。
`build.py`中的既有kernel-compat差异不是本轮新改动；本轮仅公开provenance校验接口。
既有Dolphin、配置收束、chip-board和layers改动全部保留，未整体提交。

证据目录：工作区 `out/bk7258-cli-repro-20260910/`。
- `product.log`：`python3 -m unittest discover -s tests/host/bk7258 -p test_bk7258_package_delivery.py`，21项通过。
- `workspace.log`：同入口`test_bk7258_build_workspace.py`，6项通过（含生成文件不重写及缺钥前置拒绝）。
- `layers-tests.log`：`python3 -m pytest -q tests/host/bk7258/test_bk7258_layers.py`，5项通过。
- `layers.log`：`python3 tools/bk7258/bk7258.py verify layers`通过。
- `manifest-summary.json`、`resolved-workspace.xml`、`manifest.log`：Repo本机只读解析及示例核对。

边界：当前工作区layers已枚举整个app及board/app构建文件，不再沿用固定提交只覆盖C/C++的结论；
它仍是静态规则检查，不能证明动态CMake展开、所有Kconfig依赖或最终ELF/实板行为。
CLI命令注册仍会导入领域模块；按命令懒加载未实施，不宣称启动耗时优化或整个CLI已无业务编排。
未运行repo sync、固件构建、签名、设备操作；未创建/删除密钥、未删除文件、未提交或推送。
A1–A8既有真实增量构建证据继续适用其原输入范围，本轮测试不冒充新的固件或B类证据。
