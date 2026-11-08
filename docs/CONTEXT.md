# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3，HEAD `a5717e9`。早期镜像 `9ffbb579…8d3b29` 与 GPIO allowlist 镜像 `44dd3035…65b0` 均已在 Umi 上 RAM 启动，未刷写分区；allowlist 镜像能读 PMIC/充电遥测，但 `pps-status` 找不到控制器，USB supply 为 `Unknown [SDP] DCP CDP`、输入约 4.46 V / 0.60 A。用户新照片显示充电器端 PD 5.2 V / 2.8 A（约 14.6 W），这是充电器端 PD 档位证据，不证明手机内核已读取 PD 合约或 PPS APDO。

本轮新增只读 I2C 驱动/compatible 与 Type-C 类设备报告，并完成 ARM64 L2。诊断镜像 SHA-256 `f13d367585f96486e665219fd956d35447cc20e7a1df8ace167e87241c34c313` 尚未真机启动；当前主机没有 Fastboot 或救援串口设备。快充状态详见 [PPS](../kernel/power/PPS.md) 与 [RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布)。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. 设备接回主机后 RAM 启动新镜像，读驱动绑定状态。
2. 若设备侧 PD 识别恢复，继续 PPS APDO、保护/温控、停止/恢复与拔插验证；核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
