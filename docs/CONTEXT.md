# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3。诊断镜像 `f13d3675…c313` 已 RAM 启动：Type-C `port0`/partner 存在，但 `/sys/bus/i2c/devices` 为空，`pps-status` 找不到控制器；USB supply 为 `Unknown [SDP] DCP CDP`，约 4.52 V / 0.55 A，日志有 PMIC Type-C hard-reset。根因定位为 Linux DT 的 QUPv3 父节点 `qupv3_id_2`（`geniqup@8c0000`）为 `disabled`，尽管 I2C15 子节点为 `okay`。PPS profile 已补启用父 wrapper，并在 L2 检查仅 I2C15/BQ 子节点启用；修正镜像 SHA-256 `e2ac23c4aefe85aa4a612a18fdc75873a1211f34103f0833d87df8832f88c377`，尚未真机启动。用户照片显示充电器端 PD 5.2 V / 2.8 A，不证明设备侧 PD/PPS 合约。真机记录见[诊断镜像 L3](archive/hardware/2026-11-08-umi-pps-diagnostic-ramboot.md)。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. 设备回到 Fastboot 后 RAM 启动修正镜像，核实 QUP2/I2C15 与 BQ25970 运行时绑定，并分析 Type-C hard-reset 日志。
2. I2C15/BQ25970 成功绑定后，再验证设备侧 PD/PPS、保护/温控、停止/恢复与拔插；核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
