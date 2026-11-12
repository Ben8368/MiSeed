# Umi PPS 充电路径与准入

此目录实现主线 TCPM → 自动 PPS 策略 → BQ25970；软件策略已按 LOS 接入自动 attach/退出，仍是测试路径而非已发布产品。Umi sink-only 测试镜像已启用 Type-C、I2C15、充电泵和 VBUS 节点；真机已确认控制器绑定。镜像 `44854093…e98a89f94c`：PD 与 PPS 分别约 10 分钟真机 L3 均通过，维护者宣告二者充电均已稳定。

## 输入与支持范围

参考 LineageOS `lineage-23.2`，固定提交 `71b13e62f057a649b77fe4062feb73ee72ad609c`；作者、许可证和文件哈希见 [NOTICE](NOTICE.md) 与[来源锁](../../manifests/sources.lock.json)。依赖本项目固定 Linux 的 PM8150B Type-C/PD PHY、TCPM power_supply 接口和现有 FG/SMB5。Android 的 usbpd、pmic-voter、私有 power_supply 属性不直接导入。

## 与 LOS Umi 的策略对照

| 项目 | 固定 LOS Umi | MiSeed 主线适配 |
| --- | --- | --- |
| 入口 | SE15 I2C、0x66 BQ25970、GPIO68；`usbpd_pm` 自动管理 PPS | 主线 TCPM + 本控制器；测试镜像 enabled，RPMh/I2C15/BQ 绑定已 L3 |
| BQ 配置 | BAT_OVP 4550 mV；battery OVP alarm 禁用；battery OCP/UCP alarm 禁用；BUS_OVP 12000/11000 mV、BUS_OCP 3750/3500 mA、AC OVP 14 V；电池 OCP 与 BQ 三路 TS 比较器禁用，sense 属性留注释；watchdog 30 秒档启用；本地 HEAD 正常运行刷新 L3 已验收 | 限值继续按 LOS；watchdog 刷新方式参考同系列 BQ25968，依靠成功 I²C 事务，不设独立喂狗任务。sense 位沿用本机读到的 0xd0 配置，不推断实物电阻 |
| 热控与电量 | `mi_thermald` 通过 SMB5 thermal FCC 投票；常规 Umi PPS 上限表为 16 档；PD 策略在 95% SOC 退出 charge pump | 已移植六路虚拟温度及 FCC 表；本地71–80%优先10W PPS、81%旁路、≤71%以10W复充，95%原安全边界保留 |
| 运行方式 | USB/PD 事件驱动，符合条件即进入持续策略，拔线/故障/高电量时退出 | 驱动 probe 后轮询 PD attach 并自动尝试 PPS；拔线回到等待态，故障回固定 PDO 并恢复 SMB5 策略 |

