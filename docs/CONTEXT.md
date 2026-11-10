# 当前状态

日期：2026-11-10。工作分支与 PR #4 源分支均为 `codex/nfc-wireless-charging`。RQ-14 目标为 UEFI + Linux 主线内核 + Debian；UEFI/交接仍按 WP3/WP4，例外归 TD-003–007。

WP10 已替换为 SN100T Linux NFC/NCI transport、binding 与标准 Netlink 消费者，本地 L2 通过，见[适配](archive/research/2026-11-10-sn100t-linux-nci.md)。新驱动 L3、Debian 读卡、eSE 与独立复核尚缺；旧 NQ probe/node L3 不沿用。

源码职责拆分完成，本机 L0/L1 与 ARM64 L2 通过，篇幅红灯清零、8 项黄灯归 TD-008；验证边界见[拆分记录](archive/reviews/2026-11-10-source-responsibility-split.md)。

镜像 `aa5a4683…bd1b7f7`（HEAD `75e0e38`）：维护者宣告有线**固定 PD**稳定——约 10.3 分钟真机、SOC 81%→89%、中段约 1.9–2.0 A、33–35°C、充电中无故障；源端无 APDO 时 PPS `error=-11` 回落为预期。见[PD 稳定 L3](archive/hardware/2026-11-10-umi-pd-wired-stable-l3.md)。既有镜像 `44854093…e98a89f94c` 的 PD/PPS 各约 10 分钟稳定结论仍有效，见[既有稳定 L3](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。

Windows `charge-log` 导出：主机专用读线程 + 8 MiB RX + 救援 pacing；修复镜像 `b4026d67…d44e9` 真机全量导出 **1102** 条、`gap_count=0`、`storage_errors=0`（`boot_id=f4152e76-…`）。继续旧 BQ/`-ENOTCONN`、保护标度与精确 HEAD CI，见 RISK-004。无线 RX/TX 未移植。

## 近期优先级

1. WP10：新 NCI reset/init、RF/NDEF、失败恢复 L3，再集成 Debian；eSE 独立评估。
2. 新标准 ABI、simpledrm 和 DT 真机验收；有线温控标度/保护恢复、Debian 消费与独立复核。无线 RX/TX 独立设计。
3. 继续旧 BQ 故障/间歇 `-ENOTCONN`、专项复核与精确 HEAD CI，见 RISK-004。
