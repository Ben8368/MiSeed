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

## 2026-11-11 local HEAD BQ25970 watchdog and PPS thermal L3

This follow-up used repository HEAD `2bdb2666e2557aa1d1309845bca57938967621b0`, simpledrm RAM image SHA-256 `042155d91395bb8315418f9cded8b0b2c34c5e7b11715d91de44682287a9dc50`, and the wired Ugreen 45 W charger. No flash or storage operation was performed. Preflight build-source-lock matched the worktree; L2 succeeded and the complete offline suite passed 237 tests with zero skips.

The complete RAM charge log has 2356 records, no gaps/overwrites and zero storage errors. TCPM export has 2429 lines and `complete=1`. PPS entered RUN at boot 40.634 s and continued through 246.134 s: 664 RUN samples, about 205.5 s. Samples reached about 9.0 V / 2.8 A pump input and 5.1 A BMS charging current. The 30 s watchdog configuration stayed enabled; successful per-cycle BQ protection verification continued without `-ETIME` or a timeout flag. The stop snapshot was `bq_watchdog=0x43` (configuration retained, timeout bit clear), with BQ fault/latch zero.

At the end of PPS, the worker returned `-ERANGE` with no BQ fault, bus-low/high, or watchdog timeout. TCPM then explicitly requested fixed PDO 5 V / 3 A at 246.321 s and completed the transition at 246.459 s; stop/pd-stop returned 0 and ownership was released. SMB5 continued charging for about 180 s at roughly 4.8 V / 3 A. This was a controlled thermal fallback, not a spontaneous PD or watchdog failure: after the run, the PPS thermal zone read 39.542°C with cooling state 8; DTS trip 0 is 41°C with 3°C hysteresis and maps to state 8 (4.1 A). The last normal PPS sample was at 40.980°C with about 5.12 A BMS current. This strongly supports the configured thermal limit causing a safe stop and fixed-PDO fallback, although the exact cooling-state edge was not timestamped. The physical cable disconnect occurred later, at TCPM 427.013 s.

**Watchdog normal-operation L3: pass.** The 205.5 s active PPS window exceeds six 30 s intervals with normal periodic I²C verification and no expiry. No deliberate no-refresh timeout injection was performed, so timeout-bite and post-expiry recovery are not claimed. **PPS thermal protection/fixed-PDO fallback L3: pass** for the observed state-8 stop and SMB5 recovery; exact trip edge is inferred. Other chip protection injection, full board thermal calibration, fixed-PD thermal trigger, and wireless RX/TX remain open.

## PPS 分段功率与满电自动旁路 L1/L2（2026-11-11）

维护者最终指定有线 0–14% 为 SMB5 固定 5 V/10 W、15–29% 平滑升至 27 W 目标、30–80% 为 18 W、81% 起为 9 W；优先 PPS、稳定功率及较高电压/较低电流，充满采用已经验证的旁路。授权完成 L2 后推送 PR #6；未操作设备。

