# 当前状态

日期：2026-11-09。工作分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3。四轮 RAM 测试取得 APDO 5–11 V / 5 A、PPS 约 8.8 V / 3 A；第三版曾 switching=1 后退出，第四版 `464f8c1b…b8cd0b89` 在 SOC 90% 时因首个 BMS/泵 VBAT 相差 54 mV 被原 ±50 mV 门控拦下，未启泵，已恢复固定 PD/SMB5。当前源码修复协议日志、重复 AMS、LOS 电流采样及启动电压采样等待；最新 Linux 97 项回归通过。用户要求停止构建/真机测试并先提交 PR，已停止本轮 L2（退出 137），最新源码无完整 L2/L3。详细身份、哈希、命令及边界见[本轮记录](archive/hardware/2026-11-08-umi-pps-ams-retry.md)。

固定 LOS Umi 为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC；未猜实物 sense 值。不能宣称持续 PPS 或完整保护已验收。

## 近期优先级

1. 跟进草稿 PR #3 的精确 HEAD CI；最新本地完整 L2/L3 未完成，旧镜像证据不沿用。
2. 用户恢复验证后再构建/启动最新采样等待修复，闭合持续调节、保护、温控、停止恢复、拔插与 GPIO8。
3. 不刷写、不合并；提交/推送仅限本轮已授权工作分支。见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[本轮记录](archive/hardware/2026-11-08-umi-pps-ams-retry.md)。
