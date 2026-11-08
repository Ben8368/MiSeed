# 当前状态

日期：2026-11-08。在 `codex/umi-upstream-power-port` 完成 FG/SMB5/ADC5 运行移植与一次受限充电插拔验证；原作者、GPL 归属和固定来源保留。维护者已授权将本轮实现和证据更新到 PR #2；不合并。

镜像 `10ec0d0e…d9ce7c4` 已 L2，Linux 66 项离线回归通过；本机 RAM 启动后，维护者完成电脑→充电器→电脑，导出 1004 条连续记录且重复导出一致。短时启充、拔出停充和恢复 70/65 策略已 L3；不代表异常保护、全程满充或长期供电通过。见[运行移植与实测](archive/hardware/2026-11-08-umi-runtime-power.md)。

设备仍在本次 RAM Linux，电脑 USB 在线；测试已结束，当前停充，日志留在 RAM。未刷写、未使用 adb root。原 Android 返回路径此前已验证，见[诊断往返](archive/hardware/2026-11-08-upstream-power-ramboot.md)。

## 近期优先级

1. 补齐 60 秒超时、SOC 70/65 边界、温控/过压/过流及 watchdog 故障路径证据和独立复核；RISK-004 仍限制长期供电/发布声明。
2. 消除 DTBO 重复 PMIC（RISK-010），锁定安全 GPIO 后恢复 TLMM（TD-002）。
3. 继续保留可追溯构建和 RAM 日志；分项推进其他硬件能力，不把短时充电单例扩大成完整设备验收。

## 最近验证

[本轮 L1/L2/L3](archive/hardware/2026-11-08-umi-runtime-power.md)；[电池调查](archive/hardware/2026-11-08-mi10-battery-survey.md)；[来源锁](../manifests/sources.lock.json)。
