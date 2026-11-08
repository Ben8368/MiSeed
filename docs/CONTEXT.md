# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3；PD/PPS checkpoint `43ce569` 已推送。旧测试镜像 `9ffbb579…8d3b29` RAM 启动成功，但救援台 `pps-status` 无控制器，USB supply 为 `Unknown [SDP] DCP CDP`、输入约 4.46 V / 0.60 A。源码复核发现 TLMM mask 只开放 UART34/35，阻断 LOS I2C15 GPIO44/45 和泵 IRQ GPIO68；本轮据固定 LOS 接线开放这三脚。新 mask ARM64 L2 镜像 SHA-256 `44dd30354492eba377843e942db9b06ec07e66048a8b6c914e046b9915e965b0` 已构建，但设备当前未连接主机，尚未 RAM 启动。用户照片显示充电器端 PD 5.2 V / 2.7 A，不代表手机内核 PD/PPS 合约已确认。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. 设备接回主机后 RAM 启动新镜像，读驱动绑定状态。
2. 若设备侧 PD 识别恢复，继续 PPS APDO、保护/温控、停止/恢复与拔插验证；核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
