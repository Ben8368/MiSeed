# 当前状态

2026-11-15｜`codex/fixed-pd-sampling`，基线`8d772e3`。固定PD L3捕获2.361A、停充/暂停及拔线前故障保持通过；持续充电仍中断。已回Android，见[记录](archive/hardware/2026-11-15-umi-fixed-pd-sampling-l3.md)。

WP10：SN100T/NCI 镜像L2、reset/init、RF target、close/reopen L3过；QUP1=1MHz，CORE_RESET有界重试。普通卡仅发现target；NDEF/PM/HCE/eSE未验。禁共享VEN；复位/NACK待查，见[记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)。

有线PPS：`05c4b744`既有L3，SOC11–65% PPS≤3A后续5V，峰值14.61W、非30W；未重测。固定PD阶段/80%旁路为[旧镜像证据](archive/hardware/2026-11-15-umi-wired-fixed-pd-l3.md)；[新L3异常](archive/hardware/2026-11-15-umi-fixed-pd-monitor-l3.md)已停充回Android。断电回差、保护/温标开放；无线RX/TX独立。

有线 SMB5 bit5 误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM启动/Android回退L3通过，见[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

显示：启动白/彩条仍现，维护者补报可能新增中间横条，接受本PR非阻断延期；稳定文字及1500既有通过。三种接管实验均撤回，未上机追踪已移除，手机已回Android16，见[续查](archive/reviews/2026-11-15-native-display-framebuffer.md)。首帧/PM开放。

旧旁路、PD/PPS 恢复及 watchdog/热保护L3见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)；温标/保护开放，无线未移植。

## 近期优先级

1. 有线固定PD：新镜像复验跳功率/输入监测；保护/温标、错误诱因、独立复核及精确HEAD CI归RISK-004。
2. WP10：只读NDEF、PM、失败恢复及eSE；NACK采控制器阶段，禁共享VEN复位；Debian后续。
3. 显示SMMU交接/独立供电PM；后续Debian及无线RX/TX。
