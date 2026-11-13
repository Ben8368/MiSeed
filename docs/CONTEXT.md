# 当前状态

2026-11-13｜`codex/wired-six-stage-power`；最新有线镜像接电场景 L3 通过，其他档位/保护另验。

WP10：SN100T/NCI 镜像L2、reset/init、RF target、close/reopen L3过；QUP1=1MHz，CORE_RESET有界重试。普通卡仅发现target；NDEF/PM/HCE/eSE未验。禁共享VEN；复位/NACK待查，见[记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)。

有线L3：精确HEAD `05c4b744` 镜像接电 SOC2→80%通过：2–10%/71–79%固定5V≤2A，11–65% PPS≤3A后干净退出续5V，温度趋势反馈、80%旁路（电池净流中位−2.4mA）。PPS峰值14.61W，非30W。稳定旁路无需降到71%；断电回差、无APDO、保护/温标另验；无线RX/TX独立。见[策略](archive/reviews/2026-11-13-wired-policy.md)和[日志](archive/hardware/2026-11-13-umi-wired-charge-l3-log-review.md)。

有线 SMB5 bit5 误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM启动/Android回退L3通过，见[记录](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

RAM/USB、隔离/UBWC、BQ probe 已 L3。旧镜像点屏L3保留；`2bdb266` MDSS/SMMU失败未点亮，已回退Android，见[记录](archive/hardware/2026-11-11-umi-mdss-reset-smmu-attempt-l3.md)。UEFI根/触控开放。

旧旁路、PD/PPS 恢复及 watchdog/热保护L3见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)；温标/保护开放，无线未移植。

## 近期优先级

1. WP10：验证只读 NDEF、PM、失败恢复并评估 eSE；Debian 用户态属后续阶段。NACK 时采集控制器阶段；禁共享 VEN 复位。
2. 显示SMMU交接/独立供电PM；有线温标保护L3；后续Debian及无线RX/TX。
3. RISK-004：保护/温标、错误诱因、独立复核、精确 HEAD CI；时钟见[记录](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
