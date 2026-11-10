# 2026-11-11 Umi 有线 PD/PPS/BQ25970 停机与恢复 L3

## 输入、授权与回退

| 项 | 本轮证据 |
| --- | --- |
| 操作者 / 主机 | 维护者执行实体换线，Codex 经 USB CDC 采集；Darwin 25.6.0 arm64 / zsh |
| 授权 | 维护者明确设备已在 Fastboot，并要求验证 PD/PPS/BQ25970 的协商、充电、故障停机、保护与恢复；另确认沿用绿联 45W 屏显有线充电器，可按提示换线 |
| 设备 | 唯一 Fastboot `product=umi`、`unlocked=yes`、`is-userspace=no`；Mi 10 / CSOT 既有变体，未重新核对面板、底层固件及 Android ROM |
| 源码 | `codex/linux-standard-interfaces`，HEAD `899d64aa114ab4589bf8ba1544f54ef84b97b59b` 加未提交掉线修复；本轮不修改内核、DTS 或诊断固件源码 |
| 固定输入 | Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；GCC 13.3.0；构建/来源锁与 191 项零跳过回归见[前次复核](../reviews/2026-11-10-pd-pps-detach-clock.md) |
| 载荷 | `.work/boot-l2-arm64/boot.img`，SHA-256 `9f7534c122669da97defb0d4566ee9aa1732adfd567d5c2e2f291a2aa51872f3`；RAM 用途 |
| 写入边界 | 仅一次 `fastboot boot` 与标准 power_supply 的有线禁止动作；未刷写、格式化、访问真实块设备、改电气阈值或无线 RX/TX |
| 回退 | 异常时停泵并禁止有线充电，必要时维护者断开充电器；经 PMIC PON 返回 Fastboot／Android；过压/过流/过温不主动制造 |

启动返回 Sending/Booting 均 OKAY，退出 0。CDC `/dev/cu.usbmodem101` 可读 Linux 6.18.60、BQ25970 model/绑定；`mounts` 仅 rootfs/devtmpfs/proc/sysfs/debugfs/configfs，无真实存储分区。初始 Mac 连接显示 Type-C、5 V/3 A 供电上限；未取得初始 PD 合约前不将它称为已协商 PD。初始电池约 37°C、SOC 64–65%，SMB5 Charging、BQ 未启泵。

## 实际操作与证据

1. 复核镜像 SHA-256、唯一设备身份，执行 `fastboot -s <本机序列号> boot .work/boot-l2-arm64/boot.img`（记录不保留序列号）。
2. 只读命令 `version`、`pps-status`、`status`、`mounts`；`collect_charge_log.py --port /dev/cu.usbmodem101 --output <本地日志> --timeout 120` 严格验证 BEGIN/END、序号、存储错误。初次串口响应有异步积压，改为在完整 prompt 后等静默 1.5 秒再发下一命令；权威基线为 `baseline-synced-*` 和完整 charge-log，不使用错位文件名推断响应。
3. 维护者把手机改接绿联约 60 秒，拔线约 10 秒，再接绿联约 60 秒，然后接回 Mac，确认完成。RAM logger 全程记录。
4. 导出 `two-sessions-charge.log`（1589 条、first=0/next=1589、storage_errors=0），再执行 `pps-status`、`tcpm-log`；TCPM BEGIN/END 为 2997 行、complete=1，读取消费协议环。
5. 执行 `pps-stop`，确认 `charge_behaviour=inhibit-charge`、BQ Not charging；维护者再接绿联约 60 秒后接回 Mac，单独验证固定 PD/SMB5 与 PPS 禁止保持。

以上主机命令均退出 0；串口控制以明确返回和随后标准属性/时间线核实，不把成功写入等同实际充电。原始日志与临时主机采集器只在忽略目录 `.work/pd-pps-l3-20261111/`，不入库。

## PPS 两段实测

| 项目 | 结果 |
| --- | --- |
| 源能力 | Source_Capabilities 含固定 PDO 与 APDO 5–11 V / 5 A；设备先固定 5 V，再 APDO 请求，有 Accept/PS_RDY、实际合约读回 |
| 初始协议故障 | 156.606831 s `-EAGAIN`；停泵成功，但 PD 清理也 `-EAGAIN`，`owned=1` 保留互锁；156.902940 s 清理成功才 `owned=0`。157.725847 s 又遇 TX failed / `-EIO`，安全清理成功。期间 TCPM 记录发送失败、Soft/Hard Reset、CC 变化；物理诱因未定位 |
| PPS 恢复 | 159.161268 s 首次启泵；217.385651 s 停机，持续约 58.22 s。重新接入后 230.044402 s 再启泵，288.830663 s 停机，约 58.79 s |
| 正常掉线修复 | 217.336415 s `detached during control cycle error=-107`，随后 WAIT_PLUG/reason=0、stop=0/pd_stop=0/owned=0；与源 Hard Reset/CC 断开对齐，未误记为持续故障 |
| 绝对限值停机保留 | 288.773539 s 拔线窗口 VBUS ADC 已降至 1104 mV、CHG_EN/switching 清零，硬件 fault/latch=0；保留 `-ERANGE` FAULT、停泵/清理成功、owned=0，没有用掉线分类掩盖超出绝对限值的采样 |
| 净充电 | 368 条 RUN 遥测，泵 VBUS 8.087–8.811 V、IBUS 0.638–1.907 A；BMS/泵 VBAT 均低于 4.4 V，电池净充电电流峰值约 3.29 A；启动/切换采样包含负净电流，不能误作泵反向电流 |
| 温控限流 | 虚拟温度约 39.887–40.558°C、level=8、battery cap=4100 mA；请求电流均 2050 mA，实测 IBUS 不越请求值、电池电流不越 thermal cap。电池 temperature 为 370–380 deci-C，即 37–38°C；SOC 总体 64→68% |
| BQ 状态 | 断开前健康采样 fault/latch=0、alarm=0x06、status=0x04、watchdog=0x44、control=0x87；启停/故障路径均由驱动自身读取与保护，不直接操作寄存器 |

