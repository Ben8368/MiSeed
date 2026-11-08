# 当前状态

日期：2026-11-08。PR #2 的源码 HEAD `c8bb079` 修复 Windows Bash 误判并增加显式 600 秒 SOC 豁免入口；68 项 Linux 回归通过，新镜像 `793ca218…7030c5` 本地/hosted L2 一致。包含此前文档更新的 HEAD `ba9172e` 六项约定 CI 均成功；代码和镜像自 `c8bb079` 后未变。CI 记录与后续真机实测见[本轮核查](archive/reviews/2026-11-08-fastcharge-ci.md)。

本机新镜像 RAM 启动及 600 秒准备完成；维护者提前结束，日志观测约 25.5 秒受限启充、拔出停充、78% 电量下恢复正常策略。1524 条连续记录、覆盖 0、复读一致；温度 36°C，23 条稳定 FG 电流中位 −85,449 µA。不代表 600 秒超时、异常保护或快充已通过。

设备仍在本次 RAM Linux、电脑 USB 在线；单次测试结束，额外 `charge-stop` 已成功，保持停充。未刷写或使用 adb root；原 Android 返回路径此前已验证，见[诊断往返](archive/hardware/2026-11-08-upstream-power-ramboot.md)。

## 近期优先级

1. 闭合快充 PD/PPS 控制、板级采样/保护与 GPIO 接线；当前不直接启泵，RISK-004 保留。
2. 消除重复 PMIC（RISK-010），锁定安全 GPIO 后恢复 TLMM（TD-002）。
3. 补齐 60/600 秒到期、SOC 边界、温控/过压/过流、watchdog 真机证据和独立复核；不把短时插拔扩大为长期验收。

## 最近验证

[本轮 L1/L2/L3 与 CI](archive/reviews/2026-11-08-fastcharge-ci.md)；[此前受限 L3](archive/hardware/2026-11-08-umi-runtime-power.md)；[来源锁](../manifests/sources.lock.json)。
