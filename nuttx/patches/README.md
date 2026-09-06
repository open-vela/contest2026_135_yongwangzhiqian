# NuttX 摄像头与存储补丁

补丁基线是 OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`。
在隔离的 NuttX 副本中按每个目录的数字顺序应用，不直接修改官方工作树。
以下命令在本项目根目录执行，`../nuttx` 应为该隔离副本：

```sh
for patch in "$PWD"/nuttx/patches/{video,mmcsd,fs}/*.patch; do
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

`video/0001` 涉及控制编号 ABI，内核与客户端必须一起重建。
AIDK 配置选择一个扇区读、16 个扇区写、禁用 ACMD23；其他板卡默认值不变。
补丁不更改 CSD 容量、不格式化介质，也不把传输失败转换为成功。
