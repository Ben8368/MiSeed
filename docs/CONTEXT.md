# 当前状态

2026-11-18｜[真机复验](archive/hardware/2026-11-18-umi-display-nfc-ese-l3.md#csot-90hz-命令与-geni-阶段取证复验)：CSOT 90Hz命令已发，DRM回调89.846Hz、物理Hz待量；采集通过，FDT根身份仍FAIL，待EFI交接。

PR7：L0–L2过；固定PD L3部分过，BQ ADC/独立复核未闭。[记录](archive/reviews/2026-11-17-pr7-coderabbit.md)

固定PD `61c495d`：无APDO；71%转5V、80%旁路；输入峰1.986A/ICL≤2A。BQ ADC、保护/温控/回差待验。[L3](archive/hardware/2026-11-17-umi-fixed-pd-bq25970-l3.md)

WP10 11-19 通过初始化、MIFARE 寻卡和关闭，待验重开和 NDEF。[L3 记录](archive/hardware/2026-11-19-umi-nfc-init-retry.md#同镜像-mifare-classic-寻卡-l3)

旧HEAD有线PPS L3：SOC11–65%、请求≤3A后转5V、峰14.61W；温标/保护/断电回差开放，无线RX/TX独立。[记录](archive/hardware/2026-11-15-umi-wired-fixed-pd-l3.md)

SMB5 bit5修复，FG/SMB5 10组Charging、Android回退过。[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)


旧旁路、PD/PPS恢复及watchdog/热保护见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。

## 近期优先级

1. 固定PD/BQ ADC、热态回差、保护/温标、独立复核及精确HEAD CI：RISK-004。
2. WP10 验重开、NDEF 和 PM。eSE 按[回调和 VEN 门槛](archive/research/2026-11-18-sn100t-ese-ven.md)补证，禁共享 VEN 复位。
3. 显示刷新率矛盾、SMMU/PM；Debian与无线RX/TX。