环境：Windows 10.0.26200 x64、PowerShell 7.6.9；WSL Ubuntu 24.04 x86_64、GCC 13.3.0、GNU make 4.3、dtschema 2025.8 / yamllint 1.38.0。源码基线 `0cfb6d10d9e82c15aa0d3db4aee728c074cdcc32` 加本节对应提交的输入锁；Linux 仍固定 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`。没有引入外部代码或固件、没有提高电气保护参数。

策略权威说明见 [PPS 接口](../../../kernel/power/PPS.md#状态与安全退出)。0–14% 不激活 PPS，由固定 5 V/SMB5 将输入限至 ≤2 A，10 W 是标称输入目标，实测随压降/负载变化；不是以 SOC 重定义硬件预充阈值。15–29% 进入 PPS，从 10 W 目标以每至少 1 秒最多 1 W 爬向 27 W；30–80% 目标 18 W，81% 起目标 9 W。同次插入 SOC 阶段只向后推进，避免边界回落反复切档。虚拟温度 ≥38°C（沿用已有首档解除点）或 FCC 收紧，锁定快速补能上限 18 W；9 W 阶段不因此升功率。

PPS 泵输入功率取同组 VBUS×IBUS，以 1/4 指数平均与 ±0.5 W 死区调节。现有 9.5 V/3 A、6 A 电池及 APDO/温控保护更优先，不宣称实际达到 27 W。降额时先调压，使 IBUS 低于新的电流请求至少 100 mA，再降低源端限流；实测电池/总线超限仍立即停机，未给予保护宽限。18/9 W 服从恒压末段和更严格温控。固定 5 V/SMB5 在高 SOC 继承 ≤1.8 A 输入限额，其余阶段最高仍为既有 3 A；运行中每次 worker 最多改变 ICL 0.1 A，并保留观测到的外部 ICL 上限，防止预充/收尾/无 APDO 路径丢失功率约束。

接口审查：USB PD/PPS → 固定 Linux Type-C/TCPM power_supply；温度/冷却 → IIO/thermal；满电 → SMB5 标准 STATUS=Full 与现有停电池充电、保留 USB 系统输入机制。没有新增私有 sysfs/ioctl、DT binding 或 Android HAL/Binder 依赖；无线 RX/TX 未修改。现有 umi_smb5 所有权例外仍归 TD-004，温标/真实保护及新策略 L3 归 RISK-004/TD-005。

自动满电旁路仅在有效健康/遥测和硬件 Full 后锁定，SOC=100% 单独不触发。PPS 仍按既有 95%/收尾条件停泵，交回固定 5 V/SMB5 完成末段后旁路；自动锁定仅物理拔线解除（含 IRQ 捕获的短拔插），用户 inhibit 独立保留。停止失败仍采用停充/输入暂停回退、不喂 watchdog，不能根据请求状态声称实际旁路成功。已有手动旁路 L3 保留，新自动终止交接未做 L3。

验证：

- 定向 `python3 -X utf8 -m unittest discover -s tests -p test_pps.py -v`（5 项）及 `-p test_charge_policy.py -v`（2 项）通过。实际 worker 覆盖低 SOC 不激活 PPS、15/30/80% 边界、10→27→18→9 W 目标缓变、SOC/温度回落不返档、18/9 W 平台、降额先调压、原硬限值、协议离线不冒充物理拔线、硬件 Full/状态读错/停机失败/短拔插；SMB5 补充 2/1.8 A 输入限额、外部更严格 ICL、读写错误反例。全量回归沿用这些实际源码，不只测试公式。
- Windows 经 `wsl -d Ubuntu-24.04 --cd /mnt/c/MiSeed -- bash .work/pps-power-l2-20261111.sh simpledrm` / `native`，分别使用 `MISEED_NATIVE_DSI=0/1`、`MISEED_JOBS=6`、下表 output_root；对应缓存为 `/tmp/miseed-boot-l2/root-default-{source,build}-20261111` / `native-dsi-{source,build}-20261111`，调用 `bash tools/build_kernel_l2.sh`。两套 ARM64 编译、链接、配置/输入锁核验和镜像打包均退出 0。首次新增 SMB5 helper 的 `current` 局部变量与内核宏重名，已改为 `icl_ua`；最终两套重建均成功。
- 两套分别运行 `python3 tools/validate_kernel_dt.py --source <对应 source> --build <对应 build> --dtb <对应 output_root>/umi-csot.dtb`：定向 dt_binding_check、CHECK_DTBS、最终 DTB/schema 与四项 DT 反例均退出 0、无跳过。本轮没有 DTS/binding 改动。
- `PATH=/tmp/miseed-dtschema/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`；`MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/pps-power-test-dtb-20261111.dtb`、`MISEED_DT_SCHEMA=/mnt/c/MiSeed/.work/pps-power-test-schema-20261111.json` 为对应默认 DTB/schema 的独立快照。全量 `python3 -X utf8 -m unittest discover -s tests -v`：**238 项、0 跳过，54.584 秒，OK**。快照 DTB 与最终默认打包 DTB 逐字节一致。
- 两套最终构建的全部 POWER_FILES 与当前工作树逐字节一致，build-source-lock.json 与当前输入锁一致；仓库/来源锁/预算检查及 `git diff --check` 通过。日志未见 future-time/Clock skew。当前新增输入限额仅覆盖有线，不将其作为无线验证。

| 配置 / output_root（仓库相对） | boot.img 字节数 | SHA-256 |
| --- | --- | --- |
| 默认 `.work/pps-power-simpledrm-20261111` | 9654272 | `71619df18f35ff4c0b58c0c96b0dddeab5c48986cb6739d59b2ef1baa5aebbc6` |
| 原生 `.work/pps-power-native-20261111` | 14135296 | `a103bd4a89f8f11ee4f1c2be0881fb03b2ec4d38c4b6053baefcc18bd8efc015` |

完整日志/包装器仅留忽略目录 `.work/pps-power-*-20261111*`，镜像与构建物不提交。Audit：既有 8 项篇幅黄灯归 TD-008（umi_pps.c 515 行、pps_harness.c 600 行），无新源码超限；PR #6 达 100 文件、规模红灯，按维护者指定更新本项后停止扩展，不合并。新功率/温升、SMB5 限额、自动满电旁路 L3、开发者以外专项复核及最终精确 HEAD 远端 CI 仍须分别取证。

## 2026-11-12：分段策略 L3 启动与初始基线

维护者本轮明确要求修复 CI 后暂不推送，并启用真机 L3；报告初始约 5% 电量、继续使用既有绿联 45 W PPS 有线头。CI 修复只涉及 workflow 与离线测试，充电代码未变，见[修复记录](../reviews/2026-11-10-kernel-validation-environment.md#2026-11-12pr6-boot-l2-apt-dns-失败与本地修复)。

先完成上述 240 项零跳过、目标 L2/DT/schema、输入锁与 25 项有线源码/主机工具/脚本核验，再通知接入。载荷为默认 simpledrm，禁用原生 DSI；SHA-256 `71619df18f35ff4c0b58c0c96b0dddeab5c48986cb6739d59b2ef1baa5aebbc6`。主机 Windows x64 / PowerShell 7.6.9、Platform Tools 37.0.1、pyserial 3.5；材料 `.work/ci-dns-l3-20261112/preflight.json`、`L3-steps.md`、一次性 boot_once.py 与限命令采集器均在接入前备齐。

唯一设备只读确认 product=umi、unlocked=yes、is-userspace=no 后仅一次 `fastboot boot`，Sending/Booting OKAY、退出 0；未刷分区。CDC COM6 的 VID/PID 1d6b:0104、MiSeed Umi/CSOT greeting 与 Linux 6.18.60 核实通过，不能只用 Bootloader 接收代替 Linux 启动。mounts 仅 rootfs/devtmpfs/proc/sysfs/debugfs/configfs，无真实存储挂载。

初始 PC 连接下 BMS 实测 SOC 6%、电池 28°C、PPS 虚拟温度 32.636°C，两路 cooling state=0；BQ25970 绑定、Not charging，策略 auto；SMB5 Charging、输入限流标准属性读回 2000000 µA。PC 被识别为 Type-C/SDP、没有实际 PD/PPS 合约，不能据此声称绿联固定 PD 已通过。基线严格导出 `baseline-charge.log`：312 条、first=0、无覆盖、storage_errors=0。status 的旧诊断文本 icl_ua=3000000 是常量，实际配置以标准 input_current_limit=2000000 为准。

下一步沿用绿联自然充电，先覆盖 <15% 固定 5 V/SMB5 ≤2 A、15% PPS 接管与 30% 后 18 W，再按实际 SOC 续测 81%/满电旁路；未人为放电、加热或改保护阈值。故障/保护停机时断开充电器、接回电脑停充采集，保存后用 PON bootloader → Fastboot → Android 回退；现场后续结果另追加，初始基线不外推完整四段 L3。

## 2026-11-12：已有数据复核与有线分段 L3 结论

沿用上节 `bcfd241729f1d285b7d39d598d5556c760efba6d` 有线代码及同一 `71619df1…aebbc6` RAM 镜像。维护者接回电脑后已导出首轮全部日志；随后提出的 0–20%/21–80% 新分段已取消，明确接受本轮实测速度、保留现有策略，并授权核实后提交推送 PR #6。本轮不改充电参数或无线 RX/TX，不再安排设备操作。

完整性复核：`python -X utf8 .work/ci-dns-l3-20261112/verify-round1.py` 退出 0。使用仓库采集器重新严格解析原始 BEGIN/END 与每条序号：基线 312 条，首轮 first=0、next/count=11367，连续、无覆盖、storage_errors=0；977 组电池样本与 3525 条 PPS RUN。TCPM 单独读取至 EOF，4253 行、complete=1。原始摘要如下，日志、复核脚本和派生 JSON 仅留忽略目录 `.work/ci-dns-l3-20261112/`，不提交原始长日志或镜像。

| 证据 | SHA-256 |
| --- | --- |
| baseline-charge.log | `607e4774e446c0d266bc0b1ce7a9f5303ef6cb451d8e75c68c50d40358703d53` |
| round1-charge.log | `c66a6a3cff7162ca799b37f1c8d9be91908cd5b1ecca7064acab88c0f2c554aa` |
| round1-console.log（含完整 TCPM） | `985937d1b53acce310693a5850e4be3ff20db5c5603fca81177c9f1996f60e97` |

功率口径：固定 PD 用同条 SMB5 voltage_now×current_now；PPS 用同条泵 VBUS×IBUS。均为驱动输入测量估算，不是充电头交流输入或电池净功率。事件时刻用 kmsg 原始 µs 时间戳，BMS 用 boot_ms；SOC 只作异步样本关联，不以采集入库时间认定精确切换边界。

| 已核实阶段 | 原始证据与结论 |
| --- | --- |
| 低 SOC 固定 PD/SMB5 | TCPM 157.401 s 请求 PDO 5 V/3 A，157.454 s 到 SNK_READY；源端合约 3 A 不等于手机抽取 3 A。稳定窗口 160–883 s 的 344 组 Charging 样本，标准 ICL 全部 2 A；输入功率中位 9.769 W、P90 9.775 W，最高 9.784 W（启动过渡最低 6.836 W）。首次 APDO 前固定 PD 约 726.6 s。 |
| 15%附近 PPS 接管与升功率 | 884.009 s 开始 APDO 5 V/3 A，随后 8.38 V/3 A；协议接受/PS_RDY 后均进入 SNK_READY。885.154 s 首条 RUN 目标 11 W，904.248 s 达 27 W 目标；相邻目标最多变 1 W。实测峰值 25.613 W（8.943 V×2.864 A），不是实际 27 W。最后一条 14% 与首条 15% BMS 样本分别在 885.180/887.225 s，异步采样不足以证明精确 SOC 边界；软件边界证据沿用 L1。 |
| 自然升温平滑降到 18 W | 920.962 s 虚拟温度 38.252°C、steady=1，931.075 s 目标到 18 W，约 10.1 s 完成降功率；之后不再升回 27 W。排除降档过渡（t>934 s），3 A 请求窗口 1534 条：实测中位 17.701 W，P90 17.856 W，范围 16.752–18.214 W。 |
| state 8 限流仍保持 PPS | 1410.286 s 虚拟温度 41.119°C、state 8、FCC=4100 mA；1411.286 s 源请求已降至 2050 mA，此时 IBUS=1945 mA、IBAT=3373 mA，满足先降到新限值以下再改请求。后续 1838 条 18 W 目标样本：实测中位 16.161 W，P90 16.335 W，范围 14.291–16.706 W。热限流优先于功率目标，因此会低于 18 W。 |
| 跨 30% 与拔线 | 电量覆盖 6→38%，30% 后持续 18 W 目标；此前已因温升降额，不能把这次观察记作单独由 30% 触发的 27→18 W。RUN 从 885.154 至 1981.818 s，共 1096.664 s（约 18.28 分钟）；无 miseed-pps failure，首次 APDO 至物理断开之间无固定 PDO Request。1982.050 s VBUS off 对应维护者拔线接回电脑，随后正常释放所有权、stop/pd_stop=0。 |

全部 RUN 样本的实测 IBUS 未超过当时请求（≤3 A），IBAT 未超过当时 FCC（≤6 A），VBUS≤9.5 V；本轮没有靠调整限值维持 PPS。电池样本温度 27–40°C，PPS 虚拟温度 37.203–42.208°C；虚拟温度是既有板级模型输出，不能与电池温度或完整温标验收混用。25 项有线源码哈希与接入前材料一致，镜像哈希一致；本轮源代码未变。

**本轮有线 L3 通过：** 低 SOC 固定 5 V/SMB5 ≤2 A、PPS 接管/目标爬升、自然升温平滑降低功率、state 8 先降流后收紧源请求且持续 PPS，以及跨 30% 保持 18 W 目标。较慢的主要已观测原因是温升锁定 18 W 与后续 2050 mA 热限流，并非 PPS 自发退回固定 PD。维护者明确保留此实测策略。

**未覆盖：** 81% 起 9 W、硬件 Full 自动旁路交接及全充电周期尚无本轮 L3；既有手动旁路 L3 保留。完整板级温标、固定 PD 实际热触发、硬件故障注入和其他 RISK-004 专项仍开放。维护者已报告手机重启回 Android；本机只读 ADB 枚举为 0 台，故回退状态按维护者报告记录，不声称读取 sys.boot_completed。重启后的未导出 RAM 末段无法补取，已导出首轮完整性不受影响。

本轮 CI 修复与 240 项零跳过/L2/DT/schema 结果见[环境记录](../reviews/2026-11-10-kernel-validation-environment.md#2026-11-12pr6-boot-l2-apt-dns-失败与本地修复)。高风险合并门禁仍须独立专项复核与最终精确 HEAD 远端 CI；此处提交推送授权不包含合并。
