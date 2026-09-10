<!-- SPDX-License-Identifier: Apache-2.0 -->
# 2026-09-07 AIDK v328 物理 PTT 接入阶段验证

基线为已合并的 `9ea6de9`，工作区分支 `fix/bk7258-physical-ptt`。
本次集成镜像包含工作区已有外围候选，不是纯发布 HEAD 的全功能验收。

## 实现范围

- CP 通过标准 GPIO lower-half 读取 AIDK P8 `/dev/gpio1`，30 ms 去抖；
  初始及重连后必须观测释放，再允许按下。独立 `bkvoice-ptt-v1` 消息携带
  递增序号、当前电平和心跳，不等待慢文件/播放 RPC。
- AP 的单个语音服务 worker 拥有会话、按键处理和音频资源；独立 RX 只投递
  有界帧队列。按键租约失效、超时和断连沿既有 stop/join/释放路径清理；
  清理失败保持待重试状态。
- `bkvoice provision/connect/disconnect/clear/status` 接入实际 mTLS provider。
  通过隐藏串口输入分块装载有界 BVC1 配置，包括主机名、IPv4、端口、CA、
  客户端凭据和操作者时间；密钥只留 AP RAM，清除或重启即失效。
  主机入口为 `tools/bk7258/bk7258.py voice provision`。
- 只在 AIDK `drivercheck_cp/ap` 配置启用物理 PTT 和 TLS。
  本轮未改 SDK/NuttX 实现；官方 NuttX/apps checkout 无已跟踪差异。

## 已取得证据

| 检查 | 结果 |
| --- | --- |
| AIDK CP/AP 干净构建 | 通过；实际 `.config` 和编译对象含 PTT、runtime、TLS、证书时间检查及硬件熵 |
| 信任及镜像检查 | 新的独立 BL1/MCUboot P-256 密钥，counter 388；完整 package/trust 验证通过；8 MiB，同设备尾区保持一致 |
| 自动下载 | COM8，BK Loader 软件 reset 原子接管；erase/write/success 全部通过 |
| 启动 | `18.6.328+388` confirmed；AP/CPU2/RPMsg 正常，faults/recoveries=0/0 |
| Wi-Fi | 隐藏输入配置后 link=3、获得 IPv4；日志不含密码 |
| AP 配置装载 | 1079 字节 RAM 配置装载成功；`configured=1`、`tls_available=1` |
| 连接超时后的清理 | `DISCONNECT PASS`、`CLEAR PASS`；之后 `configured=0`、`connected=0` |
| 主机配置工具 | 3 个离线测试通过；Windows 路径、命令先于秘密数据、分块及临时文件边界 |

完整 BIN SHA-256：
`429d9da52d8669a5384136018fd8cd9129ec3b28e0816480866a79d0de3ba9aa`。
临时签名私钥已删除。等待网络权限确认期间，临时 Gateway 已停止，mTLS 测试私钥
及 Wi-Fi 输入文件已删除，板卡 RAM 凭据已清除。构建输出及原始日志不进入源码提交。

证据目录：`out/shaniu-plan-20260907/` 下的 `hil-v328/`、`boot-v328/`、
`provision-v328.txt`、`connect-v328/`、`clear-after-timeout-v328/` 和
`physical-ptt-v328-status.json`。

## 未通过和后续门

连接实际 Gateway 返回 `-ETIMEDOUT`，服务端只记录启动，未记录会话。
本机 WSL 默认阻止入站，现有允许规则绑定旧来源地址。管理员添加新规则的
操作被自动审批拒绝，未执行；限定地址/端口、最多 30 分钟自动删除的方案
等待明确授权。网络路径和 TLS 握手尚未闭合，不能宣称已确认唯一根因。

物理按下/松开到 MIC 上行、Gateway 回复和 DAC 播放均未取得通过证据。
尚需快速重按、通道丢失、断网、重连、窗口背压与连续 turn 验收；
AEC 开关声学对照、其余人工外围验收及 Android 真机联调仍未完成。
当前 Gateway 为固定合成音调服务，尚未接入 ASR、对话或 IndexTTS 在线接口。
本次凭据装载只验证开发联调路径，不代表 BLE/App 配网和持久设备身份已完成。

v321 存储保留性与 301 帧严格解码的已通过基线继续有效；v323 后续异常另案保留。
本轮未重新定义或撤销存储验收，也未加入新的存储候选修改。
