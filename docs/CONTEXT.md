# 当前状态

日期：2026-11-08。已同步并切换 `codex/umi-pd-pps-fastcharge`，HEAD `c7b6b48`，包含 main `109225e`；[草稿 PR #3](https://github.com/Ben8368/MiSeed/pull/3) 未合并。当前 HEAD 远端六项 CI 含 Boot L2 全部成功；本轮 macOS 95 项回归 79 通过、16 项 Linux 专属跳过，见[Android root 核查](archive/hardware/2026-11-08-android-root-power-audit.md)。

WSL 95 项回归全过；Windows 51 通过、44 跳过；ARM64 L2 通过。最终 boot SHA-256 `b2068513…1770f0`；29 个参考 DTBO 独立叠加后主线 SPMI/TLMM 未变。TLMM 源码仅开放 GPIO34/35；PPS、Type-C/I2C15、VBUS 仍禁用，板级参数缺失拒绝 probe。**没有本次 PMIC/TLMM 或 PPS 新 L3，不可声称快充可用。** CI 按精确 PR HEAD 核验。

本轮按授权完成 Android root 只读采集并恢复普通 adbd。硬件版本 2.9.1；实际 DT 确认泵地址 0x66、GPIO8/68，但缺少 sense/BAT_OCP，存在温控关闭项；启动 ID 日志已不可得，当前缓存仅固定 5 V/3 A。身份/契约见[平台](platforms.md)；不关闭芯片、校准、watchdog 或快充准入缺项。

## 近期优先级

1. 独立复核 DTBO 隔离及 UART-only TLMM；另行授权后验证当前镜像的启动、唯一 SPMI/PON、USB 往返，RISK-010 / TD-002 未关闭。
2. 优先复用 Android 既有日志与匹配板级资料，补齐泵 ID、GPIO8/68、采样/热敏参数及 watchdog/VOUT_OVP 完整语义；RISK-004 保留。
3. 获得明确授权及回退方案后，分阶段补齐 PD/PPS、60/600 秒、SOC 与保护故障真机证据；不继承来源内核验收。

## 最近验证

[Android 核查与 CI](archive/hardware/2026-11-08-android-root-power-audit.md)；[拓扑 L1/L2](archive/research/2026-11-08-pps-topology-followup.md)；[既有受限 L3](archive/reviews/2026-11-08-fastcharge-ci.md)。
