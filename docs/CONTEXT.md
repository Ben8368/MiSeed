# 当前状态

日期：2026-11-09。分支 `codex/umi-pd-pps-fastcharge`，PR #3 待审查、不合并。本地 `4f923319…8540601f` 已 RAM/USB/PPS L3，约 81 秒后按收尾条件转 PD；复核修复纳入 PR #3，见[本轮记录](archive/reviews/2026-11-09-pr3-coderabbit-verification.md)。

镜像 `44854093…e98a89f94c`：维护者宣告 PD 与 PPS 充电均已稳定——二者分别约 10 分钟真机测试均无充电中故障。PPS：连续 RUN 约 12 分钟、2400 条样本；固定 PD/SMB5：约 10 分钟、SOC 70%→83%、净充约 2.3–2.4 A（无 APDO 时 `error=-95` 为预期退出）。主机串口导出偶发丢行仍开放。见[PPS L3](archive/hardware/2026-11-09-umi-pps-windows-l2.md#长日志镜像-pps-稳定运行-l3)、[固定 PD L3](archive/hardware/2026-11-09-umi-pps-windows-l2.md#固定-pd-约-10-分钟验证-l3)。

## 近期优先级

1. 修复/检测 Windows 串口导出丢行，按时段采集。
2. 跟进旧镜像 BQ 故障与间歇性 `-ENOTCONN`，独立专项复核与精确 HEAD CI；PR 待审查，不合并。
3. 保护/停机路径与温度标度分项 L3，与已宣告稳定的 PD/PPS 运行分开记录。见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布)。
