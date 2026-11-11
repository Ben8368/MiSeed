# 2026-11-11 Umi 有线旁路供电、PD/PPS/BQ25970 停机与恢复 L3

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

### 当前 HEAD 的 Thermal 真机验证

2026-11-11 主机 Windows 10.0.26200 x64 / PowerShell 7.6.9、WSL Ubuntu 24.04 x86_64；源码基线 HEAD `af32a2b120a7c4f14fea5cb027e039563acc6a75`，含本轮未提交的一次性 thermal sysfs 诊断命令。Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、GCC 13.3.0、GNU make 4.3。容器用 `--network none`，本机锁定输入齐备；被修改的源码缓存原样保留，构建脚本从本地 Git 对象库导出锁定版本到临时干净树完成构建，无联网取料。

构建产物仅供 RAM 启动：`boot.img` SHA-256 `15134d80309f5d211469c30bcc3ae9e5a3ce49d829ec462b71c58daf5e4b47e2`，resolved config `08b70762778823dac5ffdbd0c72f7516c9d69c0dc0f18c4d4d7b93f4a3634030`，DTB `8c7f8455cf3b6310648971daf38861a495ad354b359dc0572f58f700f03cb8de`，rescue initramfs `3a0ee783723dadbe9c580eded83d68afd62a497d0ccc6a96e6bf8ade6b7b03df`。唯一设备 `product=umi`、`is-userspace=no`；执行 `fastboot boot`，未写分区、改变阈值或操作无线 RX/TX。

标准 thermal sysfs 读数单位为毫摄氏度：初始 `qcom-battery=32000` 与 FG/power_supply `temp=320` deci°C 一致（32.0°C），`wired-pd-thermal=32000`、`wired-pps-thermal=36263`；`umi-wired-pd` cooling `cur_state=0/max_state=1`，PPS cooling `0/15`。这验证接口单位换算与当前读数的一致性，不单独证明板级标定准确。USB CDC 主机连接被识别为 SDP，不存在实际 PD/PPS 合约；SMB5 初始 `Charging`、health `Good`、`charge_behaviour=[auto]`、输入限流 `3000000` µA，故只验证固定 PD thermal cooling 回调，不把结果称为实际 PD 协商/温度越限。

1. 只读 `thermal` 枚举 zone 和 cooling device，确认设备唯一且状态合法。
2. 执行 `thermal-stop`，回执 `old_state=0 requested_state=1 readback=1 max_state=1 rc=0`。其后 cooling 状态为 1；标准 SMB5 连续采样 `status=Not charging`、health `Good`、行为枚举仍为 `[auto]`，内核遥测为 `chg_enable=0/enabled=0`，证明 cooling stop 生效且未写充电策略枚举。
3. 执行 `thermal-clear`，回执 `old_state=1 requested_state=0 readback=0 max_state=1 rc=0`。4 秒后 cooling 状态为 0，SMB5 与电池均恢复 `Charging`、health `Good`、内核 `chg_enable=1/enabled=1`，`charge_behaviour=[auto]`。无手工恢复策略或充电限值。
4. `bootloader` 返回 Fastboot；再次核实唯一设备 `product=umi`、`is-userspace=no`。回到维护者接入前的 Fastboot 状态。

停充时间线 `.work/thermal-stop-l3-20261111/thermal-stopped-charge.log` 为 331 条、无覆盖/存储错误，SHA-256 `0d9d7322bf0d374d07d01fbf8748c800233410144a5d69b7e77879b5c2a3793d`；释放后 `.work/thermal-stop-l3-20261111/thermal-cleared-charge.log` 为 468 条、无覆盖/存储错误，SHA-256 `5629a5d4748fac9fb08736ae3544d086e3f013c2d8bf89f20a5b7a2b6e65147f`。原始日志只保留于忽略目录，不入库。

本轮新增 `thermal` / 有线 `thermal-stop` / `thermal-clear` 一次性救援台命令，严格只允许唯一 `umi-wired-pd` cooling state 0/1；不改变 Linux thermal governor、温度表、阈值、传感器驱动或无线路径。验证证明温度单位/读数可见、固定 PD cooling 回调可停止并自动释放；**不证明 45°C 实际热触发、板级温标准确性、传感器断路/越界时行为，也不证明 PPS 冷却硬件停泵**。未人为加热或拔传感器。传感器异常的合成故障路径由 `test_kernel_thermal.py`、`test_charge_policy.py`、`test_pps.py` 覆盖，仍需真实故障专项及独立复核。

全量 WSL 单测 194 项通过、4 项因测试时无 compiled Umi DTB/dtschema 而跳过；本机 `tools/check_repo.py` 通过（既有 8 项源码篇幅黄灯保持可见），`git diff --check` 通过。源码篇幅按 QUALITY 执行：301 行起黄灯并登记、超过 600 行红灯阻断；本轮未以 300 行作为实现硬门槛。

`-EOPNOTSUPP` 发生在接回只提供固定 PDO 的 Mac 后；此时不启泵、PD/SMB5 路径保留，不能将此码归为绿联 PPS 运行期异常。旧的 PD/PPS 各约 10 分钟稳定结论保留，本次增加指定修复镜像的专项证据。

## 固定 PD、禁止与回退

