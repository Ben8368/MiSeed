# 当前状态

日期：2026-11-08。在干净 main `109225e` 上新建 `codex/umi-pd-pps-fastcharge`；LineageOS `lineage-23.2` 锁定 `71b13e62f057a649b77fe4062feb73ee72ad609c`。PPS/BQ25970 控制、SMB5 独占、受控测试及日志已完成源码适配和 L2；用户已授权提交、推送并创建草稿 PR；远端 CI 尚待本分支提交触发。详见[本轮移植](archive/research/2026-11-08-lineage-pps-port.md)。

WSL 81 项回归全过；Windows 42 通过、39 跳过。最终 L2 `boot.img` SHA-256 `d2d52ba8…76ad1c51`。快充、Type-C/I2C15、TLMM 与 VBUS 供给节点均禁用；板级参数缺失则拒绝 probe。**尚无 MiSeed 快充 L3，不可启用或声称快充可用。**

上一轮仅记录约 25.5 秒受限启充/拔出停充及 1524 条连续日志；600 秒期限未实测。最后记录已额外停充；本轮未连接设备、未 RAM 启动或修改供电。历史 PR #2 的修复/CI 和未合并结论见[复核](archive/reviews/2026-11-08-pr2-coderabbit.md)，不代替本分支证据。

## 近期优先级

1. 核实实际泵 ID、采样/热敏/保护参数与 watchdog 完整语义，完成 PPS 专项复核；RISK-004 保留。
2. 消除重复 PMIC（RISK-010），锁定 GPIO/供电路径后恢复 TLMM（TD-002）及 PD PHY 接线。
3. 获得明确授权及回退方案后，分阶段补齐 60/600 秒、SOC、温控/过压/过流、watchdog 与 PPS 真机证据；不继承来源内核验收。

## 最近验证

[本轮 L0/L1/L2](archive/research/2026-11-08-lineage-pps-port.md)；[既有受限 L3](archive/reviews/2026-11-08-fastcharge-ci.md)；[来源锁](../manifests/sources.lock.json)。
