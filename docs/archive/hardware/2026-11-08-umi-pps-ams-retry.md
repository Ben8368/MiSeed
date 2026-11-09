# Umi PPS 协议日志与 AMS 碰撞复测

| 项 | 证据 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08 至 2026-11-09，Codex；Darwin 25.6.0 / arm64 / zsh |
| 授权范围 | 本轮用户要求实现 PPS、对照 LOS、构建后 Fastboot 启动并读取充电器测试后的日志；仅 RAM 启动、充电测试及诊断，不刷写分区 |
| 设备 / 固件 | 唯一 Fastboot 设备报告 `product=umi`、`unlocked=yes`；沿用本机 CSOT 配置，未重采面板 ID 或底层固件身份 |
| 源码身份 | 工作分支 `codex/umi-pd-pps-fastcharge`，输入 HEAD `c5f01e4` 加本地未提交修改；固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、GCC 13.3.0、`kernel/umi-l2.config` |
| LOS 对照 | GitHub 查询确认 `lineage-23.2` 仍指向 `71b13e62f057a649b77fe4062feb73ee72ad609c`。其 `drivers/usb/pd/qpnp-pdphy.c` 同样以 PD 2.0 初始化 PHY；`policy_engine.c` 支持 PD 3.0。没有据初始化版本猜测或改写 PHY；未复制新外部源码 |
| 回退 | 停泵并读回、回固定 PDO、释放 SMB5；清理失败保留独占重试。可用救援 `charge-stop`、`bootloader` 返回原生 Fastboot，再启动原系统；异常断开充电器。本轮不访问分区 |

## 诊断入口修复与第一轮 L3

旧记录 `.work/umi-pps-tcpm-b1ca2f3/serial-capture.txt` 中 `tcpm-log` 实际返回路径不存在。固定 Linux `tcpm_debugfs_init()` 将日志放在 `/sys/kernel/debug/usb/tcpm-<device>/log`，旧救援命令错误地多加了 `tcpm/`；此前合成测试也模拟了错误目录。本轮按真实布局修正并只遍历 `tcpm-` 项，显示上限从 128 行改到内核环的 1024 条，避免消费了后面的记录却未输出。读取仍仅由显式 `tcpm-log` 触发。

- 命令：`python3 -X utf8 tools/check_repo.py`、`python3 -X utf8 -m unittest discover -s tests -v`、`git diff --check`；macOS 97 项、18 项跳过；Linux 同命令 97 项全部通过，退出码均为 0。
- `bash tools/build_kernel_l2_macos.sh` 返回 0。镜像 SHA-256 `54f99e5a7262364bd1f45096702d2d402f62d7ef289490f8bb2fb2c9c1b2ff8d`，`fastboot getvar product` 后执行 `fastboot boot .work/boot-l2-arm64/boot.img`，Sending/Booting 均 OKAY；随后 CDC 串口 `version`、`pps-status`、`status`、`tcpm-log` 均响应。
- 用户接充电器约 30 秒后接回 Mac。只读执行 `pps-status`、`tcpm-log`、`status` 和 `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/pps-tcpm-charger-timeline.log --timeout 30`，退出码为 0；充电时间线 479 条，覆盖数 0。原始长日志仅在忽略目录保留。
- boot 89.782 s 的 Source_Capabilities 包含五档固定 PDO 和 APDO 5–11 V / 5 A；89.933 s 取得 5 V / 3 A PPS，90.056 s 电压请求完成并取得 8.86 V / 3 A。
- 随后驱动立即再次写同样的 3 A，TCPM 的 TX 被 discard，源端同时发出控制消息 `0x16`，策略以 `-EAGAIN`（-11）停泵并成功回固定 5 V / 3 A；90.203 s 日志 `owned=0`。此轮没有启泵运行证据。
- boot 124.757 s 接回 Mac 后收到仅固定 5 V / 3 A 的 Source_Capabilities，124.924 s 的 `-95` 是该后续会话，不能拿它代替充电器阶段的失败原因。
- 当时 SOC 92–94%、温度约 34–35°C；未绕过 LOS 的 95% PPS 准入门槛。等待下一镜像时执行 `charge-stop`，读回 `state=0 error=0 stop_pending=0`。

## 协商修复

保留电压请求作为 PPS 刷新；读回电流已相同时不再发起第二个 AMS。激活、电压和降额电流请求只对 `-EAGAIN` 最多追加三次重试，间隔 20–25 ms，每次仍检查原操作及 SETTLE 窗口；超时和其他传输错误仍立即进入原清理。未改变电气限制或保护门槛。增加协商、启泵及失败遥测日志，合成反例覆盖三个属性的暂忙、持续忙、超时后禁止续请求、重复电流和运行期碰撞。

