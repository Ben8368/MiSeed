# Umi PPS 充电路径与准入

此目录实现主线 TCPM → 自动 PPS 策略 → BQ25970；软件策略已按 LOS 接入自动 attach/退出，仍是测试路径而非已发布产品。Umi sink-only 测试镜像已启用 Type-C、I2C15、充电泵和 VBUS 节点；真机已确认控制器绑定。镜像 `44854093…e98a89f94c`：PD 与 PPS 分别约 10 分钟真机 L3 均通过，维护者宣告二者充电均已稳定。

## 输入与支持范围

参考 LineageOS `lineage-23.2`，固定提交 `71b13e62f057a649b77fe4062feb73ee72ad609c`；作者、许可证和文件哈希见 [NOTICE](NOTICE.md) 与[来源锁](../../manifests/sources.lock.json)。依赖本项目固定 Linux 的 PM8150B Type-C/PD PHY、TCPM power_supply 接口和现有 FG/SMB5。Android 的 usbpd、pmic-voter、私有 power_supply 属性不直接导入。

## 与 LOS Umi 的策略对照

| 项目 | 固定 LOS Umi | MiSeed 主线适配 |
| --- | --- | --- |
| 入口 | SE15 I2C、0x66 BQ25970、GPIO68；`usbpd_pm` 自动管理 PPS | 主线 TCPM + 本控制器；测试镜像 enabled，RPMh/I2C15/BQ 绑定已 L3 |
| BQ 配置 | BAT_OVP 4550 mV；battery OVP alarm 禁用；battery OCP/UCP alarm 禁用；BUS_OVP 12000/11000 mV、BUS_OCP 3750/3500 mA、AC OVP 14 V；电池 OCP 与 BQ 三路 TS 比较器禁用，sense 属性留注释；watchdog 关闭 | 按 LOS 保留明确数值，准确复现其禁用位；外部温控/FCC 路径承担软件限流。sense 位沿用本机读到的 0xd0 配置，不推断实物电阻 |
| 热控与电量 | `mi_thermald` 通过 SMB5 thermal FCC 投票；常规 Umi PPS 上限表为 16 档；PD 策略在 95% SOC 退出 charge pump | 已移植同一六路虚拟温度及 FCC 表；PPS 在 LOS 的 95% SOC 门槛退出，普通充电不再额外卡 70% |
| 运行方式 | USB/PD 事件驱动，符合条件即进入持续策略，拔线/故障/高电量时退出 | 驱动 probe 后轮询 PD attach 并自动尝试 PPS；拔线回到等待态，故障回固定 PDO 并恢复 SMB5 策略 |

来源证据为固定 LOS `umi-sm8250.dtsi`、`bq2597x_charger.c`、`pd_policy_manager.c/.h` 与 `smb5-lib.c`。自动 attach、SOC/温控策略、LOS SMB5 限流与 PPS 退出回退已移植；节点绑定已 L3；PD 与 PPS 分别约 10 分钟真机通过并宣告稳定。保护故障注入、温控标度与发布验收另项开放。

仅接受独立 BQ25970、完整 ID `0x10`；参考驱动兼容的 SC8551/SC8551A/NU2105/BQ25968 不作相同 ADC/寄存器处理。本机 Android 定点读取已确认 `0x10`，不替代 MiSeed probe。固定 PDO 为 5 V / 3 A，PPS APDO 为 3.3–11 V / 3 A；策略限制到 9.5 V、3 A 总线和 6 A 电池，SMB5 与 LOS 相同设为 3 A USB ICL、6 A FCC、4.45 V FV。这些是 LOS 配置，不是实测充电功率。

## 状态与安全退出

`WAIT_PLUG → SETTLE → RUN → DONE/FAULT`。驱动自动检测 USB-PD attach；合格后独占 SMB5、请求 APDO 并运行持续调节循环。SOC 上限为 LOS Umi `CAPACITY_TOO_HIGH_THR=95`。每个 PD/泵操作有 10 秒完成检查，SETTLE 仍有 5 秒窗口；这些是 I/O/启动超时，不是会话时长。

