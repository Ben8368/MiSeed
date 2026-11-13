# 当前状态

2026-11-13｜`codex/wired-six-stage-power`；新有线策略待L3。

WP10：SN100T/NCI 新镜像 L2、真机 reset/init、RF target、close/reopen L3 通过；Android DT 确认 QUP1=1MHz，Linux 已匹配并加 CORE_RESET 有界重试。普通卡仅发现 target；NDEF、PM、HCE/eSE 未验。Debian 后续。无共享 VEN 脉冲；复位规范/NACK 根因待查，见[记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)。

有线新策略：PPS30W/≤3A、PD9V/2A、80/71固定5V旁路复充。L1 242项零跳过、双L2/DT通过，新L3待验，见[记录](archive/reviews/2026-11-13-wired-policy.md)。旧PPS、低流回升、81%旁路L3保留，见[实机](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)；温标/保护开放。

有线 SMB5 bit5 误判已修；fullon 后 10 组 FG/SMB5 均 Charging，RAM启动/Android回退L3通过，见[读数](archive/hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。

RAM/USB、隔离/UBWC、BQ probe 已 L3。旧镜像原生点屏 L3 保留；`2bdb266` MDSS/SMMU 见[记录](archive/hardware/2026-11-11-umi-mdss-reset-smmu-attempt-l3.md)：PLL/MDSS 失败、SMMU 超时，未确认点亮，已回退 Android。UEFI 根/触控开放。

旁路、PD/PPS 恢复与 watchdog/热保护 L3 见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。温标/保护开放；无线未移植。

## 近期优先级

1. WP10：验证只读 NDEF、PM、失败恢复并评估 eSE；Debian 用户态属后续阶段。NACK 时采集控制器阶段；禁共享 VEN 复位。
2. 收尾显示 SMMU 交接与独立供电/PM；有线温标/保护 L3；Debian、无线 RX/TX 后续推进。
3. RISK-004：保护/温标、错误诱因、独立复核、精确 HEAD CI；时钟见[记录](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
