# BK7258 开发流程 A1–A8 整改与主机验收

本次发布仅包含已批准的 A1–A8 范围。更早的架构审查与设备恢复研究保留在本地，不纳入本次提交。

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
