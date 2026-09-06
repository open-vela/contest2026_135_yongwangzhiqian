# NuttX 维护补丁

补丁基线是 OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`。
在隔离的 NuttX 副本中按每个目录的数字顺序应用，不直接修改官方工作树。
以下命令在本项目根目录执行，`../nuttx` 应为该隔离副本：

```sh
for patch in "$PWD"/nuttx/patches/{video,mmcsd,fs,input,netdb}/*.patch; do
  git -C ../nuttx apply --check "$patch" || exit 1
  git -C ../nuttx apply "$patch" || exit 1
done
```

| 补丁组 | 作用 |
| --- | --- |
| `video/0001` | 分开原本冲突的 gain/flip 控制编号，补全 imgsensor 控制映射 |
| `video/0002` | 初始化 scalar S_CTRL 转换结构，避免未初始化 size/保留字段 |
| `mmcsd/0001` | 单独限制读请求，保留多块写入吞吐 |
| `mmcsd/0002` | 允许指定介质禁用可选的 ACMD23 预擦除提示，默认保持原行为 |
| `mmcsd/0003` | 关闭与 BIOC_FLUSH 等待最后一次卡内写入完成 |
| `mmcsd/0004` | 同时检查 TRAN 状态与 READY_FOR_DATA，等待有超时限制 |
| `fs/0001` | 保留 FAT 扩展链的底层错误并区分空间不足、损坏链 |
| `input/0001` | FF 文件关闭时停止并回收其效果，校验写事件与 owner，避免注销时访问已销毁 lower half |
| `netdb/0001` | 允许只有 RPMsg 套接字的配置启用已有 AF_RPMSG netdb/rexec 实现 |

`video/0001` 涉及控制编号 ABI，内核与客户端必须一起重建。
AIDK 配置选择一个扇区读、16 个扇区写、禁用 ACMD23；其他板卡默认值不变。
补丁不更改 CSD 容量、不格式化介质，也不把传输失败转换为成功。

`input/0001` 对应 FF upper-half 的实际生命周期缺陷，尤其是有界振动设备的停止与销毁。
其 host 测试在临时树应用真实补丁后编译执行，并以 ASAN 验证 destroy 释放 lower half
后的注销路径：

```sh
python3 tests/host/bk7258/test_ff_lifecycle.py
```

该测试不代表任何马达已经通过物理验收。

## OpenAMP / libmetal 依赖

OpenAMP 和 libmetal **没有本项目维护的源码补丁**。它们是工作区 repo
manifest 管理的依赖，T5 的 LittleFS 同样使用 manifest 项目。准确版本、提交和 tree ID 记录在
`nuttx/dependencies.lock.json`。先按官方 OpenVela manifest 同步这三个项目，
再由构建工作区以锁定 commit 的 clean Git archive 装配；不要用任意 upstream
zip 替换，也不要从已有展开目录复制头文件。

可在 host 上运行下列验证，它会从三个 clean Git checkout 的锁定 commit 创建
临时 archive，检查 tree ID 与 OpenAMP ABI，并编译一个仅含头文件的 ABI 探针：

```sh
python3 tests/host/bk7258/test_bk7258_openamp_dependencies.py
```

隔离构建使用同一个公开入口：

```sh
python3 tools/bk7258/bk7258.py build --workspace /absolute/validation-workspace \
  --board aidk_ai_toy --boot mcuboot --clean --jobs 8 \
  --bl1-public-key "$BL1_PUBLIC" --mcuboot-public-key "$MCUBOOT_PUBLIC" \
  --openssl /usr/bin/openssl --rollback-floor "$RELEASE_GENERATION"
```

`--workspace` 中的 NuttX 是固定基线加上表补丁；`build.sh` 必须指向该副本内部的
官方入口。`apps`、`build`、`external`、`frameworks`、`prebuilts` 可链接到官方工作区；
`vendor/beken/{boards/bk7258,chips/bk7258,nuttx,prebuilt}` 必须链接到当前团队仓库对应
目录，工具会拒绝其他源码映射。依赖使用锁定提交的 `git archive` 重建给 CMake；
使用 Make 时保留官方 manifest 的真实 Git checkout，以适配其 `.git` 探测。

后续 `verify build-manifest`、签名和 release 命令传入生成 manifest 的绝对路径。
全量发布版本的 generation 必须等于编译时的 rollback floor；私钥和设备绑定备份
继续遵循板级 AUTOMATION，不写入这个依赖锁或源码提交。
