# 当前状态

2026-11-15｜`codex/linux-standard-interfaces`；收尾基线 `f349fec9`。固定PD输入监测/旁路故障锁已修，L1/L2过、L3待验，见[记录](archive/reviews/2026-11-15-fixed-pd-input-monitor.md)。

WP10：SN100T/NCI 镜像L2、reset/init、RF target、close/reopen L3过；QUP1=1MHz，CORE_RESET有界重试。普通卡仅发现target；NDEF/PM/HCE/eSE未验。禁共享VEN；复位/NACK待查，见[记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)。

有线PPS快充：精确 HEAD `05c4b744` 的既有 L3，SOC11–65% PPS≤3A 后退出续5V，PPS峰值14.61W，非30W；本轮未重测。最新固定PD L3与限流异常见[本轮记录](archive/hardware/2026-11-15-umi-wired-fixed-pd-l3.md)。80%在线旁路已有本轮固定PD证据；断电回差、硬件保护/温标仍开放；无线RX/TX独立。

有线 SMB5 bit5 误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM启动/Android回退L3通过，见[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

显示：启动白/彩条仍现，维护者补报可能新增中间横条，接受本PR非阻断延期；稳定文字及1500既有通过。三种接管实验均撤回，未上机追踪已移除，手机已回Android16，见[续查](archive/reviews/2026-11-15-native-display-framebuffer.md)。首帧/PM开放。

旧旁路、PD/PPS 恢复及 watchdog/热保护L3见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)；温标/保护开放，无线未移植。

## 近期优先级

1. WP10：验证只读 NDEF、PM、失败恢复并评估 eSE；Debian 用户态属后续阶段。NACK 时采集控制器阶段；禁共享 VEN 复位。
2. 显示SMMU交接/独立供电PM；有线温标保护L3；后续Debian及无线RX/TX。
3. RISK-004：保护/温标、错误诱因、独立复核、精确 HEAD CI；时钟见[记录](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
