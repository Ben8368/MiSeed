# Umi 有线固定 PD 二次修复 L3：保护动作通过，持续充电仍中断

| 项 | 本轮证据 |
| --- | --- |
| 日期 / 操作者 | 2026-11-15；维护者要求启动二次修复镜像、随后接回 USB 导出充电日志，Codex 协助 |
| 范围 / 设备 | 有线固定 PD / Umi / Mi 10 / CSOT；沿用已确认无 APDO 的同一充电器。一次 `fastboot boot`、自然充电、只读采集、停充/正常回退；无刷写、真实块设备写入、保护注入、阈值修改或无线 RX/TX 操作 |
| 宿主 / 输入 | Darwin25.6.0 arm64 / zsh，Platform Tools37.0.2；`codex/linux-standard-interfaces`，HEAD `3263cff0483f9cfa66c5ac693d759b4c7a6710d2` 加[二次修复](../reviews/2026-11-15-fixed-pd-sampling-paths.md)；启动前精确差异、工具/源码/产物哈希核验通过 |
| 来源锁 / 配置 | 来源锁及构建副本 SHA-256 `e23ffc62a0ab8659e375f8d730d4f98323fdb3c299470b8b3377851745acbda0`；resolved.config `3695601c86a22d9ddd44d359d65c9ce0d67e880dd81047e12c9c14e03f22c940`；固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，L2/严格DT/266项零跳过结果见修复记录 |
| 镜像 / 启动 | `.work/fixed-pd-sampling-l2-20261115/boot.img`，9,728,000 bytes，SHA-256 `ceafb34bba3fc6e45920cc9cd84fa67e793c0aa00669fae9e7c33cefe7afaeaf`；设备核对 umi/unlocked=yes/is-userspace=no，Sending/Booting OKAY、退出0，未重复启动 |
| 前置 / 回退 | 材料已备；维护者在主机基线读取前换接充电器，后接回导出。停充 `rc=0`、正常重启；ADB确认 `umi`、Android16/SDK36、boot_completed=1；底层固件本次未重读 |

## 采集与完整性

原始证据留 `.work/fixed-pd-sampling-l3-20261115/`，不入库、不公开设备标识。

- `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/fixed-pd-sampling-l3-20261115/pd-charge.log --timeout 600`，退出0。`boot_id=91cd1258-8624-454e-adb9-eb2a30b519e8`、first=0/next=count=5226，boot_ms=1624–1054611；序号连续、storage_errors=0、ram_only=1，无ERROR/LOSS。
- `capture_console.py --port /dev/cu.usbmodem101 --tag final --commands version status mounts thermal pps-status dmesg tcpm-log`，退出0。Linux6.18.60、Umi/CSOT banner、仅RAM/虚拟挂载；保存dmesg快照。TCPM ring只显式读一次，498行、complete=1。
- `control_console.py --port /dev/cu.usbmodem101 --action charge-stop`，退出0；同采集器导出 `closing-charge.log`，退出0，同boot_id、first=0/next=count=6412，storage_errors=0，含seq=5953/boot_ms=1177146的`CONTROL charge_test_stop rc=0`。随后`--action reboot`，退出0；ADB只读核实正常回退，不记录序列号。

| 证据 | bytes / SHA-256 |
| --- | --- |
| pd-charge.log | 1,576,966 / `9cda09e1e30e4efeeb63a759b8d88e7fa4477d0a3b9e3d672287485988ce11ad` |
| closing-charge.log | 1,931,200 / `05ed4d25ab4c92409bf205150d3106bfeae9fda4d7c1dd217d8c83df699ad82e` |
| final-tcpm-log.txt | 29,737 / `3d92c4b197b5b8380f35a313918f97c5f579bd195b062057909ecf8d182dde7c` |
| final-dmesg.txt | 126,274 / `1aabfd77a379836fc19b9a3dfbe7fa7a16e88b72829332203699acdad6655e44` |

