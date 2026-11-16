# 当前状态

2026-11-16｜[Linux接口收尾](archive/reviews/2026-11-16-linux-interface-closure.md)：撤私有控制并修限流；283项Linux零跳过，双配置schema/L2过。

2026-11-17｜[PPS+90Hz](archive/hardware/2026-11-17-umi-pps-90hz-l3.md)：HEAD `cb59fc8`；PPS累计10:42、三段/两次拔插，非连续无故障。KMS 1080×2340@90、目视正常，vblank未测；已回Android16。

有线固定PD：9V/3A ADC与43,972条样本无故障；SOC71%降5V、80%止充。无APDO误触PPS已修。[记录](archive/hardware/2026-11-16-umi-fixed-pd-l3.md)

WP10 SN100T/NCI：reset/init、RF target、close/reopen L3过；NDEF/PM/HCE/eSE未验。禁共享VEN，NACK待查。[L3](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)

旧HEAD有线PPS L3：SOC11–65%、请求≤3A后转5V、峰值14.61W；新HEAD见本轮记录。温标/保护、断电回差开放；无线RX/TX独立。[旧记录](archive/hardware/2026-11-15-umi-wired-fixed-pd-l3.md)

SMB5 bit5误判修复，FG/SMB5 10组Charging，Android回退过。[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)

显示：[90Hz移植](archive/research/2026-11-16-display-90hz-port.md)过L2/schema；新HEAD KMS/目视正常，vblank待测；SMMU/PM与启动条纹续查。[L3](archive/hardware/2026-11-17-umi-pps-90hz-l3.md)

旧旁路、PD/PPS恢复和watchdog/热保护见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。

## 近期优先级

1. 有线固定PD新镜像L3、跳功率闭环；保护/温标、独立复核、精确HEAD CI见RISK-004。
2. WP10 NDEF、PM、恢复及eSE；NACK查控制器阶段，禁共享VEN复位。
3. 显示SMMU交接/独立供电PM；Debian与无线RX/TX。
