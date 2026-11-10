# 当前状态

日期：2026-11-10。本地工作分支与 PR #4 源分支均为 `codex/nfc-wireless-charging`。RQ-14 明确 UEFI + Linux 主线内核 + Debian；UEFI/交接仍按 WP3/WP4 实施。接口审计与治理更新见 [本轮审计](archive/reviews/2026-11-10-linux-interface-audit.md)，例外归 TD-003–007。被动 nfc-status 已过 L1/L2，本轮镜像未做 L3。

WP10 已将 NQ 私有字符设备替换为 SN100T Linux NFC/NCI transport、binding 和标准 Netlink 只读消费者，本地 L2 通过，见[适配记录](archive/research/2026-11-10-sn100t-linux-nci.md)。尚无新驱动 L3、Debian 读卡或独立复核，TD-003/RISK-011 仍开放；旧 NQ 的 probe/node L3 不沿用。设备未操作，无线 RX/TX 未移植。

镜像 `44854093…e98a89f94c`：维护者宣告有线 PD 与 PPS 均已稳定，二者分别约 10 分钟真机、充电中无故障。PPS 连续 RUN 约 12 分钟、2400 条样本；固定 PD/SMB5 SOC 70%→83%、净充约 2.3–2.4 A（无 APDO 时 `error=-95` 为预期退出）。见 [稳定 L3](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。PR #3 待审查不合并；`4f923319…8540601f` 镜像 PPS 约 81 秒按收尾条件转 PD，见 [复核](archive/reviews/2026-11-09-pr3-coderabbit-verification.md)。

## 近期优先级

1. 按 WP10 验证新 SN100T transport 的 reset/init、RF/NDEF 与失败恢复，再集成 Debian；eSE 独立评估。
2. 有线 power_supply/thermal/DT 源码迁移与本地验证见[本轮记录](archive/reviews/2026-11-10-wired-mainline-power.md)；待新镜像 L3、Debian 消费与独立复核。无线 RX/TX 独立设计。
3. 修复 Windows 串口导出丢行；继续旧 BQ 故障/间歇 `-ENOTCONN`、保护停机与温度标度专项、独立复核及精确 HEAD CI，见 RISK-004。
