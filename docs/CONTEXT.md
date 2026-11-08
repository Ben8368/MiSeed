# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3。`e2ac23c4…c377`、`108f4474…360d`、`2e5ddc0f…e087a` 均已 Fastboot RAM 启动。第三份镜像日志确认 `qcom-rpmhpd` probe 以 `-95` 失败；固定内核里 `QCOM_RPMHPD` 未选择 `PM_OPP`，使 provider 所需的 OPP OF API 落到 `-EOPNOTSUPP` stub。已在源码暂存逻辑补依赖；修复镜像 L2 哈希 `9e065431…acfcea`，尚未 RAM 启动。检查时 Fastboot 未枚举设备。设备侧此前最新 `pps-status` 显示 RPMh RSC 已绑定 `rpmh`，但 `18200000.rsc:power-controller` 未绑定；I2C15 因此 deferred，I2C sysfs 为空、BQ25970 未绑定、PPS 控制器不可用。用户所见仍为 PD 而非 PPS。完整证据见[QUP/RPMh 续查](archive/hardware/2026-11-08-umi-pps-quruntime-followup.md)。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. Fastboot 设备重新枚举后 RAM 启动 `9e065431…acfcea`，核实 RPMh provider 与 I2C15 deferred-probe 是否恢复。
2. I2C15/BQ25970 成功绑定后，再验证设备侧 PD/APDO/PPS、保护/温控、停止/恢复与拔插；核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
