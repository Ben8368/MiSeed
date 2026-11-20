# 当前状态

2026-11-18｜[真机复验](archive/hardware/2026-11-18-umi-display-nfc-ese-l3.md#csot-90hz-命令与-geni-阶段取证复验)：CSOT 90Hz命令已发，DRM回调89.846Hz、物理Hz待量；采集通过，FDT根身份仍FAIL，待EFI交接。

PR7：L0–L2过，固定PD L3部分过，独立复核未闭。[记录](archive/reviews/2026-11-17-pr7-coderabbit.md)

固定PD `61c495d`：无APDO，71%转5V、80%旁路，输入峰1.986A/ICL≤2A；热恢复待验。[L3](archive/hardware/2026-11-17-umi-fixed-pd-bq25970-l3.md)

WP10 11-19 实体 MIFARE 寻卡及重开通过，无卡停轮询地址 NACK 重试恢复。[L3](archive/hardware/2026-11-19-umi-nfc-init-retry.md#无卡停止轮询地址-nack-恢复)

有线PPS `fb5f76c`：BQ ADC及启停过、71%转固定5V续至78%；IRQ/保护/温标/旁路回差开放。[L3](archive/hardware/2026-11-19-umi-wired-pps-bq25970-l3.md)

同镜像80%旁路及同源拔插恢复通过，两段约2W；在线80/71回差待验。[L3](archive/hardware/2026-11-19-umi-wired-auto-bypass-l3.md)

SMB5 bit5修复，FG/SMB5 10组Charging、Android回退过。[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)


旧旁路、PD/PPS恢复及watchdog/热保护见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。

## 近期优先级

1. 固定PD热恢复与低功率、BQ IRQ、保护/温标、旁路回差及独立复核、精确HEAD CI（RISK-004）。
2. WP10 验 NDEF 和 PM，补地址 NACK 波形。eSE 按[回调和 VEN 门槛](archive/research/2026-11-18-sn100t-ese-ven.md)补证，禁共享 VEN 复位。
3. 显示刷新率、SMMU/PM；Debian；[无线RX](archive/hardware/2026-11-20-umi-rx-pinctrl-l3.md)诊断绑定成功，PG=0；双输入待验。
