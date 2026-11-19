# Umi 有线固定 PD 输入监测修复版 L3：异常复现

| 项 | 本轮证据 |
| --- | --- |
| 日期 / 操作者 | 2026-11-15；维护者授权，Codex RAM 启动、采集与分析 |
| 授权范围 | 有线固定 PD 自然充电、一次 RAM 启动及日志导出；无刷写/块设备写入、保护注入、阈值修改或无线 RX/TX 操作 |
| 设备 / 原系统 | Umi / Mi 10 / CSOT，Fastboot `product=umi unlocked=yes is-userspace=no`；原系统身份沿用[本机记录](2026-11-15-umi-wired-fixed-pd-l3.md)，本次未重新读取 Android 版本/底层固件 |
| 宿主 / 工具 | Darwin 25.6.0 arm64 / zsh；Platform Tools 37.0.2 |
| 输入身份 | 干净工作分支 `codex/linux-standard-interfaces`，HEAD `3263cff0483f9cfa66c5ac693d759b4c7a6710d2`；Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；来源锁 SHA-256 `5b51ce5c2e1b8ea4f2cc5f907064d30812db79348db186572278c3c3c1667ae8` |
| 载荷 | `.work/fixed-pd-monitor-head3263cff-l2-20261115/boot.img`，9,728,000 bytes，SHA-256 `4dd6ee3fa7b076c395dd31f3f41afcb871b653140387c6dfebdae40d2a041a08`；仅一次 `fastboot boot`，Sending/Booting OKAY、退出 0 |
| 配置 / L2 | resolved.config SHA-256 `3695601c86a22d9ddd44d359d65c9ce0d67e880dd81047e12c9c14e03f22c940`；simpledrm 默认配置；独立重建退出 0，Linux 回归 265 项中 261 通过、4 项 DT/schema 环境跳过，不计通过 |
| 前置 / 回退 | 接机前已有镜像、哈希、采集、停止条件与回退材料；维护者在主机基线采集前换接充电器。后续接回导出，停充 `rc=0`，正常重启；维护者确认已进入 Android，主机 ADB 未确认 |

## 采集与完整性

证据留于忽略目录 `.work/fixed-pd-monitor-l3-20261115/`，不提交原始日志或设备标识。

- `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/fixed-pd-monitor-l3-20261115/pd-charge.log --timeout 600`：退出 0。`boot_id=981096c7-dad1-4794-8787-23ae05e04907`，first=0/next=count=415，序号连续、storage_errors=0、ram_only=1，boot_ms=1641–97406。
- `python3 .work/fixed-pd-monitor-l3-20261115/capture_console.py --port /dev/cu.usbmodem101 --tag final --commands version status mounts thermal pps-status tcpm-log`：退出 0。Linux 6.18.60、Umi/CSOT banner、仅 RAM/虚拟文件系统；TCPM ring 显式读取一次，317 行、complete=1。另只读导出完整 `dmesg`，保全重复链接调用栈。
- `control_console.py --port /dev/cu.usbmodem101 --action charge-stop`：退出 0，`Charging inhibited; test cancelled.`；随后同一采集器导出 `closing-charge.log`，退出 0，同一 boot_id，first=0/next=count=863、无存储错误，含 seq=819/boot_ms=205448 的 `CONTROL charge_test_stop rc=0`。再执行 `--action reboot` 正常回退，无重复 RAM 启动。

| 原始证据 | SHA-256 |
| --- | --- |
| pd-charge.log（114,379 bytes） | `3d2d4015657d41522e28d297f2f7453c672898b4f89c7cb00a60b7acf3d5282d` |
| closing-charge.log（243,310 bytes） | `5a86817c68a6ccf3dbf1ad1c21473465e74c57cbbdf46c2ed4bad77223bc1345` |
| final-tcpm-log.txt | `aff0ad9e501ff049006754b1bbf4556f6324b0ba311b1173c88c3fa5a13fff9b` |
| incident-dmesg.txt | `a69d609ff1f8cce3afdbbaf4d31d03c3c1c6029f56d92190277f6ed3254dd932` |

## 实测与异常

维护者确认沿用上次无 APDO 的固定 PD 充电器，报告屏显功率在小于 1W 与约 15W 间跳变。TCPM 在 14.129944s 检出充电器接入，59.138148s 检出 VBUS 断开，约 45.008s；广播五个固定 PDO：5V/3A、9V/3A、12V/3A、15V/3A、20V/2.25A，无 APDO。14.779096s 完成 9V/2A 合约进入 SNK_READY，至拔线无再次切档或 Hard Reset。启动电脑阶段的两次 Hard Reset 与后续接回电脑的 5V 合约分开记录。

起始 BMS 实测 SOC 11%，本次至 12%，温度样本 31–33°C；不能由此前关机描述推定为实测 SOC 0%。9V 稳态采样窗口 boot_ms=18000–59000 内共 20 组 SMB5 样本，输入电流中位 1.883593A；一组异常如下：

- seq=162 / boot_ms=35478：SMB5 `status=Not charging`、`voltage_now=8.923168V`、`current_now=2.425170A`、ICL=1.850000A，合约仍为 9V/2A。前后约两秒样本均约 1.883A、ICL=1.9A。该读数再现[旧镜像异常](2026-11-15-umi-wired-fixed-pd-l3.md)的相近形态，不推定为噪声或持续物理过流。
- 同时段电池净电流从约 +2.856A 降到 boot_ms=36498 的 −0.477538A，再恢复；boot_ms=37540 为 +0.119140A，45828 为 +1.109862A，随后又回到约 +2.85A。与维护者功率跳变现象相符，但日志没有同步外部功率计时间线，不能据电池电流定量还原“小于 1W”输入谷值。
- 初始及停充前完整日志、dmesg 未见 `miseed-fixed-pd input-overlimit` 或 `fault rc/stop_rc/suspend_rc`。事件后 worker 仍 `enabled=1 rc=0`。新增监测在独立约 1s worker 采样，sysfs 约 2s 导出另行采样；不能认定 worker 读到同一个 2.425A 值却忽略，也不能据此证明监测捕获该事件。此次 L3 未闭合严格输入限额及异常停止要求。
- 两次 `sysfs: cannot create duplicate filename …/port0/usb_power_delivery` 调用栈经 `tcpm_psy_set_prop → smb5_fixed_update`，落在固定 profile 切换调用链；同段后续仍完成协议协商。应独立修复链接生命周期，现有证据不足以认定它造成稳态功率跳变。
- `begin_pps=-EOPNOTSUPP(-95)`、APDO 参数全零、owned=0，符合固定源不支持 PPS；无泵运行或 BQ fault/latch 非零证据，不把该兼容检查当作本次跳功率根因。

## 判定与后续

**本轮固定 PD 稳定性/严格输入监测 L3 不通过。** RAM 启动、日志完整导出、固定 9V/2A 协商和停充/返回 Android分别取得上述证据；未完成 5V 收尾、80%旁路或故障锁保持真机验收。此前镜像的正常阶段证据保留其原范围。

下一步离线复核 SMB5 输入调节、ADC/状态采样时序及监测覆盖，并修复 TCPM 链接生命周期。现有资料没有 AICL/实际限流完整寄存器时序或同步外部电流证据，不先认定硬件、线缆、温控或 ADC 换算为根因；不通过改系数、滤波丢样、放宽合约预算或关闭保护掩盖异常。原始值不变，未知原因继续归 RISK-004；保护注入、完整板级温标、PPS、无线 RX/TX、Debian 集成与独立复核仍未由本轮证明。
