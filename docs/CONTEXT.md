# 当前状态

日期：2026-11-12。分支 `codex/linux-standard-interfaces`，基线 main `d9a65dc`；PR #4/#5 已合入。本阶段聚焦主线内核接口，Debian 集成后续推进。

WP10 使用 SN100T Linux NFC/NCI 与标准 Netlink；新驱动 RF/读卡/恢复 L3、eSE 和 Debian 服务待验，见[NFC 适配](archive/research/2026-11-10-sn100t-linux-nci.md)。

有线分段 L3 核实6→38%、温升降额保持PPS；81%/自动满电旁路待验，见[有线记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。保持实测策略，已回Android；CI apt修复本地240项/L2通过，授权推送PR6。温标开放，8项篇幅黄灯归TD-008。

有线 SMB5 bit5 状态误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM 启动/Android 回退 L3 通过，见[真机读数](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

RAM/USB、隔离/UBWC、BQ probe 已 L3。旧镜像原生点屏 L3 保留；HEAD `2bdb266` MDSS/SMMU 试验见[记录](archive/research/2026-11-11-native-dsi-chip-boundaries.md)：PLL/MDSS 失败、SMMU 超时，未确认点亮，已回退 Android。UEFI 根/触控开放。

旁路及 PD/PPS 恢复见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)；watchdog/PPS 热保护 L3 见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。温标/保护仍开放；无线未移植。

## 近期优先级

1. WP10：新 NCI reset/init、RF/NDEF、失败恢复 L3，再集成 Debian；eSE 独立评估。
2. 收尾显示 SMMU 交接与独立供电/PM；有线温标/保护 L3；Debian、无线 RX/TX 后续推进。
3. RISK-004：硬件保护/温控标度、协议错误物理诱因、独立复核与精确 HEAD CI；时钟修复见[记录](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
