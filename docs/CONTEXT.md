# 当前状态

2026-11-13｜`codex/wired-six-stage-power`；有线新策略本地提交，L3待验；聚焦主线接口，Debian后续。

WP10 使用 SN100T Linux NFC/NCI、标准 Netlink；RF/读卡/恢复 L3、eSE、Debian 服务待验，见[NFC记录](archive/research/2026-11-10-sn100t-linux-nci.md)。

有线新策略PPS30W目标/≤3A、PD9V/2A、80/71固定5V旁路复充。L1 242项零跳过、双L2/DT通过，新L3待验，见[记录](archive/reviews/2026-11-13-wired-policy.md)。旧PPS持续49分、低流回升及81%旁路L3保留，见[实机](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)；温标/硬件保护仍开放。

有线 SMB5 bit5 状态误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM 启动/Android 回退 L3 通过，见[真机读数](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

RAM/USB、隔离/UBWC、BQ probe 已 L3。旧镜像原生点屏 L3 保留；历史 `2bdb266` MDSS/SMMU 试验见[记录](archive/hardware/2026-11-11-umi-mdss-reset-smmu-attempt-l3.md)：PLL/MDSS 失败、SMMU 超时，未确认点亮，已回退 Android。UEFI 根/触控开放。

旁路及 PD/PPS 恢复见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)；watchdog/PPS 热保护 L3 见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。温标/保护仍开放；无线未移植。

## 近期优先级

1. WP10：新 NCI reset/init、RF/NDEF、失败恢复 L3，再集成 Debian；eSE 独立评估。
2. 收尾显示 SMMU 交接与独立供电/PM；有线温标/保护 L3；Debian、无线 RX/TX 后续推进。
3. RISK-004：硬件保护/温控标度、协议错误物理诱因、独立复核与精确 HEAD CI；时钟修复见[记录](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
