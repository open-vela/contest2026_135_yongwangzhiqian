# AIDK GC2145 JPEG 拍照重启修复（2026-09-05）

修复版本为 `18.6.230+290`。COM8 实板通过同次启动连续三次拍照，以及正常重启后的首张拍照；每次均完成打开、取帧、关闭，返回有效 640×480 JPEG。

本次提交提取 chip 层的独立频率投票与停止生命周期修复。以下实板结果来自包含 AIDK 相机接入及诊断改动的完整 v230 工作区，不能视为仅检出本提交即可重建同一镜像；其余接入改动仍单独保留在工作区。

## SDK 对照与修复依据

v3.1.1.9 的 `dvp_camera_jpeg_mode()` 依次初始化 DMA、YUV、JPEG。`bk_yuv_buf_init()` 向 `PM_DEV_ID_JPEG` 请求 480 MHz，而后执行的 `bk_jpeg_enc_init()` 向同一个客户端请求 320 MHz。两个模块的频率需求共用一个可覆盖的投票位置。

核对源码：

- `vendor/beken/bk_avdk_smp/ap/components/bk_dvp/src/bk_dvp.c`
- `vendor/beken/bk_avdk_smp/ap/middleware/driver/yuv_buf/yuv_buf_driver.c`
- `vendor/beken/bk_avdk_smp/ap/middleware/driver/jpeg_enc/jpeg_driver.c`

上述路径相对 open-vela 父目录。实际链接库与适配层的 `frame_buffer_t`、DVP 配置布局已核对一致；JPEG 工作缓冲大小与 SDK 相机控制器的 `width * 16 * 2` 一致。

| 对照 | 结果 |
| --- | --- |
| v228：不选择额外 JPEG IRQ guard，遵循 SDK 原始打开流程 | 仍然重启 |
| v229：480→320 MHz 后保持 5 秒 | 通过；M1 分别为 `00100030`、`00100020` |
| v229：独立保持 480 MHz，完整拍照后释放 | 有效 JPEG，17,259 字节，关闭成功 |
| 同一 v229 固件、同次启动：取消独立保持后再次拍照 | 重启 |

这些证据支持保留整条 MJPEG 采集链路的独立频率需求；没有据此断言更底层的具体复位机制。

## 实现位置

- [bk7258_dvp.c](../../../chips/bk7258/ap/bk7258_dvp.c)：MJPEG 打开前取得独立 480 MHz 投票，SDK 关闭、停止 DMA 后释放；后续资源获取失败时回滚，释放失败保留所有权标志供再次 uninitialize 重试。
- [bk7258_pm.h](../../../chips/bk7258/include/bk7258_pm.h)：在现有客户端编号之后增加 `BK7258_PM_FREQ_CLIENT_CAMERA`，通过原 CP PM 服务处理。
- [AIDK Kconfig](../../../boards/bk7258/aidk_ai_toy/Kconfig)：撤掉无效的 JPEG IRQ guard 选择，恢复 SDK 原始 JPEG 打开时序。

最终路径还保留已按 NuttX 上层调用上下文修正的停止处理：完成回调内只停止软件交付；普通 MJPEG STREAMOFF 不提前复位 SDK JPEG/YUV，硬件退出由 SDK close 完成。频率保持覆盖这个完整生命周期。

临时 APP 调频/日志屏蔽、NSH 内存查看开关及未通过正向控制的 CP SRAM 重启记录已移除。SDK 源码与预编译库未修改。

## 实板验证

| 用例 | JPEG 有效字节数 | 结果 |
| --- | ---: | --- |
| 同次启动第 1 张 | 17,711 | SOI/EOI 正确，V4L2 无错误，close=0，无重启 |
| 同次启动第 2 张 | 16,887 | SOI/EOI 正确，V4L2 无错误，close=0，无重启 |
| 同次启动第 3 张 | 16,939 | SOI/EOI 正确，V4L2 无错误，close=0，无重启 |
| 正常重启后第 1 张 | 17,627 | SOI/EOI 正确，V4L2 无错误，close=0，无重启 |

干净构建、包验证、信任链验证、同设备完整下载均通过；8 MiB 镜像尾区与已接受的同设备基线保持一致。

本地证据位于 open-vela 父目录：`out/bk7258/aidk_ai_toy/hil-v230-20260905-001/`，包含 `acceptance.json`、`sdk-review.json`、下载结果及各次 UART 日志；对照证据位于相邻的 `hil-v228-20260905-001/`、`hil-v229-20260905-001/`。

镜像 SHA-256：`744e313ce196b9676bf6ccd0c4a35f8abdbbf78ebfc34e2e4d1cf638df5a7f6e`。

本次验证范围是拍照重启回归，未替代 100 次成功/取消/超时、泄漏、隐私指示及 M4 视觉问答产品验收。
