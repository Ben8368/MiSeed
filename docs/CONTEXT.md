# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3，已推送 HEAD `b1ca2f3`。`9e065431…acfcea` 真机 RAM 启动证实 RPMh、I2C15、BQ25970 绑定；`50a2ce9c…1987e23` 日志证实 PPS 仍 `-95`，但 SMB5 回退后主充恢复、用户报告 PD 约 14 W 稳定。更新镜像 `27528568…1340dd3` 的 376 条无覆盖时间线显示：约 4.400 V BMS 样本触发软件停充，电压样本随后骤降并重开。当前补丁移除该重复门槛、保留有效采样与已核验硬件 FV；97 项回归通过，ARM64 L2 镜像 `1e6d4c5a…23b3a23` 构建通过，待 PR CI。TCPM 日志命令已加入，但暂无可核验 APDO 读数。详见[真机记录](archive/hardware/2026-11-08-umi-pps-quruntime-followup.md)。

固定 LOS Umi 参考为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。已移植自动 SMB5/PPS、3 A / 6 A / 4.45 V 与主充回退；完整 L3 保护及 PPS 合约仍未验证。

## 近期优先级

1. 验证当前充电波动修复并更新草稿 PR #3/CI。
2. 后续采集 TCPM PDO/APDO 与请求日志，闭合 PPS 合约、泵、保护、恢复及 GPIO8 核实；未闭合前不宣称 PPS 已启用。
3. 不刷写分区。见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
