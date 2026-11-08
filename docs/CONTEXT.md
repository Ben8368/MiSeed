# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge` / [草稿 PR #3](https://github.com/Ben8368/MiSeed/pull/3)。采集归档已提交 `b88e81d`，该 HEAD 六项 CI 全过；本轮追加 PPS 阻塞 I/O 跨期检查，Linux 95/95、macOS 79 通过/16 跳过、ARM64 L2 成功，boot `fa72db0f…beb71`；新提交 CI 另查。见[续核与验证](archive/reviews/2026-11-08-pps-deadline-and-protection-review.md)。

既有 29 个参考 DTBO 隔离检查通过。TLMM 仅开放 GPIO34/35；PPS、Type-C/I2C15、VBUS 仍禁用，缺项拒绝 probe。**本轮没有 MiSeed 新拓扑或 PPS L3，不能声称快充可用。**

本机 root 定点读取泵 ID `0x10`、0x2b=`0xd0`（2 mΩ 配置，非实物测量）；GPIO8/68、PM8150B 连接器 ADC 0x4f、user_space 热区和 16 档 battery cooling 已观测。常规温控配置非文本，尚未解析；不能只搬泵驱动。已恢复普通 adbd；充电器为用户报告绿联45W屏显，APDO 未测。身份/契约见[平台](platforms.md)。

## 近期优先级

1. 独立复核 DTBO 隔离及 UART-only TLMM；另行授权后验证当前镜像的启动、唯一 SPMI/PON、USB 往返，RISK-010 / TD-002 未关闭。
2. 复用匹配 Android/公开实现，闭合原温控执行链、实板校准与 watchdog/VOUT_OVP；RISK-004 保留。
3. 用户已请求后续 RAM 启动及插拔快充测试；保护闭合并确认回退后才执行，不把授权当作安全证明。

## 最近验证

[续核、来源与 L1/L2](archive/reviews/2026-11-08-pps-deadline-and-protection-review.md)；[最初 Android 核查](archive/hardware/2026-11-08-android-root-power-audit.md)；[拓扑修复](archive/research/2026-11-08-pps-topology-followup.md)。
