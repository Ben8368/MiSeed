# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3，HEAD `18c8a65`。最新诊断镜像 SHA-256 `f13d367585f96486e665219fd956d35447cc20e7a1df8ace167e87241c34c313` 已在 Umi 上通过 `fastboot boot` RAM 启动。只读状态显示 Type-C `port0`/partner 存在，但 `/sys/bus/i2c/devices` 为空，`pps-status` 找不到控制器；USB supply 为 `Unknown [SDP] DCP CDP`，约 4.52 V / 0.55 A。日志有 PMIC Type-C hard-reset 记录。用户照片显示充电器端 PD 5.2 V / 2.8 A（约 14.6 W），不证明手机内核 PD 合约/PPS APDO 已建立。真机记录见[诊断镜像 L3](archive/hardware/2026-11-08-umi-pps-diagnostic-ramboot.md)。

固定 LOS Umi 参考仍为泵 ID `0x10`、sense 配置 `0xd0`、六路 thermal-FCC。当前工作树已移植 LOS 自动 SMB5/PPS、3 A / 6 A / 4.45 V、95% 退出与主充回退；BQ watchdog/OCP/TS comparator 按 LOS 配置。ARM64 L2 成功；完整 L3 保护与 PPS 结果仍在适配。

## 近期优先级

1. 定位 I2C15 控制器未注册的原因，核对运行时 GENI/I2C 平台设备与驱动绑定；同时分析 Type-C hard-reset 日志。
2. I2C15/BQ25970 成功绑定后，再验证设备侧 PD/PPS、保护/温控、停止/恢复与拔插；核实 GPIO8 是否需按 LOS 接入 SMB5 VBUS-disable。
3. 不刷写分区。状态及风险见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布) 与[审查记录](archive/reviews/2026-11-08-los-automatic-pps-port.md)。