## 协议、充电与故障时序

充电器于TCPM 11.491063s连接、1011.117397s断开，约16分40秒。广播固定5V/3A、9V/3A、12V/3A、15V/3A、20V/2.25A，无APDO；14.652445s完成9V/2A合约。BMS样本SOC9–14%、温度26–32°C；故障时SOC14%、30°C。正常9V采样窗口boot_ms=16000–226000内100组SMB5样本，MID电压8.613450–8.677680V，输入电流中位1.782870A、峰值1.984320A；限额随原温控策略下降。未据此宣称完整温标或SOC全程通过。

| 内核时间 / 事件 | 直接证据 |
| --- | --- |
| 226.482898s，故障前寄存器 | chg_enable=1、ICL_STATUS/AICL_ICL/ICL_CFG均0x20（1.6A限额）、input_cmd=0x00、power_path=0x95 |
| 226.528258s，worker采样 | measured_ua=1346235、icl_ua=1600000、contract_ua=2000000；仍正常运行 |
| 226.604111s，标准属性采样超限 | `input-overlimit measured_ua=2361265 icl_ua=1600000 contract_ua=2000000 source=power_supply`，seq=1327/boot_ms=226677；原始2.361265A值保留 |
| 226.628459s，故障停止结果 | `fault rc=-75 stop_rc=0 suspend_rc=0`；从超限日志至结果约24ms，不把日志差当作硬件瞬态保护反应时间 |
| 226.654815s，停止后寄存器 | chg_enable=0、phase=disabled、input_cmd=0x01、ICL_STATUS=0x00、power_path=0x52；AICL_ICL/ICL_CFG仍0x20，没有通过改限额抹去异常 |
| 226.820130s，安全合约 | TCPM回固定5V/3A、SNK_READY；输入仍暂停、故障锁未解除 |
| 至1011.117397s拔线 | 按原始内核时间筛选：705条worker记录均enabled=0；706次input_cmd快照均0x01；无自动启充，保持约13分04秒。BMS转为放电；不把此暂停窗口记为正常充电时间 |

物理拔线后才解除会话故障，接回电脑进入新的5V会话，随后可再充电；这不算故障锁自行恢复。电脑连接段出现两次Received hard reset，与充电器窗口分开；充电器窗口没有Hard Reset，除故障后的安全5V回退外没有反复切档。`begin_pps=-95`、APDO零、owned=0为固定源不支持PPS的兼容检查，无泵运行证据。

本次dmesg/充电日志均无重复`usb_power_delivery`文件名告警，覆盖9V切入、故障回5V及拔线恢复。维护者答复没有持续观察本次功率显示，不能据此认定本次仍有或已消除“小于1W/15W”目视跳变。

## 分项判定与待查

- **标准电流读取检测超限、两路停止、故障保持至拔线：本次L3通过。** 原worker未采到该值，新的标准getter实际捕获并停机，验证了[前次缺口](2026-11-15-umi-fixed-pd-monitor-l3.md)的修复；寄存器与后续状态交叉支持，原始值未滤波/钳位。
- **TCPM链接修复：本次所覆盖profile转换不再出现旧重复链接告警。** MID物理通道正常提供带载样本；不由此关闭全部ADC准确度问题。
- **正常持续固定PD充电：尚未通过。** 2.361265A异常输入读数仍使会话中断，物理过流、传感器/采样异常及此前功率跳变根因仍归RISK-004；不能把保护有效改写为充电正常，也不能据缺少同步外部测量就放宽阈值、改换算系数或忽略该值。原始输入电流、有效/配置ICL和电源路径需继续独立复核。

80%旁路/收尾、本次故障之外的保护注入、完整温标、PPS/无线RX/TX及Debian集成未由本次证明。旧镜像的已闭合正常阶段证据保留原范围；本轮不注入危险故障，不提交镜像/原始日志，不提交或推送代码。
