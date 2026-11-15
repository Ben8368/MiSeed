# Umi 有线固定 PD L3（最新源码）

| 项 | 本轮记录 |
|---|---|
| 日期 / 操作者 | 2026-11-15；维护者授权，Codex 协助 RAM 启动与日志分析 |
| 授权范围 | 固定 PD L3；仅一次 `fastboot boot`、充电过程日志记录与只读导出；不刷写、不控制充电阈值、不操作无线 RX/TX |
| 设备 | Mi 10 / Umi / CSOT；ADB `ro.product.device=umi`；Fastboot `product=umi`、`unlocked=yes`、`is-userspace=no`；不记录序列号 |
| 主机 / 工具 | Darwin 25.6.0 arm64 / zsh；Android Platform Tools 37.0.2 |
| 仓库 / 输入身份 | `codex/linux-standard-interfaces`，HEAD `f349fec9f6f1562eead915bdc61ae4252f85226f`；来源锁 SHA-256 `14010bd1cf7e44bd83c41e92b94468ef0b9b23c8687842976f3ae41de19f9b98` |
| 镜像 | `.work/boot-l2-arm64/boot.img`，SHA-256 `a85ff7a4c6752836995e1d1c6a5690741be1ad6a0672c7cf740e69e64f0c86f5`；启动后复算一致 |
| 启动 / 回退 | `fastboot boot` Sending/Booting 均 OKAY，退出 0；一次性 RAM 启动，未写分区 |
| 原始日志 | `.work/fixed-pd-l3-20261115/pd-charge.log`，6,138,288 bytes，SHA-256 `e35958592f41c577f645dc3890a99f4643003b97cb252c1924f9e65ba5924f61`；不入库 |
| TCPM 导出 | `.work/fixed-pd-l3-20261115/final-tcpm-log.txt`，311 行、`complete=1`，SHA-256 `c60515ca0e45be91e5a712e275499124f644162f074e3327bbebe731aae1815f`；只读导出一次并消费协议环 |

## 执行与完整性

- `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/fixed-pd-l3-20261115/pd-charge.log --timeout 600`：退出 0；`boot_id=10b18d77-8afe-4abc-b495-56ced64ed428`，`first=0 next=count=21051`，序号 0–21050 连续，`overwritten=0`、`storage_errors=0`、`ram_only=1`。
- 记录从 `boot_ms=1550` 至 `5590203`（约 93 分 09 秒）；三个 power_supply 每种各 2,683 个样本。后接电脑的 SDP 段与充电器 PD 段分开分析。
- 只读控制台确认 Linux 6.18.60，挂载只有 rootfs、devtmpfs、proc、sysfs、debugfs、configfs；没有真实存储挂载。
- `capture_console.py --port /dev/cu.usbmodem101 --tag final --commands version status mounts thermal pps-status tcpm-log`：退出 0。启动后的 `status`/`pps-status` 已在电脑接回后读取，电脑 SDP 的 5 V/3 A 不作为充电器合约证据。

## 固定 PD 实测

实际充电器于 TCPM 38.080 秒连接，5449.529 秒断开，约 90 分 11 秒。Source_Capabilities 是五个固定 PDO：5 V/3 A、9 V/3 A、12 V/3 A、15 V/3 A、20 V/2.25 A；无 APDO。

| SOC / 路径 | 协议与输入实测 | 判定 |
|---|---|---|
| 6–10%，固定 5 V | Request PDO0 5 V/3 A；SMB5 185 组样本，输入限额 1.95–2.00 A，实测中位 1.983 A、峰值 1.984 A；电池样本均 Charging | 本次覆盖范围通过；不外推 0–5% |
| 约 11–70%，固定 9 V | Request PDO1 9 V/2 A，进入 `SNK_READY`；SMB5 1,732 组样本，输入电压 8.611–9.063 V，输入限额 1.70–2.00 A，输入电流中位 1.884 A、P90 1.983 A | 固定 PDO 协商与运行通过；SOC 边界在 10→11% 过渡，记录分辨率不足以确定精确切换采样 |
| 71–79%，固定 5 V | Request PDO0 5 V/3 A；SMB5 444 组样本，输入限额 1.85–2.00 A，实测中位 1.984 A、峰值 1.985 A；电池样本均 Charging | 本次覆盖范围通过 |
| 80%，旁路 | `enabled=0 input=1 full_bypass=1` 在线约 489 秒；236 组电池净电流 −4,882–0 µA，中位 −2.441 mA；SMB5 输入电流中位 0.472 A | 本次在线旁路通过；不据此声称电气隔离 |

BMS 温度实测 35–42°C，固定 PD 输入限额在中段为 1.70–2.00 A 并随自然温度变化调整；没有注入热故障，也未校准板级温标。SOC 11–70%段 1,732 组电池样本中有 11 组短暂报告 Discharging（最低 −483,398 µA），但该段整体 SOC 上升至 71%；不把电池侧 `current_now` 与 USB 输入电流混为一谈。

## 异常与结论边界

- 9 V 段唯一一组输入读数超过当时限额：`boot_ms=2222193`、SOC 41%、BMS 41°C，SMB5 `voltage_now=8.920064 V`、`current_now=2.404735 A`、`input_current_limit=1.95 A`，当时 SMB5 状态为 Not charging，而电池状态为 Charging。前后约 2.1 秒样本分别为 1.984 A / 2.000 A 限额及 1.983 A / 2.000 A 限额；未发现对应 TCPM 合约切换或 `ERROR`/`LOSS` 记录。保留为单点超限，不推定为噪声，也不推定为持续过流。
- 固定 PDO 源在 SOC 11%出现一次自动 PPS `begin_pps` 兼容检查，返回 `-EOPNOTSUPP`，`owned=0`、APDO 参数为零；没有 APDO、PPS 请求或 PPS 合约，本轮没有验收 PPS 快充。
- 充电器实际连接窗口内无 TCPM Hard Reset；启动初期电脑 USB 连接阶段出现的两次 Hard Reset 不计入充电器窗口。开机早期有一条 Fuel Gauge `Failed to get charger supply: -2`，其后设备持续提供电池/SMB5样本。

**结论：** 最新 HEAD 镜像完成固定 PDO 5 V→9 V/2 A→5 V→80%旁路的 Umi/CSOT L3 场景，且来源确认无 APDO；本轮未重验 PPS 快充。固定 PD 协议与阶段功能在本次范围内通过。由于 9 V 阶段有一笔 2.405 A 样本高于 2 A 限额，严格输入限额子项暂不作无条件通过，归 RISK-004 后续独立复核。硬件保护注入、完整温标、无 9 V 源回退、其他 PDO 档位、断电回差、无线 RX/TX 与 Debian 集成均未由本轮证明。
