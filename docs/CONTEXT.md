# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3，上一推送 HEAD `53ccc1e`。`9e065431…acfcea` 真机 RAM 启动成功，RPMh、I2C15 与 BQ25970 驱动均绑定。其后诊断镜像 `8f01d739…1955c8` 已 RAM 启动；USB 重连读到 TCPM 固定 PD `online=1`，控制器 PPS 激活和固定回退均 `-95`，导致 SMB5 所有权滞留，用户报告 14 W / 2 W 跳变。工作树已增加针对 PPS 激活入口 `-EOPNOTSUPP` 的无 APDO 清理修正和回归测试；ARM64 L2 新镜像 SHA-256 `50a2ce9c…1987e23` 已成功构建，尚待真机 RAM 启动。完整读数见[真机续查记录](archive/hardware/2026-11-08-umi-pps-quruntime-followup.md)。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. Fastboot RAM 启动清理修正镜像 `50a2ce9c…1987e23`，确认 `-EOPNOTSUPP` 时不再留住 SMB5，且检查 PPS 合约状态。
2. 若设备仍固定 PD，继续核实 TCPM 实际 APDO/激活错误；保护/温控/停止恢复/拔插完成 L3 前仍不标记 PPS 已激活；核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
