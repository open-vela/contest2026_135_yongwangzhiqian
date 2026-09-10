<!-- SPDX-License-Identifier: Apache-2.0 -->
# Gateway 设备身份绑定验证

2026-09-07：在现有 `python -m shaniu_gateway` CLI 增加可选设备登记文件输入，
通过已验证 mTLS 叶证书 DER SHA-256 映射设备 ID，为 Android 控制寻址提供基础。
没有新建公共服务入口或更改 companion-v1 帧格式。

加载器限制文件类型、所有者/写权限、大小、设备数量、字段集合及 ID/证书唯一性。
内存映射复制后只读。Gateway 只在 HELLO 成功后返回可寻址连接；重复在线连接拒绝，
关闭或 HELLO 超时释放占用。未登记证书即使通过 CA 验证也被拒绝。

```sh
make -C gateway/shaniu test
```

46 项测试通过。新增真实本地 mTLS/WSS 测试覆盖登记连接、HELLO 前隐藏、重复连接
不替换现有连接、断开重连、CA 信任但未登记的证书拒绝、loopback 也不得绕过 mTLS、
HELLO 超时后可重连。加载器测试覆盖未知/重复字段、重复设备/证书、无效 ID/散列、
超限文件、符号链接和其他用户可写文件。日志检查不含测试设备 ID。
使用即时生成测试证书及本地模型替身，未调用真实 MiMo 或修改板端凭据。

边界：这不是 BLE/App 认领或 Android bearer 授权实现，也没有 console-v1 HTTP/WSS
控制端点。未登记模式继续服务原语音夹具，但不给未来控制层提供设备寻址。后续需将
App 授权、状态/修订与控制操作连接到上述已验证设备连接；板端未知音量/电池等信息
不能以固定默认值冒充上报状态。
