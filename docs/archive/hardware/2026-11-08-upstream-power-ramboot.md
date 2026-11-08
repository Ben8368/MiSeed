# 2026-11-08 电源编译移植版诊断镜像 RAM 启动

## 范围与输入

| 项 | 本轮事实 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08，Codex；宿主 Darwin 25.6.0 / arm64 / zsh |
| 授权 | 维护者明确回复“授权本次诊断 RAM 启动及返回 Android”；允许一次标准 `adb reboot bootloader`（包括可能的 BCB/misc 路径）、指定镜像 `fastboot boot`、只读日志检查与返回 Android |
| 边界 | 不刷写、不手工访问分区/NV、不加载 FG/SMB5 模块、不控制充电；没有使用 adb root |
| 设备 / 变体 | 唯一设备 A，ADB / Fastboot 均为 umi；CSOT 为既有参考配置，本次没有重新验证面板/触控身份；不公开序列号或 boot UUID |
| 原系统 | Android 16，kernel `4.19.325-cip131-st15-perf-g71b13e62f057`；前后均 `sys.boot_completed=1`、ADB `uid=2000(shell)`；底层固件未重新读取 |
| 仓库 / 输入 | `codex/umi-upstream-power-port`，HEAD `2acf745` 加未提交的编译移植；Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；构建与逐文件输入见[来源评估](../research/2026-11-08-upstream-power-port.md) |
| 启动时来源锁 | `manifests/sources.lock.json` SHA-256 `722966288128e74f550e3311c1f7cfacb793c09a6d9ee54c2e68903806d610da`；验收后的状态字段另行更新，不改变已测载荷 |
| 载荷 | `.work/upstream-power-port-l2-20261108/boot.img`，7,397,376 B，SHA-256 `3e7083a7ca5f8dc4bc9a3901305fe80add064f9b324bfe47b6cd3e92e383489f`；仅 RAM boot |
| 配置 / 工具 | resolved config SHA-256 `a938b008ea0dbc06d93374aea8b40c828953d28720b71ba3841dad344ac2367c`；Ubuntu 24.04 arm64 容器 / GCC 13.3.0 / GNU make 4.3 |
| 前置 / 回退 | 哈希核对；唯一 umi；已解锁、原生 Fastboot。正常回退为串口 `bootloader` → `fastboot reboot`；USB 失联时电源＋音量下回 Fastboot，必要时长按电源恢复原系统 |

## 实际操作与结果

主机命令中的 `<设备A>` 为脱敏占位符，实际调用均用预先锁定的同一序列号。未重试 RAM 启动，未执行 flash / erase / format / lock。

1. `.work/host-tools/platform-tools/adb devices`，对唯一在线设备逐项读取 `ro.product.device`、`ro.build.version.release`、`sys.boot_completed`、`uname -r`、`id`、`/proc/sys/kernel/random/boot_id`。确认原系统启动完成、普通 shell，随后再次核对指定镜像 SHA-256。
2. `adb -s <设备A> reboot bootloader` 退出 0，仅执行一次。Fastboot 核实 `product=umi`、`unlocked=yes`、`is-userspace=no`，三个 `getvar` 均退出 0。
3. `fastboot -s <设备A> boot /Users/ben/Project/Code/MiSeed/.work/upstream-power-port-l2-20261108/boot.img`：Sending 7224 KB 和 Booting 均 OKAY，退出 0。
4. `/dev/cu.usbmodem101` 枚举成功。串口依次执行 `version`、`mounts`、`status`，均返回诊断提示符；运行内核为 Linux 6.18.60，版本包含 MiSeed / Ubuntu2404 / GCC 13.3.0。
5. 两次执行 `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/upstream-power-ramboot-20261108/<不同文件名>.log --timeout 20`，分别保存 `charge-first.log`、`charge-replay.log`，均退出 0。
6. 串口执行 `bootloader`，设备直接回原生 Fastboot；重新核实唯一设备和 `product=umi`、`is-userspace=no`。`fastboot -s <设备A> reboot` OKAY，退出 0。
7. 轮询同一设备 ADB，确认 `sys.boot_completed=1`，型号/Android release/内核与前置匹配、boot UUID 已改变、ADB 仍为普通 shell。最终设备保持 Android 在线。

## 观测与证据等级

| 检查 | 结果 |
| --- | --- |
| 控制器 / 重启模式 | PDC 绑定 `qcom_pdc`；SPMI 绑定 `spmi_pmic_arb`；PON 绑定 `qcom-pon`；DT `mode-bootloader=0x2` |
| 存储挂载 | 仅 rootfs、devtmpfs、proc、sysfs、只读 debugfs、configfs；没有块设备文件系统挂载 |
| 日志导出 | 两份均 149 条，seq 0..148 连续，起止标记完整，覆盖计数 0；两次导出的记录逐字相同，非消耗式重放通过 |
| 观察器 | 36 条 USB/SMB5 状态记录，`read_error` 计数 0；最后一条为 `usb_present=1 hw_phase=fullon chg_enable=1 ... control=read-only` |
| 返回原系统 | Linux → 原生 Fastboot → Android 16 成功；原内核恢复且启动完成 |

`chg_enable=1` / `fullon` 是继承的硬件状态与状态机读值，**不代表 MiSeed 启用了充电或测得净充入电流**。FG/SMB5 仅独立模块 L2，既没有进入 Image/initramfs，也没有在本轮设备加载。

L3 仅覆盖这个哈希的诊断镜像 RAM 启动、USB 串口、只读状态/日志导出及返回 Android。没有本轮物理断线/充电器切换、FG 测量精度、主动充电、过压/过温/过流保护、长时运行或其他变体证据，不解除 RISK-004 / RISK-010 或独立专项复核要求。没有重新构建或宣称新的远端 CI。

完整长日志和含设备选择信息的本机记录仅在忽略的 `.work/upstream-power-ramboot-20261108/`；本归档只包含脱敏摘要与产物身份。
