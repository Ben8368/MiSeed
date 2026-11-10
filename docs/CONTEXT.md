# 当前状态

日期：2026-11-10。工作分支 `codex/linux-standard-interfaces`，基线 main `d9a65dc`（PR #4/#5 已合入）。本轮只完善 Linux 内核接口；Debian 集成后续进行，验收在此分支继续。产品目标与 WP3/WP4 不变。

WP10 已替换为 SN100T Linux NFC/NCI transport、binding 与标准 Netlink 消费者，本地 L2 通过，见[适配](archive/research/2026-11-10-sn100t-linux-nci.md)。新驱动 L3、Debian 读卡、eSE 与独立复核尚缺；旧 NQ probe/node L3 不沿用。

本分支修正 BQ IRQ pinconf、扩展 BQ 芯片后端、补有线固定 PD 电池温度 thermal 停充；L1 184 项、ARM64 L2、定向 schema 通过，新行为未 L3，见[标准接口完善](archive/reviews/2026-11-10-linux-standard-interfaces.md)。8 项篇幅黄灯归 TD-008。

镜像 `aa5a4683…bd1b7f7`：有线**固定 PD**稳定约 10.3 分钟，SOC 81%→89%，充电中无故障；无 APDO 时 PPS 回落符合预期，见[PD 稳定 L3](archive/hardware/2026-11-10-umi-pd-wired-stable-l3.md)。`44854093…e98a89f94c` 的 PD/PPS 各约 10 分钟稳定结论仍有效，见[既有稳定 L3](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。

Windows `charge-log` 导出：主机专用读线程 + 8 MiB RX + 救援 pacing；修复镜像 `b4026d67…d44e9` 真机全量导出 **1102** 条、`gap_count=0`、`storage_errors=0`（`boot_id=f4152e76-…`）。继续旧 BQ/`-ENOTCONN`、保护标度与精确 HEAD CI，见 RISK-004。无线 RX/TX 未移植。

## 近期优先级

1. WP10：新 NCI reset/init、RF/NDEF、失败恢复 L3，再集成 Debian；eSE 独立评估。
2. 在本分支继续新 ABI、simpledrm/DT、有线温控标度/保护恢复 L3 与独立复核；Debian 后续集成，无线 RX/TX 独立设计。
3. 继续旧 BQ 故障/间歇 `-ENOTCONN`、专项复核与精确 HEAD CI，见 RISK-004。
