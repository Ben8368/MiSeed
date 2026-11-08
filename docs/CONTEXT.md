# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge` / [草稿 PR #3](https://github.com/Ben8368/MiSeed/pull/3)。初次采集已独立归档并提交；本轮移植 LOS 常规温控、补齐五路 PMIC ADC，Linux 95/95、macOS 79 通过/16 跳过、ARM64 L2 成功，boot `6789b4e7…c9a882`；新提交 CI 另查。见[温控移植与证据](archive/research/2026-11-08-los-thermal-port.md)。

既有 29 个参考 DTBO 隔离检查通过。TLMM 仅开放 GPIO34/35；PPS、Type-C/I2C15、VBUS 仍禁用，缺项拒绝 probe。**本轮没有 MiSeed 新拓扑或 PPS L3，不能声称快充可用。**

本机泵 ID `0x10`、0x2b=`0xd0`（2 mΩ 配置，非实物测量）；GPIO8/68 已观测。LOS 温控服务运行、profile=0，无覆盖配置；动作编码及 FCC 映射已查清，移植六路虚拟温度/滞回，未知屏幕状态取严格档位。已恢复普通 adbd；充电器为用户报告绿联45W屏显，APDO 未测。身份/契约见[平台](platforms.md)。

## 近期优先级

1. 独立复核 DTBO 隔离及 UART-only TLMM；另行授权后验证当前镜像的启动、唯一 SPMI/PON、USB 往返，RISK-010 / TD-002 未关闭。
2. 复核已移植温控的实机单位/执行效果，补齐实板校准与 watchdog/VOUT_OVP；RISK-004 保留。
3. 用户已请求后续 RAM 启动及插拔快充测试；保护闭合并确认回退后才执行，不把授权当作安全证明。

## 最近验证

[温控、来源与 L1/L2](archive/research/2026-11-08-los-thermal-port.md)；[最初 Android 核查](archive/hardware/2026-11-08-android-root-power-audit.md)；[拓扑修复](archive/research/2026-11-08-pps-topology-followup.md)。
