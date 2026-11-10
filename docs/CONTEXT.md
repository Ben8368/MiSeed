# 当前状态

日期：2026-11-10。工作分支与 PR #4 源分支均为 `codex/nfc-wireless-charging`。RQ-14 目标为 UEFI + Linux 主线内核 + Debian；UEFI/交接仍按 WP3/WP4，例外归 TD-003–007。

WP10 已替换为 SN100T Linux NFC/NCI transport、binding 与标准 Netlink 消费者，本地 L2 通过，见[适配](archive/research/2026-11-10-sn100t-linux-nci.md)。新驱动 L3、Debian 读卡、eSE 与独立复核尚缺；旧 NQ probe/node L3 不沿用。

有线标准接口续修：修正 FG 电流符号和 SMB5 输入电流量纲，抽出 BQ 寄存器层，撤除 observer 虚构 DT；显示改 simpledrm/KMS。验证与边界见[续修](archive/reviews/2026-11-10-mainline-interface-followup.md)。设备未操作，无线 RX/TX 未移植。

源码职责拆分完成，本机 L0/L1 与 ARM64 L2 通过，篇幅红灯清零、8 项黄灯归 TD-008；验证边界见[拆分记录](archive/reviews/2026-11-10-source-responsibility-split.md)。

镜像 `44854093…e98a89f94c`：维护者宣告有线 PD 与 PPS 均已稳定，分别约 10 分钟真机、充电中无故障。PPS 连续 RUN 约 12 分钟、2400 条样本；固定 PD/SMB5 SOC 70%→83%、净充约 2.3–2.4 A，无 APDO 时 `error=-95` 为预期退出。见[稳定 L3](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。PR #3 待审查不合并；`4f923319…8540601f` 镜像 PPS 约 81 秒正常转 PD，见[复核](archive/reviews/2026-11-09-pr3-coderabbit-verification.md)。

## 近期优先级

1. WP10：新 NCI reset/init、RF/NDEF、失败恢复 L3，再集成 Debian；eSE 独立评估。
2. 新标准 ABI、simpledrm 和 DT 真机验收；有线温控标度/保护恢复、Debian 消费与独立复核。无线 RX/TX 独立设计。
3. 修复 Windows 串口导出丢行；继续旧 BQ 故障/间歇 `-ENOTCONN`、专项复核与精确 HEAD CI，见 RISK-004。
