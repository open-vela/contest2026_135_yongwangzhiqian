# Gateway 人物风格验证（2026-09-07）

沿用既有五种 persona 名称、MiMo provider 和每连接会话。`--persona-mode` 设置
启动默认值，`MiMoConversation.set_persona()` 只允许在无进行中/待提交回答时切换。
固定 system policy 保留，附加白名单风格；任意提示词和未知模式拒绝。切换下轮生效，
保留有界上下文，与其他连接隔离；不修改 emotion、TTS 音色或设备权限。

`make -C gateway/shaniu test`：40 项通过，`SHANIU_GATEWAY_TEST_PASS`。
新增 3 项异步测试覆盖五种风格不同、固定 policy 保留、下一次请求实际携带新风格、
保留历史、连接隔离、进行中/待提交/关闭拒绝切换、非法模式拒绝。
既有本地 HTTPS/WSS 端到端、取消、回压和多轮上下文测试同时通过。

本轮没有为人物风格调用外部模型；模型输出风格质量与实际听感仍待真实交互评价。
Android 鉴权控制端点尚未接入，不声称 App 远程设置已经生效。