本轮修改后的专项 `tests.test_pps` 4 项通过，完整 Linux 97 项通过，macOS 97 项（18 项跳过）通过；第二版 L2 返回 0，镜像 `1dad9c3d895a0c6b78aad15fd025b661da0885924c95a9e2de4dacfe7f81c6c5`（7,806,976 bytes）。构建有两处新生成 `modules.order` 的 0.21/0.53 秒时钟偏移警告；使用新解包源码及干净输出目录，最终链接、内建对象及镜像检查均通过。该警告不作为无警告构建的证据。


## 第二轮 L3：协商完成，ADC 门控退出

重新核实唯一 Umi、解锁状态及哈希后执行同一 `fastboot boot` 命令，退出码 0、Sending/Booting OKAY。用户启动后立即改接充电器，故主机暂时无 USB；用户确认完成测试并接回后，救援 `pps-status`、`tcpm-log`、`status` 均响应。`collect_charge_log.py` 同参数改用输出 `.work/pps-retry-charger-timeline.log`，返回 0，286 条记录、覆盖 0。

boot 11.274 s 协商到 8.82 V / 3 A，11.275 s 进入 `PPS_SETTLE`；没有再次发相同电流请求。11.523 s 样本为 VBUS 8538 mV、IBUS 0 mA、BMS 4212 mV、泵 VBAT 4205 mV、电池充入方向电流 -384 mA、泵 IBAT 6 mA、fault/alarm/bus_low/bus_high/switching 全为 0。该 VBUS 比请求低 282 mV，触发额外的 ±200 mV 一致性门控，返回 `-ERANGE`（-34）；11.703 s 已回固定 5 V / 3 A 且 `owned=0`，未启泵。用户观察表头一直显示 PD；与设备端短暂成功协商 PPS 的日志分列，不能以表头标签推翻或替代协议证据。后续 Mac 会话的 `-95` 仍与充电器会话分开。

固定 LOS 的 `bq2597x_init_adc()` 使用 13-bit 平均采样；本项目也已配置该模式。此单样本不能区分平均采样滞后、持续电压偏差或其他原因。第三版仅在 SETTLE、泵仍关闭、其他门控通过且 VBUS 低于目标窗口时，允许在原 5 秒期限内等待；启动启泵和 RUN 仍须满足原 ±200 mV，未放宽电气限值、过压或其他保护。新增反例覆盖采样收敛、启动超时、同时硬件故障、过高电压和运行中失配；专项与完整 Linux 97 项通过、macOS 97 项（18 项跳过）通过。第三版构建返回 0，镜像 SHA-256 `50be50c18cc969d9050ebcb5b036a6605821f81082c4df60e63b7ca9f9aaf465`；新建输出中仍有短时文件时钟偏移警告，最终链接及对象/镜像检查通过。


## 第三轮 L3：已启泵，电流门控退出

再次核实 Umi、解锁状态及镜像哈希后 `fastboot boot .work/boot-l2-arm64/boot.img` 返回 0、Sending/Booting OKAY。用户测试后接回 USB，执行 `pps-status`、`tcpm-log`、`status` 和 collector，输出 `.work/pps-adc-settle-charger-timeline.log`，返回 0，224 条、覆盖 0。

21.513 s 协商 8.80 V / 3 A；21.762 s VBUS 8524 mV，驱动保持停泵等待，22.041 s 后续样本通过原门控并启泵。22.173 s 的硬件状态 `switching=1`，但 VBUS 8741 mV、IBUS 46 mA、BMS VBAT 4198 mV、泵 VBAT 4231 mV、BMS 净充入方向电流 -386 mA、泵 IBAT -7 mA。无 fault/alarm/bus_low/bus_high；额外的泵 IBAT 非负门槛和 RUN 的 BMS 净充入非负门槛导致退出，22.330 s 恢复固定 PD，`owned=0`。这证明采样等待可收敛并曾开启 switching，不能证明持续泵充电。用户报告“起初小于 1 W，随后约 9 W PD”，只作表头观察，不冒充设备功率测量。

## 第四版：LOS 电流采样对齐

固定 LOS `bq2597x_init_adc()` 禁用泵 IBAT ADC，启用 IBUS/VBUS/VAC/VBAT，对应寄存器 0x15 为 0x2f；`usbpd_pm_update_cp_status()` 从 BMS 获取电池电流，使用泵 IBUS 和 BMS 电流调节。MiSeed 原先开启了所有 ADC，并额外以泵 IBAT 和 BMS 净电流的符号作为运行准入。BMS 净放电可能表示系统负载大于泵供给，不等价于泵反向；本机 sense 实物值未核验，不增加猜测电流容差。

第四版将 ADC 通道和限流采样路径对齐上述 LOS：不再启用、读取或依赖泵 IBAT，删除把 BMS 净放电直接当成泵反向的判断。仍保留 BMS 有效值范围、6 A/thermal FCC 充入上限、非负 IBUS 及 3 A/降额上限、电压一致性、保护读回、switching、温控和原停泵/固定 PD/SMB5 恢复。真实负 IBUS 反例仍须停泵，净放电及未使用 IBAT 的 -7 mA 合成输入可继续受限调节；没有新增电流、电压、温控或电阻参数。该变更移除了 MiSeed 的额外门控，必须单独 L3 复验，不能沿用第三版启泵证据声称本版稳定。

