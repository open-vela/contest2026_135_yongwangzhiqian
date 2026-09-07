<!-- SPDX-License-Identifier: Apache-2.0 -->
# 傻妞 companion-v1 最小 Gateway

这是确定性固定回复服务，用于验证 `companion-v1` 的 WSS、状态机、双向窗口和
流式下行。默认监听桌面 loopback；受控网络接入需双向 TLS 认证。它不包含
ASR、LLM、TTS、设备注册或私有音色。

## 当前行为

- 只接受 WSS 的 `/companion/v1`，并要求 WebSocket subprotocol `companion-v1`；
- TLS 最低版本为 1.2，不提供明文 `ws://` 或跳过证书校验的服务端选项；
- 默认仍只绑定 loopback，且不要求客户端证书。显式提供 `--client-ca`
  时，服务端要求 TLS 客户端证书由该 CA 验证；这也是唯一允许绑定非
  loopback 地址的模式；
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

默认服务不会接受 `0.0.0.0`、LAN 或公网地址。受控 LAN 验证必须显式传入
`--client-ca <CA-file>`；此时只接受单播字面 IPv4/IPv6 绑定，仍拒绝
`0.0.0.0`、`::`、有限广播 `255.255.255.255`、组播和域名绑定，并强制客户端证书验证。该选项不提供
部署凭据、设备注册或身份日志；证书和端点由受控部署流程提供。

## 验证

```sh
make test
```

测试用例在临时目录生成并销毁自签证书，实际建立 loopback WSS 连接，覆盖正常回复、
取消、sequence gap、turn 中断连、下行背压、证书 hostname 失败、绑定限制，以及
mTLS 的可信客户端成功、缺失客户端证书和不可信客户端证书拒绝。
成功标记为 `SHANIU_GATEWAY_TEST_PASS`。

协议对应实现是 `app/bk7258/bk7258_voice_companion.[ch]`。两端改动协议常量或状态规则时，
必须同步更新互操作测试；本目录的测试通过不等于 AIDK 实板闭环通过。
