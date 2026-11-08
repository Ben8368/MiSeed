# Umi PPS 充电路径与准入

此目录实现主线 TCPM → PPS 策略 → BQ25970 的受控测试路径；不是已验收的快充产品。构建会编译该路径，设备树中的 Type-C、I2C15 和充电泵保持 disabled。不得仅修改 status 后上机。

## 输入与支持范围

参考 LineageOS `lineage-23.2`，固定提交 `71b13e62f057a649b77fe4062feb73ee72ad609c`；作者、许可证和文件哈希见 [NOTICE](NOTICE.md) 与[来源锁](../../manifests/sources.lock.json)。依赖本项目固定 Linux 的 PM8150B Type-C/PD PHY、TCPM power_supply 接口和现有 FG/SMB5。Android 的 usbpd、pmic-voter、私有 power_supply 属性不直接导入。

仅接受独立 BQ25970、完整 ID `0x10`；参考驱动兼容的 SC8551/SC8551A/NU2105/BQ25968 不作相同 ADC/寄存器处理。设备 ID 必须另行核实。固定 PDO 仅声明 5 V / 0.5 A，PPS APDO 声明 3.3–11 V / 3 A；控制器限制到 9.5 V、3 A 总线和 6 A 电池，数值来自参考策略，不是实测充电功率。

## 状态与安全退出

`OFF → WAIT_UNPLUG → WAIT_PLUG → SETTLE → RUN → DONE/FAULT`。每次显式准备后必须先检测到断开，再检测接入；没有自动重试启泵或开机启泵。60/600 秒期限从接入开始，包含协商时间。PPS 测试不豁免 SOC 上限 70%。

先独占 SMB5、停主充并暂停其 USB 输入，再配置并逐项读回保护；激活 TCPM 的 `ONLINE=2` 后读取 APDO 能力。电压/电流请求使用微伏/微安接口，读取实际协商结果，再检查电池和泵 ADC。初始目标为 `2 × VBAT + 400 mV`，之后按 20 mV 调节；电压、电池电流、总线电流三个限制取最严格方向。控制器采用额外的保守 ADC 一致性/偏差门控，这些属于本项目策略，尚待 L3 确认。

任何遥测错误、合约不匹配、保护读回不符、温控/过压/过流、硬件锁存故障或期限结束都会停泵。退出读回 CHG_EN 与 switching 状态，返回固定合约并确认 5 V / ≤0.5 A，最后释放 SMB5；主充仍保持 inhibit，不自动恢复充电。无法停止时保留独占、记录错误并重试，不发送独立 watchdog 保活。硬件失效无法被软件保证消除，必须断开充电器。

BQ watchdog 选择 5 秒，刷新与成功控制循环绑定。其寄存器编码来自参考头文件；参考驱动自身关闭 watchdog，公开 TI 简版资料没有完整计时器语义。因此刷新、到期停止及 I/O 挂死后的保护效果仍是**运行准入阻断项**，不能把合成测试当作证明。也不移植关闭电池/总线/芯片温控的配置。

## 必须补齐的板级属性

驱动没有以下参数的默认值，缺一即拒绝 probe：

| 属性 | 单位 / 校验 |
| --- | --- |
| `ti,sense-resistor-micro-ohms` | µΩ；芯片编码仅支持 2000/5000，必须确认实板 |
| `ti,bat-ocp-milliamp` / `ti,bat-ocp-alarm-milliamp` | mA；精确编码、alarm < trip，须确认电池与路径额定能力 |
| `ti,tsbat-threshold-code` / `ti,tsbus-threshold-code` | 原始 8 位编码；必须匹配实板热敏网络 |
| `ti,die-alarm-decicelsius` / `miseed,die-max-decicelsius` | 0.1°C；软件停止值必须低于硬件 alarm |

已知 VBUS/VBAT OVP、总线 OCP 和 AC OVP 取自固定 Umi DTS；仍须独立审查其适用性。测试文件里的 synthetic 数值不是板级参数，不得复制到设备树。

启用前还需：核实 I2C15 `0x884000`、地址 `0x66`、GPIO68 中断；解决 RISK-010 重复 PMIC 与 TD-002 TLMM；确认 Type-C 的 `pm8150b_vbus` 供给（驱动已编入，但该节点仍禁用）及电源路径 GPIO；完成保护专项复核。共享 VBUS 源端不得被其他节点重新驱动；仅允许 sink/device。

## 诊断接口

控制位于唯一绑定 I2C 设备的 `fastcharge_test`，不是 BQ 通用 power_supply 的无保护启用属性。USB 诊断支持 `pps-status`、`pps-test`、`pps-test-10min`、`pps-stop`；`charge-stop` 会先尝试停止已绑定 PPS，再停普通充电。默认镜像无合格启用节点时明确报告不可用；不会猜 I2C 总线编号。

`miseed-pps` 状态、请求和错误进入已有断线 RAM 日志。原 0.5 A 测试继续独立工作，但独占期间拒绝并发普通充电命令。探测不协商 PPS；挂起/关机/移除停止会话；无法安全停止时拒绝挂起。

## 验证入口

- `python -m unittest discover -s tests -v`：策略、实际控制代码的合成传输故障、原充电/日志回归。
- `bash tools/build_kernel_l2.sh`：固定基线 ARM64 内核/DTB/救援镜像；校验 PPS 内建配置、对象和链接符号。
- 独立授权的本机 L3：先验证不启泵的读回/身份，再验证保护与退出，最后逐步评估 PPS。具体证据要求见 [QUALITY](../../docs/QUALITY.md)，本轮没有执行设备操作。