先独占 SMB5、停主充并暂停其 USB 输入，再配置并逐项读回保护；激活 TCPM 的 `ONLINE=2` 后读取 APDO 能力。电压/电流请求使用微伏/微安接口，读取实际协商结果，再检查电池和泵 ADC。初始目标为 `2 × VBAT + 400 mV`，之后按 20 mV 调节；电压、电池电流、总线电流三个限制取最严格方向。ADC 通道对齐固定 LOS：IBUS/VBUS/VAC/VBAT 启用，泵 IBAT 禁用（0x15=0x2f）；电池电流取 BMS 净值，不把净放电直接视为泵反向，总线负电流仍停止。电压请求保留作 PPS 刷新；当前电流与目标一致时跳过重复写入。激活、电压和降额电流请求仅对 `-EAGAIN` 追加最多三次重试，间隔 20–25 ms，仍检查原期限。控制器保留额外 ADC 一致性门控；SETTLE 在泵关闭、绝对采样限值与故障检查通过、VBUS 不超过目标 +200 mV 时于原 5 秒窗口内等待两路转换收敛；启泵仍须满足 VBAT 差 ±50 mV 和 VBUS 目标 ±200 mV，RUN 不再以跨芯片 VBAT 差单独停泵，各路绝对限值仍须满足；运行 VBUS 保留目标 +200 mV 上界，低于目标的固定差值仅作启泵前检查，带载时依原绝对范围、switching 和 bus_low/high 保护，不把源端合约值当作泵端 ADC；调压及收尾采用 BMS/泵 VBAT 较高值。

任何活动期间遥测错误、合约不匹配、保护读回不符、温控/过压/过流或硬件锁存故障都会停泵。退出读回 CHG_EN 与 switching 状态，返回固定合约并确认 5 V / ≤3 A，最后释放 SMB5 并恢复 LOS 普通主充策略。无法停止时保留独占、记录错误并重试，不发送独立 watchdog 保活。硬件失效无法被软件保证消除，必须断开充电器。

BQ watchdog 与固定 LOS `bq2597x_init_device()` 一致关闭：仅设置 0x0b bit2，保留频率/时间选择位，删除周期刷新。关闭位读回不符仍退出，关闭状态下不把历史 timeout flag 当作当前超时。不再要求验证一个未启用的泵 watchdog 刷新/到期机制；关闭模式没有该硬件超时停泵保障，软件循环和 I/O 挂死后的退出能力仍须如实分项验证。来源与变更见[对齐记录](../../docs/archive/reviews/2026-11-08-los-watchdog-alignment.md)。

阻塞 I/O 返回后重新检查操作窗口：PPS 激活、电压/电流请求、保护复核与启泵分别检查，SETTLE 还受 5 秒窗口约束。超出操作窗口时不继续提交主动请求，进入停泵/固定合约回退；清理操作不被窗口拦截。这不能中止已经发出的挂死事务，也不是硬实时保证。

常规温控取自本机 LOS 有效配置及已核实的动作映射：电池温度加五路 PMIC IIO 温度，计算虚拟温度、九档滞回，再查原 16 档 PPS FCC 表。未知屏幕状态取两种动作的严格值；读取错误、ADC 端点、整组采样超过 200 ms 均停止。降额先降低 PD 电流，单次会话不再提高；超出实测电流上限或降至现有泵工作范围之外则停止。原系统 3 秒延迟降额不移植。细节和 L1/L2 见[移植记录](../../docs/archive/research/2026-11-08-los-thermal-port.md)，主线温度标度仍待 L3。

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

活动类型支持列表不证明合约；Source_Capabilities、APDO Request、Accept/PS_RDY、ADC、switching 与净充电分别核实。`tcpm-log` 显式读取 `/sys/kernel/debug/usb/tcpm-*/log`，会消费协议环，读取至 EOF，内核环容量为 262144 条；不自动并入 `status`。旧 `-95` 可来自接回 Mac 后仅固定 PDO 的会话，不能代替充电器会话的错误。共享 VBUS 仍 sink/device-only。PD 与 PPS 充电均已在本机分别约 10 分钟 L3 通过并宣告稳定；保护/温控/恢复分项、GPIO8、RISK-010、温度标度及主机串口完整导出仍待独立专项复核。