来源证据为固定 LOS `umi-sm8250.dtsi`、`bq2597x_charger.c`、`pd_policy_manager.c/.h` 与 `smb5-lib.c`。自动 attach、SOC/温控策略、LOS SMB5 限流与 PPS 退出回退已移植；节点绑定已 L3；镜像 `44854093…e98a89f94c` 的 PD/PPS 分别约 10 分钟真机通过并宣告稳定。本地 HEAD `2bdb266` 另完成 watchdog 正常刷新及 PPS thermal cooling state 8 停泵/固定 PDO/SMB5 恢复 L3，见[2026-11-11 记录](../../docs/archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。其余保护故障注入、完整板级温标和发布验收仍开放。

仅接受独立 BQ25970、完整 ID `0x10`；参考驱动兼容的 SC8551/SC8551A/NU2105/BQ25968 不作相同 ADC/寄存器处理。本机 Android 定点读取已确认 `0x10`，不替代 MiSeed probe。固定 PDO 为 5 V / 3 A，PPS sink APDO 为 3.3–11 V / 2.5 A；策略限制到 9.5 V、2.5 A 总线和 6 A 电池，SMB5 与 LOS 相同设为 3 A USB ICL、6 A FCC、4.45 V FV。这些是 LOS 配置，不是实测充电功率。

## 状态与安全退出

当前有线策略（L1 240项与双配置L2/DT已通过；新L3已覆盖6→56%连续PPS、低流降额与回升，71/81等分项待验，证据见[本轮记录](../../docs/archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)末节）：

| SOC / 周期 | PPS 可用 | 仅固定 PD |
| --- | --- | --- |
| 0–5% | 固定5V/SMB5，输入≤1A，标称5W | 同左 |
| 6–70% | 20W目标，输入≤2.5A，电压随2:1泵工作点调整 | 固定5V/≤3A，标称15W |
| 71–80% | 保持或启动PPS，10W目标 | 固定5V/≤2A，标称10W |
| ≥81%或硬件Full | 停止电池充电，确认停泵后固定5V旁路供系统 | 停电池充电、保留系统输入 |
| 旁路后≤71% | 优先PPS，以10W复充至≥81% | SMB5以10W复充至≥81% |

旁路在72–80%保持；复充周期锁定10W，即使采样跨到70%以下也不恢复快充。SMB5在独占接管时原子交接当前阶段，并在接管前核对硬件Full、自动旁路、用户禁止及cooling；限充不伪报硬件Full。旁路不承诺电池电气隔离或高负载下完全不补电。

电压基准沿用`2×VBAT+400mV`启动值，采用BMS/泵VBAT较高值；运行按20mV步进匹配电池变化、热/电流及功率环，在匹配点保持电压，不为了凑功率继续升压。请求上限9.5V/2.5A；输入请求取阶段功率预算、温控折算、APDO能力及2.5A的严格值并向下量化50mA。2A软件下限已移除；采用[USB-IF说明](https://www.usb.org/sites/default/files/D2T2-1%20-%20USB%20Power%20Delivery.pdf)的1A可编程限流区下界，实际电流可低于1A，硬件低负载范围须L3验证。预算不足以安全使用1A请求时停泵保留PPS，不能把最低请求当作最低实际电流。DT的工作功率下限改为既有7V总线下界×1A=7W，不是硬件保护阈值。功率是输入目标，恒压、热限额、路径损耗可令实测较低。

六路温度权重、原始trigger/clear/FCC表及电池温区未改；在下一热档既有clear点提前降低控制目标，不放宽实际安全限额。38°C提前降至18W改为可恢复；温控解除后功率预算每至少1秒最多增加1W，源端限流每至少1秒最多增加50mA。降额仍先调压使实测IBUS低于新请求至少100mA，再收紧源端限流；电池电流独立受实际热限额保护。

`WAIT_PLUG → SETTLE → RUN`；进入81%自动旁路后回WAIT_PLUG，由SMB5回差阻止提前接管。独立`THERMAL`态用于已确认的单纯热限额越限：立即停泵并核实，保留SMB5独占和PPS合约，周期刷新。全部非热安全条件仍须通过；允许低电流运行后重新请求、进入原5秒SETTLE，再经新鲜遥测核验启泵。过流绝对边界、传感器/合约/保护错误及同时存在的硬件故障不能进入该恢复态。温控预算低于可用请求区时同样保持停泵。停泵期间SMB5输入仍暂停，不声称系统正由外部旁路供电。

真实错误进入FAULT：确认泵停、固定5V合约和释放互锁后，SMB5记录独立故障锁，禁止自动电池充电；只由物理拔线清除，用户auto及cooling释放不清故障锁。无APDO的`-EOPNOTSUPP`保留普通PD承接。停止失败保留独占并重试；不独立喂watchdog。原手动旁路、旧故障回退和温控L3保留为对应旧镜像证据，不冒充本版恢复策略验收。

SETTLE保留VBAT差±50mV、无负载VBUS目标±200mV及5秒期限；RUN允许带载压降，保留VBUS上界、绝对电压/电流和switching/bus状态门控。每次主动操作10秒窗口、仅`-EAGAIN`最多三次20–25ms重试和8秒PPS刷新均保留；挂起先停泵。BQ watchdog仍为30秒档，常规成功I²C事务刷新；旧正常刷新L3与未做的到期注入分列于硬件记录。

SMB5对5/15/10W预算同时施加1/3/2A硬上限；正常运行ICL每次最多变化0.1A。按真实50mA编码记录自身写入，标准属性显式写入独立更新外部限额，避免策略值量化后被误记为外部长期上限。所有权例外仍归TD-004，温标和新保护/恢复行为归RISK-004/TD-005；无线RX/TX未改。

## 板级属性与固定 LOS 差异

固定 LOS DTS 明确禁用的保护不再以“必须启用”作为 probe 条件；MiSeed 复用其外部 thermal-FCC 路径。其余电气限制使用 LOS Umi 的有效配置值：

| 属性 / 项目 | 固定 LOS 行为与 MiSeed 处理 |
| --- | --- |
| sense resistor | LOS 没有启用 DT 属性；MiSeed 保持本机读到的 0xd0 配置，不推断电阻实值或改写 sense 位 |
| 电池 OCP / alarm | LOS 明确禁用且阈值属性注释；MiSeed 同样禁用，走 BMS 电流采样与 LOS thermal-FCC 上限，不添加猜测阈值 |
| TSBAT / TSBUS / TDIE | LOS 明确禁用；MiSeed 同样禁用，走已移植的五路 PMIC 温度、BMS 温度与 thermal-FCC 策略 |
| OVP / 总线 OCP / AC OVP | 采用 LOS Umi 明确设置的编码值，并在启泵前及控制循环中读回 |
| thermal board compensation | 只接受本机已观测的 0；五路命名 IIO 输入必须是温度类型 |

软件温控来自固定 LOS Umi 常规 profile 与动作链；主线 ADC 实际标度仍未在 MiSeed 真机验证。测试文件里的 synthetic 数值不是板级参数，不得复制到设备树。

0x2b 的 bit3 不写入，读回必须保持本机 Android `0xd0` 对应的 0；翻转即拒绝继续。此不变量不构成 VOUT_OVP 极性、阈值和失效动作的完整证明。

测试 profile 按 LOS 开放 I2C15 GPIO44/45 和 BQ IRQ GPIO68；GPIO8 未接入 LOS 的 VBUS-disable 行为。早期绑定、协商/启泵/回退和采样门控修正见[历史真机证据](../../docs/archive/hardware/2026-11-08-umi-pps-ams-retry.md)；`49b1f406` 后的 L2/L3 演进见[本轮记录](../../docs/archive/hardware/2026-11-09-umi-pps-windows-l2.md)。历史待办及错误值不作为当前运行状态。

活动类型支持列表不证明合约；Source_Capabilities、APDO Request、Accept/PS_RDY、ADC、switching 与净充电分别核实。`tcpm-log` 显式读取 `/sys/kernel/debug/usb/tcpm-*/log`，会消费协议环，读取至 EOF，内核环容量为 262144 条；不自动并入 `status`。旧 `-95` 可来自接回 Mac 后仅固定 PDO 的会话，不能代替充电器会话的错误。共享 VBUS 仍 sink/device-only。PD 与 PPS 充电均已在本机分别约 10 分钟 L3 通过并宣告稳定；Windows 串口完整导出已在 pacing 修复镜像上复验无缺段。保护/温控/恢复分项、GPIO8、RISK-010、温度标度仍待独立专项复核。

## Linux 标准接口与诊断

BQ25970 注册 `/sys/class/power_supply/bq25970-charger`，提供 `online/status/health/usb_type/voltage_now/current_now/charge_behaviour`，电压/电流为 µV/µA；ADC 尚未配置时返回 `ENODATA`，读取不消费锁存故障。`charge_behaviour=auto` 允许原有自动策略，`inhibit-charge` 停泵并回固定 PD，禁用请求跨拔插保持；故障停机未完成时不允许 auto。单独禁止泵会恢复 SMB5；禁止整个有线路径须按泵→SMB5 顺序禁止两者。SMB5 的 `input_current_limit` 为可写配置（µA），`current_max` 为只读 AICL 结果。标准通知经供电关系传到 FG，电池仍由 `qcom-battery` 提供，FG 的标准 current_now 已修正为充电正、放电负，内部原始解码不变；两个有线消费者同步迁移。SMB5 输入电流按已锁定 PM8150B buck 的 0.2 V/A 将 ADC µV 换算为 µA，OTG/boost 返回 ENODATA；见[接口续修](../../docs/archive/reviews/2026-11-10-mainline-interface-followup.md)。

thermal zone `wired-pps-thermal` 提供同一六路虚拟温度（m°C），cooling device `umi-wired-pps` 提供 0–15 档。DT trip/滞回与既有表一致；最终限流取 cooling 请求与独立保护的严格值，用户写 cooling state=0 不解除保护。该 zone 只管理有线 PPS，不能当作固定 PD、无线 RX/TX 的完整热模型。

有线固定 PD/SMB5 单独提供 `wired-pd-thermal`（电池温度，m°C）与 `umi-wired-pd`（0=允许原策略、1=停充）。DT 以已有 45°C 暖端保守停充，不引入新的温控阈值、滞回或 FCC 表；冷端/读错保护仍由 worker 独立执行。cooling 请求跨拔插和 PPS 所有权释放保持，state=0 不清用户 inhibit、也不直接启充；PPS 占用时不触碰其互锁。此实现与新 DT 的 L3 另验，完整板级热模型未宣称完成，见[完善记录](../../docs/archive/reviews/2026-11-10-linux-standard-interfaces.md)。

`python3 tools/wired_power.py` 是默认只读的标准接口消费者，输出缺失设备和属性错误；`--action inhibit-charge`/`auto` 分别请求整条有线路径禁止/自动，必须在授权真机操作时使用。Debian/UPower 可消费标准电池设备，但本轮不声称 Debian 实机集成完成。USB 诊断 `pps-status` 读取标准 uevent，`pps-stop`/`charge-stop` 写标准 `charge_behaviour`；不再使用 `fastcharge_test`。有界 `charge_test` 仅在显式启用 `CONFIG_MISEED_CHARGE_DIAGNOSTICS` 时存在，默认镜像关闭，不作为产品依赖。

### 有线旁路供电的上层调用

自动旁路：有效健康/遥测下，SOC≥81%或硬件 STATUS=Full 锁定停止电池充电；保留固定 5 V/SMB5 系统输入，SOC处于72–80%时不复充，≤71%才解除自动锁定，仍须满足独立温控/健康条件。标准 charge_control_end_threshold/start_threshold 分别读回81/71；SOC限充不伪报硬件Full。停电池充电请求成功后恢复系统输入预算，不继承收尾5 W限制，也不承诺电池电气隔离或负载高于源能力时电池绝不补电。物理拔线清除自动锁定；用户 inhibit 独立保留，写 auto 不能绕过81/71回差。已有手动旁路 L3 保留，新SOC触发、复充及交接需重新L3。

旁路供电复用已经开放的 Linux 标准 `power_supply` ABI：外部有线电源供系统运行，电池充电禁止并保持净电流近零；能力与 L3 证据归 [platforms](../../docs/platforms.md#硬件能力)。上层无需新增私有 sysfs、ioctl、Android HAL 或诊断命令；标准属性含义遵循 [内核 ABI 文档](https://raw.githubusercontent.com/torvalds/linux/master/Documentation/ABI/testing/sysfs-class-power)。

| 上层操作 | 现有接口与顺序 |
| --- | --- |
| 开启有线旁路 | 先向 BQ25970 泵的 `charge_behaviour` 写 `inhibit-charge`，再向 SMB5 写同值；保留外部输入供系统，不把 `input_current_limit=0` 当作旁路开关 |
| 退出旁路 / 恢复自动充电 | 先向 SMB5 写 `auto`，成功后再向泵写 `auto`；恢复原有保护策略，故障/停机未完成时保留 `EBUSY`，不强制清除故障 |
| 查询请求与实际效果 | 读取两路 `charge_behaviour/status/health/online` 和 `qcom-battery` 的 `status/current_now/voltage_now`；外部在线、两路停充及电池净电流共同判断效果，µA/µV 分别换算，不将读错或缺失填 0 |

现有协调消费者可直接供上层调用（以下命令在目标 Linux 上执行）：

```sh
python3 tools/wired_power.py --action status
sudo python3 tools/wired_power.py --action inhibit-charge
sudo python3 tools/wired_power.py --action auto
```

工具按名称/泵型号唯一发现设备，仅操作有线路径，JSON 返回 `supplies`、`missing` 和逐设备 `control_errors`。退出 0 表示设备齐全且控制写入无报告错误；上层仍须检查属性中的 `error`，异步轮询实际状态，不能把请求成功直接显示为已生效。控制是跨两路的有序操作，不是用户空间原子事务：禁止时即使泵写入失败仍尝试禁止 SMB5 并报告错误；恢复时 SMB5 失败即不请求泵，内核继续保持互锁。外部离线时显示为禁止充电请求已保持，不显示外部供电中；单独 `pps-stop` 仍允许 SMB5 充电，不能代替整条路径旁路请求。

写控制由 root 或显式授权的系统服务持有权限，上层普通应用通过该服务调用，避免全局放宽 sysfs 写权限。Debian 服务/界面尚待集成；状态反馈可消费标准属性和 uevent。该契约不添加持久 ABI，不修改电气阈值，不把有线结果复用于无线 RX/TX；旁路供电的控制器仍保留既有故障、温控和停机恢复边界。

DT 使用 `monitored-battery` 只指向 simple-battery；`qcom,fuel-gauge`/`xiaomi,fuel-gauge` 指向遥测提供者，`power-supplies` 表示 TCPM 电源。三份本地 binding 位于 [bindings](bindings/)，属于可校验的本地提案，未宣称上游已接受。内核内部 `umi_smb5_*` 原子互锁仍保留：通用属性不能表达失败保留所有权，不能用用户空间分步写入取代它。完整接口审查与本轮验证见[记录](../../docs/archive/reviews/2026-11-10-wired-mainline-power.md)。

`miseed-pps` 状态、请求和错误进入已有断线 RAM 日志。日志从启动后动态扩展，RAM 文件预算为 64 MiB，不覆盖旧记录；预算用尽或分配/写入失败时丢弃新增记录并累计 `storage_errors`；导出读取不可变前缀，每次仅用一条记录缓冲，重启仍会清空 RAM 日志。相同电压/电流不再每个 200 ms 控制周期重复发送 APDO；参数变化立即请求，稳定时每 8 秒刷新，为 USB-IF 的 10 秒 SPR PPS 请求超时留出调度余量（[USB-IF overview](https://www.usb.org/sites/default/files/D2T2-1%20-%20USB%20Power%20Delivery.pdf)）。PD PHY 的 `rx_irq_signal` 只表示收到 RX 中断，不表示 reset；异常失败会记录 BQ 当前/锁存故障、报警、状态、看门狗和控制寄存器原值，保持原有停泵/回退行为。TCPM 指针表用可回退到非连续内存的 kvcalloc 分配，分配失败只停用日志；环保留 262144 条，覆盖更长的协议时间线；它仍有界，`tcpm-log` 读到 EOF 也不代表环溢出前后的完整历史。详见[接收修正](../../docs/archive/research/2026-11-09-pd-receive-guard.md)。SMB5 配置沿用 LOS 3 A ICL、6 A FCC、4.45 V FV；PPS 占用期间拒绝并发普通充电命令，PPS 正常退出后恢复 SMB5 worker。挂起/关机/移除停止循环；挂起失败拒绝休眠，恢复时仅重启原可运行会话，保留手动停止与故障状态；手动停止后继续只读观察拔线。SMB5 inhibit 时拒绝 PPS 接管。

## 验证入口

- `python -m unittest discover -s tests -v`：策略、实际控制代码的合成传输故障、原充电/日志回归。
- `python3 tools/check_wired_dt.py --dtb <L2-DTB> --schema <processed-schema.json>`：定向 schema 与实际 phandle 校验，任何诊断均失败；测试需配置 MISEED_WIRED_DTB/MISEED_DT_SCHEMA 并安装 dtschema，命令见本轮记录。
- Windows 日志导出需可选依赖 `pyserial`：`python tools/collect_charge_log.py --port COM6 --output local/charging-session.log`（端口按实际枚举选择）。专用读线程持续排空 CDC-ACM，先收齐后再校验；COM RX 缓冲优先 8 MiB。救援端 `charge-log` 每 4 行 pacing/`fflush`。修复镜像 `b4026d67…d44e9` 真机全量 1102 条无缺段。检查 VID/PID、诊断身份和连续序列；缺段/超时保留原始 `.partial`，换文件名重试且不重启；最终 `.log` 不覆盖旧文件。macOS/Linux 使用实际 ACM 端口，无此依赖。
- `bash tools/build_kernel_l2.sh`：固定基线 ARM64 内核/DTB/救援镜像；校验 PPS 内建配置、对象和链接符号。
- 本机 L3：PD 与 PPS 分别约 10 分钟稳定运行见[本轮记录](../../docs/archive/hardware/2026-11-09-umi-pps-windows-l2.md)；旧 QUP/RPMh、SMB5 回退和电压样本问题见[续查记录](../../docs/archive/hardware/2026-11-08-umi-pps-quruntime-followup.md)。本版源码须对应镜像哈希的 L3，不沿用更早短时 switching 证据。

来源、PMIC 隔离与接线见[拓扑记录](../../docs/archive/research/2026-11-08-pps-topology-followup.md)。`collect_device.py --power` 可只读补充启动日志中的 ID 与缓存 PDO，不把 standalone 名称当芯片型号。不得把构建通过、短暂合约与已宣告稳定的 PD/PPS L3 混同；也不得用未完成的保护专项回写否定该结论。
