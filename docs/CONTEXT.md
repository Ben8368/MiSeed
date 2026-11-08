# 当前状态

日期：2026-11-08。`codex/umi-upstream-power-port` 新增显式 600 秒 SOC 豁免诊断入口并本地修复 PR #2 Windows Bash 测试误判；68 项 Linux 回归及新镜像 `793ca218…7030c5` L2 通过，维护者已授权更新 PR #2，并要求先测试采集数据；未提交/推送或上机。快充因 PD/PPS、GPIO68、采样与保护配置缺口阻断，见[本轮核查](archive/reviews/2026-11-08-fastcharge-ci.md)。

镜像 `10ec0d0e…d9ce7c4` 已 L2，Linux 66 项离线回归通过；本机 RAM 启动后，维护者完成电脑→充电器→电脑，导出 1004 条连续记录且重复导出一致。短时启充、拔出停充和恢复 70/65 策略已 L3；不代表异常保护、全程满充或长期供电通过。见[运行移植与实测](archive/hardware/2026-11-08-umi-runtime-power.md)。

设备仍在旧镜像 RAM Linux、电脑 USB 在线；本轮只读导出 4378 条，末样本 77%、34°C、停充。未关闭当前策略、未启新测试、未刷写或使用 adb root。原 Android 返回路径此前已验证，见[诊断往返](archive/hardware/2026-11-08-upstream-power-ramboot.md)。

## 近期优先级

1. 提交并推送已授权的 PR #2 修复，核实远端 CI；以 0.5 A、600 秒受限诊断入口采集插拔数据，不称快充。
2. 闭合快充 PD/PPS 控制、板级采样/保护与 GPIO 接线；消除重复 PMIC（RISK-010），锁定安全 GPIO 后恢复 TLMM（TD-002）。
3. 补齐 60/600 秒超时、SOC 边界、温控/过压/过流及 watchdog 真机证据和独立复核；RISK-004 仍限制长期供电/发布声明。

## 最近验证

[本轮 L1/L2/L3](archive/hardware/2026-11-08-umi-runtime-power.md)；[电池调查](archive/hardware/2026-11-08-mi10-battery-survey.md)；[来源锁](../manifests/sources.lock.json)。
