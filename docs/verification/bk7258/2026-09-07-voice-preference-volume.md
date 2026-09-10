# BKVoice 偏好音量接线验证（2026-09-07）

## 实现

复用公共 `media_policy_get_range`、`media_policy_set_stream_volume` 和
`media_policy_get_stream_volume`。轻量 player bridge 补齐 MusicVolume 范围及当前策略
档位查询，沿用原有互斥与增益设置；不新增 SDK/DAC 控制路径。

启用 BK7258_PREFERENCES 时，每轮 PTT 回复 prepare 完成后读取 KVDB 偏好，按策略
实际范围四舍五入映射档位，设置并读回核对，随后才启动播放。请求 73% 在 0–15 范围
对应 11 档，不宣称 DAC 精确为 73%。正在播放时的配置修改到下一轮回复生效。
失败走原有 turn 清理；prepare 后的策略错误也保留正确资源状态供 close 释放。

## 验证

`make -C tests/host/bk7258 run-voice-turn-audio run-voice-turn-volume` 退出 0，
`BKVOICE_TURN_AUDIO_HOST_PASS`、`BKVOICE_TURN_VOLUME_HOST_PASS`。
原有录播生命周期回归在开/关偏好两种编译配置下通过；新增覆盖 8 个场景：偏好读取、
范围查询、设置、读回失败，读回不一致、无效范围、越界偏好和正常应用；另有 6 个
量化边界，包括 0/1/50/99/100% 和 0–31 的不同最大档位。验证失败不播放且资源释放。

使用保留的 AIDK AP compile_commands（role identity `bk7258-role-5b54cde5639a3771`）
对当前 player bridge 和 turn audio 源码进行独立目标对象编译，额外开启偏好宏，输出
仅在临时目录。两个对象编译通过，符号检查确认新查询接口及偏好读取/策略设置引用存在。
未改变既有构建对象或生成新镜像，临时对象已清理。

## 剩余验收

板级 BK7258_PREFERENCES 仍默认关闭，等待配置后端及掉电恢复验收；本轮没有刷机、
实际扬声器听音或 Android 设置联调。编译与 mock 不证明目标策略读回或 DAC 听感。
人物设置、实时音量调整及 App reported-state 协议仍按主计划继续实施。