- `pps-stop` 在 397.890 s（recorder 时间）记录标准 `inhibit-charge` 写入 rc=0，随后 PPS_OFF、stop=0/pd_stop=0/owned=0。绿联下一次固定合约从 TCPM 473.142778 s 到 556.479469 s，约 83.34 秒；PDO 0 为 5 V / 3 A，整个分段没有 APDO Request。
- 稳态窗口 475–555 s 共 39 组标准供电采样：BQ 全为 Not charging、`inhibit-charge`；SMB5 Charging/Good，输入 4.652592–4.747056 V / 2.979235–2.980860 A，电池净充电 2.381834–2.423338 A，36°C、SOC 70→71%。因此单独停泵会恢复/保留普通充电，不将“泵已停”误写为“整机已停充”。
- `charge-stop` 在 recorder 605.584 s 记录 rc=0。之后 SMB5 `hw_phase=disabled/chg_enable=0/enabled=0`，BQ 与 SMB5 均 Not charging/inhibit-charge，电池 Discharging；输入仍可给系统负载供电，不能要求 USB 总线电流也为 0。
- 维护者再接绿联约 30 秒，实际协议接入约 659.273–696.739 s；662–695 s 的 16 组电池/BQ/SMB5 交叉采样全部保持禁止，电池净电流 −3.906 至 −1.464 mA。TCPM 可重新协商固定 PDO，但两条有线充电路径均未自行启用。
- 最终 `final-charge.log` 为 first=0/next=3144/count=3144、storage_errors=0，严格采集器退出 0；最终 TCPM 分段 198 行、complete=1（此前固定 PD 分段 204 行、complete=1）。最终 PPS 属性仍 inhibit-charge。
- 全部日志保存后执行救援 `bootloader`，经 PON 回到唯一 `umi` Bootloader Fastboot；最终结果以 `rollback.json` 的枚举和产品只读核验为准。未刷写任何分区。

## 有线旁路供电：已实现并完成真机验证

本项目旁路供电的功能定义为：外部有线电源供系统运行，同时禁止电池充电，电池净电流保持接近零。该功能已实现，并由上述同一载荷、同一轮有线真机操作完成 L3 验证；本节是对已保存证据的功能确认，不是新增设备操作。

实现复用 SMB5 的系统供电路径与标准 `power_supply/charge_behaviour=inhibit-charge`，BQ25970 泵保持关闭。662–695 s 的 16 组采样中：USB 输入功率由同组 µV/µA 读数计算为 1.772–2.658 W、平均 2.216 W；电池净放电仅 1.464–3.906 mA，按同组电池电压折算约 5.89–15.72 mW。结合两路 Not charging、SMB5 `chg_enable=0/input=1`，确认系统主要由外部电源供电，电池停止充电；接入绿联后该状态持续保持。约 2 W 是系统供电与转换损耗的输入估算，不是电池仍在充电的证据。

能力与具体载荷登记在 [platforms](../../platforms.md#硬件能力)，上层标准调用契约见 [PPS 接口](../../../kernel/power/PPS.md#有线旁路供电的上层调用)。本次结论不包含电池电气隔离、拆电池运行或负载突变下的补电行为；它们不影响已经闭合的有线旁路供电功能结论。

## 完整性、分级与未覆盖

`final-charge.log` SHA-256 `e504c43fe31dddd61c6e07d15f0d54b66594ddab4f106483491432db862315b7`；各阶段原始文件摘要另存忽略目录 `evidence-hashes.json`。`two-sessions-charge.log` SHA-256 `a852a1874f61d93331a47e1922505f678689b3e953957584fbe2a12af808dc96`；对应 TCPM 文本 `ca27edb46f16f865f3edc3aeb9927cdee4f166da4d76ef3849c2c41b13d5d83f`。完整导出验证序号连续、覆盖 0、存储错误 0；断开 Mac 仍持续采样。

L3 仅覆盖上述有线协商、实际充电、自然协议故障时安全停机/所有权重试、拔插恢复、掉线分类、温控限流、固定 PD、有线旁路供电、标准禁止控制及跨拔插保持；停止验证后返回 Fastboot。不以标度/阈值代码存在、寄存器读回正确或软件禁止替代硬件保护触发：未主动注入 BQ OVP/OCP/OTP、传感器故障/I2C 断路、真实停泵硬件失效；五路板级温度标度、sense/VOUT_OVP 含义、GPIO8 和全部硬件保护专项仍归 RISK-004/TD-005。显式写入 auto 的恢复、PPS thermal cooling 强制停机与真实温度触发未做 L3；固定 PD cooling 强制停充/释放已在前述 SDP 连接下完成 L3；本轮恢复证据是自动策略在源端掉线/重连后的恢复。未验证无线 RX/TX、其他充电器、Debian 集成或上游合入。

🚦 审计：所列有线 L3、授权与无存储写入边界 🟢；保护专项/物理诱因 🟡 未闭合，归 RISK-004；高风险合并验收 🔴，仍缺开发者以外专项复核与精确 HEAD CI。本轮仅更新证据文档，沿用前次 L1/L2；最终 `python3 -X utf8 tools/check_repo.py` 与 `git diff --check` 退出 0，未提交、推送或合并。
