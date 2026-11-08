# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3，已推送 HEAD `fd928e9`（该 HEAD 的六项 GitHub Actions 检查全成功）。`9e065431…acfcea` 真机 RAM 启动成功，RPMh、I2C15 与 BQ25970 驱动均绑定。诊断镜像 `8f01d739…1955c8` 日志证实 PPS/fixed fallback `-95` 会留住 SMB5。修复镜像 `50a2ce9c…1987e23` RAM 启动后，USB CDC 读取到 532 条无覆盖日志：PPS 仍 `-95`，但 `owned=0`、SMB5 `enabled=1`、输入在线；用户报告 PD 约 14 W 稳定，TCPM 仍为 PD 5V/3A。新增显式 `tcpm-log` 读取协议环，以取得 Source_Capabilities/APDO 与请求状态；ARM64 新 L2 已成功构建，待 RAM 启动采集。完整读数见[真机续查记录](archive/hardware/2026-11-08-umi-pps-quruntime-followup.md)。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. Fastboot RAM 启动带 `tcpm-log` 的最新镜像，读取 Source_Capabilities、PPS request/response 与状态转换，定位固定 PD 原因。
2. 按真实协议轨迹修复后复验活动 PPS 合约、泵遥测、温控/保护/停止恢复/拔插；完成前不标记 PPS 已激活。核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
