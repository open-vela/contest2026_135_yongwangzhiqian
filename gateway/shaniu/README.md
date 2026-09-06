<!-- SPDX-License-Identifier: Apache-2.0 -->
# 傻妞 companion-v1 最小 Gateway

这是 `P2-A/S2` 的确定性固定回复服务，只用于在桌面 loopback 上验证
`companion-v1` 的 WSS、状态机、双向窗口和流式下行。它不是公网服务，也不包含
ASR、LLM、TTS、鉴权、设备注册或私有音色。

## 当前行为

- 只接受 `wss://127.0.0.1`、`wss://[::1]` 或 `wss://localhost` 上的
  `/companion/v1`，并要求 WebSocket subprotocol `companion-v1`；
- TLS 最低版本为 1.2，不提供明文 `ws://` 或跳过证书校验的服务端选项；
- 逐帧校验 40-byte network-order header、身份、turn、sequence、payload、状态和
  `WINDOW_UPDATE`；
- WSS 映射固定为“一条 binary WebSocket message 承载一个完整 `companion-v1` frame”，
  不接受 text message、半帧或一条 message 内拼接多帧；
- `TURN_END` 后先发 `TTS_START`，再按下行窗口逐个生成并发送 20 ms、16 kHz、mono、
  S16LE 的确定性音调帧，最后发 `TTS_END`；不构造或缓存整句 WAV；
- 日志只包含状态、帧/字节计数、首包延迟和错误码，不记录 PCM、文本、会话 ID 或
  远端地址。

## 本地运行

使用 Python 3.10 或更高版本安装冻结依赖：

```sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

下面的证书只适合本机开发测试，不得作为产品证书或部署凭据：

```sh
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout key.pem -out cert.pem -days 1 \
  -subj /CN=localhost -addext subjectAltName=DNS:localhost
```

启动服务：

```sh
.venv/bin/python -m shaniu_gateway \
  --cert cert.pem --key key.pem --host 127.0.0.1 --port 8765
```

服务不会接受 `0.0.0.0`、LAN 或公网地址。后续实板接入需要先增加受审计的鉴权、凭据、
部署和证书策略，不能通过放宽本切片的绑定限制完成。

## 验证

```sh
make test
```

测试用例在临时目录生成并销毁自签证书，实际建立 loopback WSS 连接，覆盖正常回复、
取消、sequence gap、turn 中断连、下行背压、证书 hostname 失败和非 loopback 绑定拒绝。
成功标记为 `SHANIU_GATEWAY_TEST_PASS`。

协议对应实现是 `app/bk7258/bk7258_voice_companion.[ch]`。两端改动协议常量或状态规则时，
必须同步更新互操作测试；本目录的测试通过不等于 AIDK 实板闭环通过。
