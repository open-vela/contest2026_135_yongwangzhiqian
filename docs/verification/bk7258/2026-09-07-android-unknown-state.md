<!-- SPDX-License-Identifier: Apache-2.0 -->
# Android 未上报状态与 APK 构建验证

2026-09-07：为接入真实 Gateway 状态查询，移除设备音量/充电状态的虚假初值。
开发中的 console-v1 snapshot 保留原字段集合，但允许 `volume_percent`、`charging`
显式为 null；缺字段、错误类型和超范围数值仍拒绝。设置/电池增量事件继续要求真实值。
旧版严格解码器不接受 null，联调需要使用更新后的 App 与 Gateway 契约。

`CompanionState` 未收到上报时保存 null。界面显示未知，音量控件在已知上报前禁用，
不把禁用滑块的内部位置当作设备真实音量；已知数值和布尔状态的处理保持兼容。

验证：

- 缓存 Kotlin 2.0.21 编译器独立编译协议与测试，JUnit 22 项通过。
- 初次 Gradle 校验发现未配置 SDK，随后发现候选 SDK 缺少 Android 35；没有重复下载。
  核对并复用已有 Windows Android SDK 35 的本地映射后，离线运行
  `:app:testDebugUnitTest :app:assembleDebug`，43 项测试、0 failure、0 error。
- 新增测试涵盖 null 往返、状态存储不补默认值、错误 charging 类型及音量边界；
  测试夹具显式要求其模拟上报值已知，不引入生产 fallback。
- APK：`android/shaniu-companion/app/build/outputs/apk/debug/app-debug.apk`，
  4062699 字节，SHA-256
  `3642b5a0256358588444b80c9eab42f1a366c7b7fe39365a80f2a4b6462daa1f`。

此阶段未安装手机、未修改设备或 Gateway 部署。完成的是未知状态表达及客户端构建，
不是 HTTPS 控制端点、Android 鉴权或实板上报验收；后续服务端必须继续保留未知语义。
