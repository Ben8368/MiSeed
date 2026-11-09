# 2026-11-08 LOS Umi 自动 SMB5 与 PPS 策略移植

## 依据与改动

按固定 LineageOS Umi 提交 `71b13e62f057a649b77fe4062feb73ee72ad609c` 移植已有 LOS 策略，不复制下游 Android PD 实现：

- SMB5 改用 LOS Umi 的 3 A USB ICL、6 A FCC、4.45 V FV、15–45°C 温区和自动充电策略；去掉原 MiSeed 70% SOC 停止及其 65% 滞回。
- PPS 驱动从 probe 后轮询 PD attach，满足现有 LOS 电池/SOC/温控条件后请求 APDO，运行持续调节；高电量/taper/故障时停泵、退回 5 V PDO 并恢复 SMB5 worker。原 60/600 秒只保留普通充电诊断命令，不再约束 PPS。
- 固定 PDO 对齐 5 V / 3 A；BQ watchdog、battery OCP/UCP alarm 和三路 TS comparator 保留 LOS 的关闭配置，未添加原树没有的属性或电气阈值。
- `pps-stop` 保留为人工停止；自动策略重试清理失败。救援台命令、Kconfig、PPS 说明和风险记录同步更新。

sink-only 测试镜像已启用 `umi-pps.dtsi` 的 Type-C、I2C15、泵和 VBUS 节点，并完成 ARM64 L2 与 RAM 启动。用户提供的充电器照片显示 PD 5.2 V / 2.7 A；同次救援台日志的 USB supply 类型为 `Unknown [SDP] DCP CDP`、输入约 4.46 V / 0.60 A，`pps-status` 无已绑定控制器。因此尚不能证明设备侧 PD 合约或 PPS APDO 成功，泵路径也未验证。

## Audit 与验证

修改后、验证前已输出 🚦 Audit Report。高风险失败/恢复路径已由开发者检查；独立专项复核尚未完成；PPS 控制器未绑定且保护/停止恢复 L3 未完成，不能宣称真机 PPS 可用或合并。

| 环境 / 命令 | 结果 |
| --- | --- |
| Darwin 25.6.0 / arm64 / zsh：`git diff --check` | 0 |
| 同宿主：`python3 -X utf8 tools/check_repo.py` | 0，L0 通过 |
| 同宿主：`bash tools/build_kernel_l2_macos.sh` + `bash tools/build_kernel_l2.sh` | ARM64 L2 成功；Colima/Rosetta 固定工具链，镜像 SHA-256 `9ffbb5794bc0988356d247f63a1b8259e7f9c90da5440eca60592c15fc8d3b29` |
| `python3 -m unittest discover -s tests` | 95 项通过，16 项因需 Linux GCC/伪终端跳过 |
| MiSeed 设备 L3 | `fastboot boot` RAM 启动成功；照片显示充电器端 PD 5.2 V / 2.7 A；救援台 `pps-status` 无控制器，设备侧合约/PPS 未证实 |

后续查明 TCPM/I2C15 控制器探测，继续完成设备侧 PD/PPS、保护/温控及退出/恢复验证。仅 RAM 启动；未刷写分区。

## 首次 PPS 镜像后的 GPIO allowlist 跟进

旧测试镜像 RAM 启动后 `pps-status` 找不到绑定控制器。对照 LOS/主线接线发现 TLMM 原 mask 仅开放 UART12 GPIO34/35，因此 I2C15 GPIO44/45 与 BQ IRQ GPIO68 仍不可申请。本次将 mask 收敛为仅开放 UART34/35、I2C15 44/45、泵 IRQ68；GPIO8 继续保留，因为 LOS VBUS-disable 尚未接入 SMB5 路径。

| 验证 | 结果 |
| --- | --- |
| `python3 -m unittest discover -s tests` | 95 项通过，16 项因需 Linux GCC/伪终端跳过 |
| `python3 -X utf8 tools/check_repo.py` / `git diff --check` | 通过 |
| ARM64 L2 + ABL isolation | 成功；新 boot SHA-256 `44dd30354492eba377843e942db9b06ec07e66048a8b6c914e046b9915e965b0` |
| 新 mask RAM boot | 未执行：设备当前未连接主机 |

该 GPIO 修复尚未推送；新 mask 的运行时绑定与 PPS 结果待设备重新连接后验证。
