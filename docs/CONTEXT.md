# 当前状态

2026-11-18｜[显示/NFC/eSE](archive/hardware/2026-11-18-umi-display-nfc-ese-l3.md)：KMS报90Hz、vblank实测59.9Hz，显示专项未过；旧NFC记录不适用当前驱动；eSE未实现。

PR7：L0–L2过；固定PD L3部分过，BQ ADC/独立复核未闭。[记录](archive/reviews/2026-11-17-pr7-coderabbit.md)

固定PD `61c495d`：无APDO；71%转5V、80%旁路；输入峰1.986A/ICL≤2A。BQ ADC、保护/温控/回差待验。[L3](archive/hardware/2026-11-17-umi-fixed-pd-bq25970-l3.md)

WP10旧版NFC：reset/init、RF target、close/reopen通过；当前驱动有FIRM/VEN/CORE_RESET改动，旧证据不复用。NDEF/PM/HCE/eSE未验，禁VEN复位。[旧记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)

旧HEAD有线PPS L3：SOC11–65%、请求≤3A后转5V、峰14.61W；温标/保护/断电回差开放，无线RX/TX独立。[记录](archive/hardware/2026-11-15-umi-wired-fixed-pd-l3.md)

SMB5 bit5修复，FG/SMB5 10组Charging、Android回退过。[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)

显示90Hz移植过L2；当前vblank与KMS矛盾。交接、SMMU/PM和启动条纹待查。[L3](archive/hardware/2026-11-18-umi-display-nfc-ese-l3.md)

旧旁路、PD/PPS恢复及watchdog/热保护见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。

## 近期优先级

1. 固定PD/BQ ADC、热态回差、保护/温标、独立复核及精确HEAD CI：RISK-004。
2. WP10当前驱动NFC、NDEF/PM与eSE；查NACK，禁共享VEN复位。
3. 显示刷新率矛盾、SMMU/PM；Debian与无线RX/TX。
