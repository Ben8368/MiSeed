# 当前状态

2026-11-16｜有线固定PD L3：9V/3A、ADC过，43,972条无故障；SOC71%降5V、80%止充。无APDO误触PPS短停已修，新镜像Linux全280项L2过，真机待验。见[记录](archive/hardware/2026-11-16-umi-fixed-pd-l3.md)/[修复](archive/research/2026-11-16-pd-adc5-sequence.md)。

WP10 SN100T/NCI：L2及reset/init、RF target、close/reopen L3过；普通卡仅发现target，NDEF/PM/HCE/eSE未验。禁共享VEN；NACK待查。[L3](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)

有线PPS `05c4b744` L3：SOC11–65%、≤3A后转5V，峰值14.61W；未重测。固定PD阶段及80%旁路见[记录](archive/hardware/2026-11-15-umi-wired-fixed-pd-l3.md)。温标/保护、断电回差开放；无线RX/TX独立。

有线SMB5 bit5误判已修，10组FG/SMB5均Charging，Android回退L3过。[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)

显示：[90Hz移植](archive/research/2026-11-16-display-90hz-port.md)已过L2/schema、Linux281项，GPIO50/实际刷新待L3。稳定文字/1500既有通过；启动白/彩条与中线延期，Android16已恢复。[续查](archive/reviews/2026-11-15-native-display-framebuffer.md)

旧旁路、PD/PPS恢复和watchdog/热保护见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。

## 近期优先级

1. 有线固定PD新镜像L3、跳功率闭环；保护/温标、独立复核、精确HEAD CI见RISK-004。
2. WP10 NDEF、PM、恢复及eSE；NACK查控制器阶段，禁共享VEN复位。
3. 显示SMMU交接/独立供电PM；后续Debian与无线RX/TX。
