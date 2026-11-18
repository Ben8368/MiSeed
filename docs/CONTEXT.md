# 当前状态

2026-11-18｜[显示/NFC/eSE](archive/hardware/2026-11-18-umi-display-nfc-ese-l3.md)：KMS报90Hz、vblank实测59.9Hz；GPIO50等待改为厂商约128ms，待复测。NFC当前驱动与05c4b74成功L3相同；eSE未实现。

PR7：L0–L2过；固定PD L3部分过，BQ ADC/独立复核未闭。[记录](archive/reviews/2026-11-17-pr7-coderabbit.md)

固定PD `61c495d`：无APDO；71%转5V、80%旁路；输入峰1.986A/ICL≤2A。BQ ADC、保护/温控/回差待验。[L3](archive/hardware/2026-11-17-umi-fixed-pd-bq25970-l3.md)

WP10 NFC/NCI：reset/init、RF target、close/reopen L3过，驱动与当前一致；NDEF/PM/HCE/eSE未验，禁VEN复位。[记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)

旧HEAD有线PPS L3：SOC11–65%、请求≤3A后转5V、峰14.61W；温标/保护/断电回差开放，无线RX/TX独立。[记录](archive/hardware/2026-11-15-umi-wired-fixed-pd-l3.md)

SMB5 bit5修复，FG/SMB5 10组Charging、Android回退过。[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)

显示90Hz移植过L2；实测约60Hz后将GPIO50等待改为厂商约128ms，待L2/L3复测。交接、SMMU/PM和启动条纹待查。[L3](archive/hardware/2026-11-18-umi-display-nfc-ese-l3.md)

旧旁路、PD/PPS恢复及watchdog/热保护见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。

## 近期优先级

1. 固定PD/BQ ADC、热态回差、保护/温标、独立复核及精确HEAD CI：RISK-004。
2. WP10当前驱动NFC、NDEF/PM与eSE；查NACK，禁共享VEN复位。
3. 显示刷新率矛盾、SMMU/PM；Debian与无线RX/TX。
