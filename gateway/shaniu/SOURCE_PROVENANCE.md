<!-- SPDX-License-Identifier: Apache-2.0 -->
# 来源与许可证

| 范围 | 来源与许可处理 |
|---|---|
| `shaniu_gateway/protocol.py` | 根据本仓 `app/bk7258/bk7258_voice_companion.[ch]` 的 `companion-v1` 常量、40-byte network-order header 和会话约束独立实现；未复制外部源码，两端均适用本仓 Apache-2.0。 |
| `shaniu_gateway/server.py` | 本仓创建的 S2 loopback WSS 固定回复实现，适用 Apache-2.0；只调用 Python 标准库及 `websockets` 的公开 API。 |
| `websockets==12.0` | Python 第三方依赖，发行包元数据声明 BSD-3-Clause；本仓不复制其源码。 |
| `tests/*.py` | 本仓为上述协议与服务创建的 host 测试，适用 Apache-2.0；测试证书在临时目录即时生成，不作为源码或交付凭据。 |