专项 `tests.test_pps` 4 项通过；完整 Linux 97 项通过、macOS 97 项（18 项跳过）通过。第四版 `bash tools/build_kernel_l2_macos.sh` 返回 0，7,806,976-byte 镜像 SHA-256 `464f8c1b12bea511ce1ca737254e349f5b0ee0ef90a3a6e2cfcde907b8cd0b89`；保存到忽略路径 `.work/pps-los-current-20261109/boot.img`，配置/对象及输入哈希同目录留存。构建使用干净输出；存在新生成 `.asm-offsets.s.cmd` 时间戳超前 0.19 秒的时钟警告，最终链接、对象和镜像检查通过，仍不声明无警告构建。用户在 2026-11-09 明确要求暂缓真机测试，待自然放电至低于 90% 后再接回；停止向设备发送命令。当时最后一次尝试 `charge-stop` 时 USB 串口已不存在，未发送命令，不能声称已成功停充；用户正在自行放电。待恢复后先核对当前连接、SOC、设备及镜像哈希，再 RAM 启动第四版并取日志。

截至该暂停点，第四版尚无 L3，代码和记录未提交/推送；后续恢复记录如下。所有测试均未刷写分区。


## 第四轮 L3：低 SOC 恢复与电池电压一致性门控

2026-11-09 用户接回 Fastboot，撤销前述暂停。宿主仍 Darwin 25.6.0 / arm64 / zsh；核对唯一 Umi 的 `product=umi`、`unlocked=yes`，逐项核验保存的配置、源码及镜像 SHA-256，均匹配。执行 `.work/host-tools/platform-tools/fastboot boot .work/pps-los-current-20261109/boot.img`，返回 0，Sending/Booting OKAY；救援 `version`、`pps-status`、`charge`、`tcpm-log` 均响应，SOC 90%、温度约 36°C。该 SOC 不宣称低于 90%，符合原低于 95% 的策略准入。

用户连接同一充电器约 30 秒后接回。读取 `pps-status`、`tcpm-log`、`status`，再执行 `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/pps-los-current-20261109/charger-timeline.log --timeout 30`，退出码 0、370 条、覆盖 0。boot 58.805 s 协商到 8.86 V / 3 A PPS，59.053 s 的首个 SETTLE 样本：VBUS 8804 mV、IBUS 0 mA、BMS VBAT 4222 mV、泵 VBAT 4168 mV、BMS 充入方向电流 9 mA；fault/alarm/bus_low/bus_high/switching 全为 0。两路电池电压相差 -54 mV，超过原 ±50 mV 一致性门槛，触发 `-ERANGE`，本版没有启泵；59.230 s 回固定 5 V / 3 A，`owned=0`。之后 Mac 会话的 `-95` 单列，不代表该充电器不支持 APDO。

修复等待期间执行 `charge-stop` 和 `pps-status`，均成功响应，读回 `state=0 error=0 stop_pending=0`。至此第四版有 RAM/协议/安全退出 L3，持续泵充电仍未验证。

## 当前源码：启动电压一致性等待；按用户要求先交草稿 PR

最新修改将启动等待扩展到 BMS/泵 VBAT 的独立转换差异：仅 SETTLE、泵仍关闭、绝对电压/电流/温控/故障检查均通过且 VBUS 不超过目标 +200 mV 时等待。保持原 5 秒期限，不继续主动升压；必须同时满足原 VBAT 差 ±50 mV 和 VBUS 目标 ±200 mV 才能启泵。负 IBUS、硬件故障、过压及 RUN 失配仍退出。新增合成反例覆盖 ±54 mV 收敛、未收敛超时、同时故障/负 IBUS/过压及运行期失配，不引入猜测电气阈值。

在用户要求停止前，最新源码专项 4 项与完整 Linux 97 项已通过，macOS 97 项（18 项跳过）通过；对应本地日志 `.work/pps-battery-adc-settle-tests-{linux,macos}.log`。随后用户明确要求“这次先不构建测试了，先提交 PR”。正在运行的 L2 容器已停止，构建进程退出码 137；没有完整的新 L2，也没有最新源码的新镜像或 L3。之后仅静态自查与提交元数据检查，不再运行构建或产品回归，不再操作真机。

用户已授权提交/推送并更新既有草稿 [PR #3](https://github.com/Ben8368/MiSeed/pull/3)，保持草稿、不合并。精确提交和远端 CI 以 PR 当前 HEAD 为准，旧版 L2/L3 不转记为新 HEAD 通过；原始长日志和镜像均保留在忽略目录。未覆盖：最新 L2/L3、持续 PPS 功率、完整保护故障注入、温度/电流标度、停止恢复与拔插的完整闭环、GPIO8、长期稳定及开发者以外专项复核。
