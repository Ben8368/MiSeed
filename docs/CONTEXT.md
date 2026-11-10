# 当前状态

日期：2026-11-11。分支 `codex/linux-standard-interfaces`，基线 main `d9a65dc`；PR #4/#5 已合入。本阶段聚焦主线内核接口，Debian 集成后续推进。

WP10 使用 SN100T Linux NFC/NCI 与标准 Netlink；新驱动 RF/读卡/恢复 L3、eSE 和 Debian 服务待验，见[NFC 适配](archive/research/2026-11-10-sn100t-linux-nci.md)。

`b53705e` 已修正 BQ IRQ、拆分芯片后端并完善有线固定 PD thermal；L1 184 项、ARM64 L2、定向 schema 通过；其余 thermal 行为 L3 待验，见[接口审查](archive/reviews/2026-11-10-linux-standard-interfaces.md)。8 项篇幅黄灯归 TD-008。

有线 SMB5 bit5 状态误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM 启动/Android 回退 L3 通过，见[真机读数](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

RAM 启动、USB CDC、simpledrm 与 Android 往返 L3 通过；UEFI、原生 DSI、触控未验，见[真机记录](archive/hardware/2026-11-10-umi-startup-usb-display-l3.md)。

有线 PD/PPS 既有约 10 分钟稳定结论保留；修复镜像 `9f7534c1…1872f3` 已过双 PPS 会话、故障清理/拔插恢复及固定 PD/禁止保持 L3，见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。硬件保护注入仍归 RISK-004，无线 RX/TX 未移植。

## 近期优先级

1. WP10：新 NCI reset/init、RF/NDEF、失败恢复 L3，再集成 Debian；eSE 独立评估。
2. 新 ABI、simpledrm/DT、有线温控标度/保护恢复 L3 与独立复核；Debian 后续集成，无线 RX/TX 独立设计。
3. RISK-004：硬件保护/温控标度、协议错误物理诱因、独立复核与精确 HEAD CI；时钟修复见[记录](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