## 诊断接口

控制状态位于唯一绑定 I2C 设备的 `fastcharge_test`。USB 诊断支持 `pps-status`、`pps-stop`；合格板级节点启用后，PPS 会在 USB-PD attach 自动进入；`charge-stop` 会先尝试停止已绑定 PPS，再停普通充电。当前测试镜像已启用相关节点，控制器已绑定；命令不会猜 I2C 总线编号。

`miseed-pps` 状态、请求和错误进入已有断线 RAM 日志。日志从启动后动态扩展，RAM 文件预算为 64 MiB，不覆盖旧记录；预算用尽或分配/写入失败时丢弃新增记录并累计 `storage_errors`；导出读取不可变前缀，每次仅用一条记录缓冲，重启仍会清空 RAM 日志。相同电压/电流不再每个 200 ms 控制周期重复发送 APDO；参数变化立即请求，稳定时每 8 秒刷新，为 USB-IF 的 10 秒 SPR PPS 请求超时留出调度余量（[USB-IF overview](https://www.usb.org/sites/default/files/D2T2-1%20-%20USB%20Power%20Delivery.pdf)）。PD PHY 的 `rx_irq_signal` 只表示收到 RX 中断，不表示 reset；异常失败会记录 BQ 当前/锁存故障、报警、状态、看门狗和控制寄存器原值，保持原有停泵/回退行为。TCPM 指针表用可回退到非连续内存的 kvcalloc 分配，分配失败只停用日志；环保留 262144 条，覆盖更长的协议时间线；它仍有界，`tcpm-log` 读到 EOF 也不代表环溢出前后的完整历史。详见[接收修正](../../docs/archive/research/2026-11-09-pd-receive-guard.md)。SMB5 配置沿用 LOS 3 A ICL、6 A FCC、4.45 V FV；PPS 占用期间拒绝并发普通充电命令，PPS 正常退出后恢复 SMB5 worker。挂起/关机/移除停止循环；挂起失败拒绝休眠，恢复时仅重启原可运行会话，保留手动停止与故障状态；手动停止后继续只读观察拔线。SMB5 inhibit 时拒绝 PPS 接管。

## 验证入口

- `python -m unittest discover -s tests -v`：策略、实际控制代码的合成传输故障、原充电/日志回归。
- Windows 日志导出需可选依赖 `pyserial`：`python tools/collect_charge_log.py --port COM6 --output local/charging-session.log`（端口按实际枚举选择）。先有界收包后校验，检查 VID/PID、诊断身份和连续序列；缺段/超时保留原始 `.partial`，换文件名重试且不重启；最终 `.log` 不覆盖旧文件。macOS/Linux 使用实际 ACM 端口，无此依赖。
- `bash tools/build_kernel_l2.sh`：固定基线 ARM64 内核/DTB/救援镜像；校验 PPS 内建配置、对象和链接符号。
- 本机 L3：PD 与 PPS 分别约 10 分钟稳定运行见[本轮记录](../../docs/archive/hardware/2026-11-09-umi-pps-windows-l2.md)；旧 QUP/RPMh、SMB5 回退和电压样本问题见[续查记录](../../docs/archive/hardware/2026-11-08-umi-pps-quruntime-followup.md)。本版源码须对应镜像哈希的 L3，不沿用更早短时 switching 证据。

来源、PMIC 隔离与接线见[拓扑记录](../../docs/archive/research/2026-11-08-pps-topology-followup.md)。`collect_device.py --power` 可只读补充启动日志中的 ID 与缓存 PDO，不把 standalone 名称当芯片型号。不得把构建通过、短暂合约与已宣告稳定的 PD/PPS L3 混同；也不得用未完成的保护专项回写否定该结论。
