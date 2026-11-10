# 当前状态

日期：2026-11-10。本地工作分支 `codex/wired-mainline-power`；本轮按授权交付 PR #4（远端 `codex/nfc-wireless-charging`）。RQ-14 明确 UEFI + Linux 主线内核 + Debian；UEFI/交接仍按 WP3/WP4 实施。接口审计与治理更新见 [本轮审计](archive/reviews/2026-11-10-linux-interface-audit.md)，例外归 TD-003–007。被动 nfc-status 已过 L1/L2，本轮镜像未做 L3。

WP10 既有 NQ-NCI 内核/DT 已过 L2 与 RAM boot probe/node L3；尚未接入 Linux NFC/NCI，不能认定内核功能完成或只剩 Debian。2026-11-10 ADB 只读核对了 Android HAL/固件/eSE 基线，设备保持 Android、已恢复普通 ADB。后续按 [Linux NFC](NFC.md) 迁移，RISK-011 开放；无线 RX/TX 尚未移植。

镜像 `44854093…e98a89f94c`：维护者宣告有线 PD 与 PPS 均已稳定，二者分别约 10 分钟真机、充电中无故障。PPS 连续 RUN 约 12 分钟、2400 条样本；固定 PD/SMB5 SOC 70%→83%、净充约 2.3–2.4 A（无 APDO 时 `error=-95` 为预期退出）。见 [稳定 L3](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。PR #3 待审查不合并；`4f923319…8540601f` 镜像 PPS 约 81 秒按收尾条件转 PD，见 [复核](archive/reviews/2026-11-09-pr3-coderabbit-verification.md)。

## 近期优先级

1. 按 WP10 评估 SN100T 并接入 Linux NFC/NCI；以标准接口进入 Debian，eSE 独立评估。
2. 有线 power_supply/thermal/DT 源码迁移与本地验证见[本轮记录](archive/reviews/2026-11-10-wired-mainline-power.md)；待新镜像 L3、Debian 消费与独立复核。无线 RX/TX 独立设计。
3. 修复 Windows 串口导出丢行；继续旧 BQ 故障/间歇 `-ENOTCONN`、保护停机与温度标度专项、独立复核及精确 HEAD CI，见 RISK-004。
