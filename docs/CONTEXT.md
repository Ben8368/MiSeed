# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3。Umi sink-only PD/PPS 测试节点已启用；新镜像 `9ffbb5794bc0988356d247f63a1b8259e7f9c90da5440eca60592c15fc8d3b29` 已通过 `fastboot boot` RAM 启动。用户照片中的充电器显示 PD、5.2 V / 2.7 A；同次救援台日志显示 USB supply 类型为 `Unknown [SDP] DCP CDP`、输入约 4.46 V / 0.60 A，`pps-status` 报控制器不可用。故照片证明充电器端 PD 状态，不证明 MiSeed 内核已协商 PPS；泵和自动 PPS 尚未验收。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. 查明真机 Type-C/TCPM 与 I2C15/泵控制器未绑定的原因，读取运行时 DT 和驱动探测错误。
2. 让 sink-only PD 路径在设备侧正确识别，再继续 PPS APDO、保护/温控、停止/恢复与拔插验证。
3. L2 和 RAM 启动完成；不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
