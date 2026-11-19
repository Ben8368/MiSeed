# Umi 有线 80% 自动旁路与充电器重接 L3

| 项 | 证据 |
|---|---|
| 日期 / 设备 | 2026-11-19，Xiaomi Mi 10 / Umi / CSOT。维护者授权 RAM 启动并操作充电器换线、拔插 |
| 宿主 / 系统 | Windows x64 / PowerShell 7.6.9，Platform-Tools 37.0.1，pyserial 3.5。Android 16 → RAM Linux 6.18.60 |
| 源码 / 载荷 | `fb5f76c7e6adef9bde55f93cd4e41f0beab953e9`，boot.img SHA-256 `58692cf3963618e2d7af36fc57c4abbe0426c3ec42ff14f54143505f0a5bbf6b`。沿用[前一轮镜像及来源锁](2026-11-19-umi-wired-pps-bq25970-l3.md) |
| 操作边界 | 有线自然充电和一次同源拔插。无刷写、持久存储挂载、阈值修改、故障注入或无线 RX/TX 操作 |

## 操作与证据完整性

ADB 电量 78%。核实唯一 Bootloader 设备 `product=umi`、`unlocked=yes`、`is-userspace=no` 后执行一次 `fastboot boot`，退出 0，发送和启动均 OKAY。COM6 确认 Umi/CSOT、Linux 版本、RAM/虚拟挂载和 78%、28°C 的基线。

换接原有 45 W PPS 充电器，维护者报告功率降至约 2 W，拔线再接同一个充电器后仍稳定约 2 W。接回电脑后执行 `python -X utf8 tools/collect_charge_log.py --port COM6 --output .work/wired-auto-bypass-l3-20261119/charge-log.log --timeout 180`，再收取一次 `tcpm-log` 及只读状态。采集均退出 0。同 boot_id 的 RAM 时间线有 2,516 条连续记录，覆盖 0、存储错误 0，TCPM 环 270 行且 `complete=1`。

## 真机判定

| 子项 | 结果与证据 |
|---|---|
| 78%→80% 自动旁路 | **通过。** 有线源公布 APDO，手机在本轮高 SOC 工况使用固定 5 V / 3 A 合约。128.532 秒首次记录 SOC 80%、`full_bypass=1 enabled=0 input=1`。稳定段 93 组样本中，SMB5 输入功率中位 1.998 W，电池净电流中位 −2.441 mA |
| 80% 拔线并重接同一充电器 | **通过。** 332.625 秒 VBUS 断开，旁路锁清除。338.614 秒 VBUS 恢复，仍为 80%，旁路重新建立。339.009 秒确认固定 5 V / 3 A。重接后稳定段 23 组样本中，SMB5 输入功率中位 2.000 W，电池净电流中位 −2.441 mA |
| 在线 80/71 回差 | **待验。** 两段外部供电旁路期间均为 80%。保持输入连接时的 72–79% 锁定、≤71% 复充及再次到 80% 的完整循环仍缺真机证据 |

稳定段输入功率由同次 SMB5 `voltage_now` × `current_now` 计算。最后换接电脑 USB 的 SDP 窗口出现一组电池 `Charging` / +27.832 mA，下一组恢复 `Discharging`。两段充电器窗口的统计独立于电脑阶段。

采集后执行一次正常 `reboot`，ADB 核实 `umi`、Android 16 / SDK 36、`sys.boot_completed=1`。原始材料仅保留在忽略目录 `.work/wired-auto-bypass-l3-20261119/`。

| 材料 | SHA-256 |
|---|---|
| baseline.log | `8b141b5dfabeb8d3c0c8d31370d599aa5ce105a79145bebe80a80688992d434c` |
| charge-log.log | `2575b9721a22c220a5c4eb6ae4aab23e192424efe0b11e5d249cb7d51f60257b` |
| final-state.log | `f08c856a6b30d926de1121a33bb5e058a3a58cb0d8dbb2ad0ffa355c96b5f22e` |
