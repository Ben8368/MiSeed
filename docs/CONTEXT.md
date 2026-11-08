# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3，已推送 HEAD `2806638`。`9e065431…acfcea` 真机 RAM 启动成功：RPMh provider 绑定、I2C15 枚举、`15-0066` 的 BQ25970 PPS 驱动绑定，`-95`/deferred 阻塞解除。`pps-status` 为 `state=1 error=0 requested_mv=0 requested_ma=0`，即仍等待 USB-PD 条件、无 PPS 请求；用户拔电脑 USB 接充电器仍看见固定 PD。SMB5 APSD 快照为 `Unknown SDP DCP [CDP]`、约 4.58 V / 0.506 A，不能用它判定 TCPM PD/APDO。新增固定白名单 power_supply 只读诊断，L2 镜像 `8f01d739…1955c8` 尚未启动，检查时用户已在墙充、USB CDC 不在主机侧枚举。完整证据见[QUP/RPMh 续查](archive/hardware/2026-11-08-umi-pps-quruntime-followup.md)。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. 设备重新连到主机/ Fastboot 后 RAM 启动 `8f01d739…1955c8`，查看 TCPM source power_supply 的 `usb_type/online/PDO`。
2. 依据设备真实属性对齐 PD/APDO/PPS 入口；PPS 请求及保护/温控/停止恢复/拔插完成 L3 前仍不标记激活；核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