`-EOPNOTSUPP` 发生在接回只提供固定 PDO 的 Mac 后；此时不启泵、PD/SMB5 路径保留，不能将此码归为绿联 PPS 运行期异常。旧的 PD/PPS 各约 10 分钟稳定结论保留，本次增加指定修复镜像的专项证据。

## 固定 PD、禁止与回退

- `pps-stop` 在 397.890 s（recorder 时间）记录标准 `inhibit-charge` 写入 rc=0，随后 PPS_OFF、stop=0/pd_stop=0/owned=0。绿联下一次固定合约从 TCPM 473.142778 s 到 556.479469 s，约 83.34 秒；PDO 0 为 5 V / 3 A，整个分段没有 APDO Request。
- 稳态窗口 475–555 s 共 39 组标准供电采样：BQ 全为 Not charging、`inhibit-charge`；SMB5 Charging/Good，输入 4.652592–4.747056 V / 2.979235–2.980860 A，电池净充电 2.381834–2.423338 A，36°C、SOC 70→71%。因此单独停泵会恢复/保留普通充电，不将“泵已停”误写为“整机已停充”。
- `charge-stop` 在 recorder 605.584 s 记录 rc=0。之后 SMB5 `hw_phase=disabled/chg_enable=0/enabled=0`，BQ 与 SMB5 均 Not charging/inhibit-charge，电池 Discharging；输入仍可给系统负载供电，不能要求 USB 总线电流也为 0。
- 维护者再接绿联约 30 秒，实际协议接入约 659.273–696.739 s；662–695 s 的 16 组电池/BQ/SMB5 交叉采样全部保持禁止，电池净电流 −3.906 至 −1.464 mA。TCPM 可重新协商固定 PDO，但两条有线充电路径均未自行启用。
- 最终 `final-charge.log` 为 first=0/next=3144/count=3144、storage_errors=0，严格采集器退出 0；最终 TCPM 分段 198 行、complete=1（此前固定 PD 分段 204 行、complete=1）。最终 PPS 属性仍 inhibit-charge。
- 全部日志保存后执行救援 `bootloader`，经 PON 回到唯一 `umi` Bootloader Fastboot；最终结果以 `rollback.json` 的枚举和产品只读核验为准。未刷写任何分区。

## 完整性、分级与未覆盖

`final-charge.log` SHA-256 `e504c43fe31dddd61c6e07d15f0d54b66594ddab4f106483491432db862315b7`；各阶段原始文件摘要另存忽略目录 `evidence-hashes.json`。`two-sessions-charge.log` SHA-256 `a852a1874f61d93331a47e1922505f678689b3e953957584fbe2a12af808dc96`；对应 TCPM 文本 `ca27edb46f16f865f3edc3aeb9927cdee4f166da4d76ef3849c2c41b13d5d83f`。完整导出验证序号连续、覆盖 0、存储错误 0；断开 Mac 仍持续采样。

L3 仅覆盖上述有线协商、实际充电、自然协议故障时安全停机/所有权重试、拔插恢复、掉线分类、温控限流、固定 PD、标准禁止控制及跨拔插保持；停止验证后返回 Fastboot。不以标度/阈值代码存在、寄存器读回正确或软件禁止替代硬件保护触发：未主动注入 BQ OVP/OCP/OTP、传感器故障/I2C 断路、真实停泵硬件失效；五路板级温度标度、sense/VOUT_OVP 含义、GPIO8 和全部硬件保护专项仍归 RISK-004/TD-005。显式写入 auto 的恢复和 thermal cooling 强制停机未做 L3；本轮恢复证据是自动策略在源端掉线/重连后的恢复。未验证无线 RX/TX、其他充电器、Debian 集成或上游合入。

🚦 审计：所列有线 L3、授权与无存储写入边界 🟢；保护专项/物理诱因 🟡 未闭合，归 RISK-004；高风险合并验收 🔴，仍缺开发者以外专项复核与精确 HEAD CI。本轮仅更新证据文档，沿用前次 L1/L2；最终 `python3 -X utf8 tools/check_repo.py` 与 `git diff --check` 退出 0，未提交、推送或合并。
