# 当前状态

日期：2026-11-11。分支 `codex/native-dsi-dt-power`，基线 main `d9a65dc`；PR #4/#5 已合入。本阶段聚焦主线内核接口，Debian 集成后续推进。

WP10 使用 SN100T Linux NFC/NCI 与标准 Netlink；新驱动 RF/读卡/恢复 L3、eSE 和 Debian 服务待验，见[NFC 适配](archive/research/2026-11-10-sn100t-linux-nci.md)。

`b53705e` 已修正 BQ IRQ、拆分芯片后端；L1/L2、PPS 温控限流及固定 PD cooling 强制停充/释放 L3 已过。真实热触发/温度阈值与传感器异常未注入，见[有线验证](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。8 项源码黄灯归 TD-008。

有线 SMB5 bit5 状态误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM 启动/Android 回退 L3 通过，见[真机读数](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

RAM/USB、隔离/UBWC、BQ probe 已 L3。原生 DSI/DSC 1080×2340@60、1500 亮度命令/目视点屏及 Android 回退已 L3；MDSS reset 原生 L2/schema 通过、SMMU 待 L3，见[续修](archive/research/2026-11-11-native-dsi-chip-boundaries.md)；UEFI 根/触控开放。

有线旁路供电已实现并完成真机验证；镜像 `9f7534c1…1872f3` 的 PD/PPS、故障清理/恢复与禁止保持 L3 见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。既有 PD/PPS 约 10 分钟稳定结论保留；硬件保护专项归 RISK-004，无线 RX/TX 未移植。

## 近期优先级

1. WP10：新 NCI reset/init、RF/NDEF、失败恢复 L3，再集成 Debian；eSE 独立评估。
2. 收尾显示 SMMU 交接与独立供电/PM；有线温标/保护 L3；Debian、无线 RX/TX 后续推进。
3. RISK-004：硬件保护/温控标度、协议错误物理诱因、独立复核与精确 HEAD CI；时钟修复见[记录](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
