# Umi LineageOS 无线 RX 基线

2026-11-19，MiSeed 基线 `774c0a4`，Windows x64 / PowerShell 7.6.9，ADB 37.0.1。设备为 `umi`，LineageOS `23.2-20261115-NIGHTLY-umi`，内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。本次范围为无线接收 RX 调研，USB 仅用于离座基线采集；无线反充 TX 未测。

## 已取得的真机证据

| 项 | 观测 |
| --- | --- |
| I²C | LOS `888000.i2c`，`5-0061` 绑定 `rx1619`，`5-0067` 绑定 `rx1619_trim`，P9415 候选 `5-003b` 枚举但未绑定 |
| 活动 DT | `/soc/i2c@888000/rx1619@61`，回读 `compatible`、`reg`、IRQ/enable/TX/power-good GPIO cells 与 `mi,wireless-by-usbin`，与锁定源码对应属性一致 |
| USB 基线三次读数 | `usb/present=1`，`usb/online=0`，Type-C 为 `Source attached (default current)` |
| 无线基线三次读数 | `rx1619/present=0`，`wireless/wireless_power_good_en=0`，`dc/present=0`，`dc/online=0`，`wireless/wireless_cp_en=0` |
| 电池 | `Discharging`，75%，`temp=272`（27.2°C） |
| 权限恢复 | 普通 shell 可识别绑定，但电源属性读取受限。使用本轮获准的临时 adbd root 采样，结束核回 UID 2000 |

采集器 [collect_wireless_rx.py](../../../tools/collect_wireless_rx.py) 按已核 LOS 内核版本及字段白名单读取，设备侧无充电配置、分区、GPIO 或寄存器写入，也不保存设备标识。USB 基线主机留档 `.work/umi-wireless-rx-20261119/usb-baseline.json`，SHA-256 `09d8fad5a8cba2042c33672726e8d15e7d6f5ea61a602cacfb2abbc965f7a7f3`，作为上座前对照。同目录 `active-rx-dt.json` 为活动 DT 只读补采，SHA-256 `93495fffef81585c37c9f0d350200d8d6377acc14fcad4da3b7429f388aa31c9`，补采后同样核回 UID 2000。

## 拓扑与接口核对

- 锁定 LOS [Umi DT](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/arch/arm64/boot/dts/vendor/qcom/umi-sm8250.dtsi#L97-L141) 的 RX1619 和 P9415 共用 TLMM GPIO157 IRQ、GPIO155 enable、GPIO154 TX 和 PM8150B GPIO7 power-good，两候选均声明 `mi,wireless-by-usbin`。另见 [SMB5 板级路径](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/arch/arm64/boot/dts/vendor/qcom/umi-sm8250.dtsi#L360-L396) 的 NCP3902、DC Q1、RX sleep 与无线电荷泵控制。
- [锁定 SMB5](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/qcom/smb5-lib.c#L5122-L5198) 的 `dc/present` 在 `mi,bq-wireless` 路径读 DCIN PON，`dc/online` 可以被软件延时保持；`usb/online` 也受软件分支影响。[USB present](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/qcom/smb5-lib.c#L5479-L5493) 读 USBIN 实时插入状态。[USB input_current_now](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/qcom/smb5-lib.c#L6214-L6265) 来自 USBIN 电流通道。上座时结合 Type-C 无连接、RX power-good、两路 present 及 USBIN 电流判断；仅凭 `dc/online` 不判定电流路径。
- 锁定 Linux `v6.18.60`（`f45ed76d6791cf72ff1265ee93511175bbbc2f18`）已提供 `i2c16`（`0x888000`，GPIO48/49）、PM8150B GPIO 控制器及 `POWER_SUPPLY_TYPE_WIRELESS`；在锁定 `drivers/power/supply` 和对应 binding 中未找到 RX1619 实现。MiSeed 当前板级将所涉 TLMM 引脚保留。初步接口为 GENI I²C、regmap、GPIO/IRQ 与 `power_supply`，用标准 sysfs/uevent 验证只读状态，按已核物理量选择遥测属性。LOS 接口仅用于参考采集。

## 无线对照与验收

Windows 已通过已有配对直连 Android 无线 ADB，并用启动 ID 确认和 USB 为同一次 Android 启动。临时 adbd root 重启会换 TLS 端口，接机窗口内通过 USB 确认新端口、读取器和恢复通路。两次窗口均在等待未插线的离座条件时超时，未取得无线样本；每次均通过 USB 核回 UID 2000。第二次窗口中抽查仍见 USB 已连接，Type-C 报 `Source attached (default current)`。

采集器只为 USB 提供 `--temporary-adb-root`。无线采样如需 root，先保留 USB 完成权限切换，再连上已配对的无线 ADB，采完接回 USB 恢复并核对 UID 2000。

维护者反馈接回 USB 后，复核启动 ID 与采样窗口一致。用获准的临时 adbd root 补采筛选后的内核日志，随后核回 UID 2000。`9341.405695` 至 `9361.887856` 秒间，RX1619 的 `Vout` 为 17.050–17.138 V，`Vrect` 为 17.080–17.294 V，`Iout` 为 107 mA。`9363.363335` 秒记录无线离座、power-good 拉低，后续三组 `Vrect` 和 `Iout` 均归零。`9444.641874` 秒起见后续 `USB_STATE=CONNECTED`。这些是 LineageOS 接收端遥测和离座边沿。补采未取得上座时成对的 PMIC USBIN/DCIN 状态、Type-C 状态、电池侧功率或协议档位。主机留档 `.work/umi-wireless-rx-20261119/usb-return-rx-log.json`，SHA-256 `9ce0ceb3caf1f8033bfe77c13d0caf5bf1981b4229e28c0fc077e91b445f3d9b`。

### 上座同步采样

17:24:56–17:25:01（上海时间），同一 LOS 启动且手机物理拔除 USB‑C，使用维护者的无线底座。通过配对的无线 ADB 采到三组状态，手机取下接回 USB 后核回 UID 2000。

| 字段 | 三组读数 |
| --- | --- |
| Type-C / 接收器 | `Nothing attached`；`rx1619/present=1`，`wireless_power_good_en=1` |
| PMIC 输入 | `usb/present=1`，`dc/present=1`；两者 `online=0` |
| USBIN 电流通道 | `usb/input_current_now` 分别为 1,341,695、555,035、1,356,290 µA |
| RX 遥测 | `input_voltage_regulation` 为 5.317、5.537、7.602 V；`Vrect` 为 10.312、11.171、16.757 V；`Iout` 均为 87 mA |
| 电池与无线泵 | 电池 71%、28.0°C，三次 `Discharging`；`wireless_cp_en=0` |

USB 外口未连接而 USBIN presence 和电流通道均响应，支持本机 LOS 在本次无线 RX 中使用 PM8150B USBIN。`dc/present=1` 在此 LOS 配置下来自 DCIN PON 位。两路 `online` 受驱动策略影响。本次未取得 PMIC `POWER_PATH_STATUS` 直选位或 DCIN 电流，路径排他性及电池净充入仍待独立验证。三个读数跨数秒，RX ADC 与 USBIN ADC 分次读取，无外部量测校准。主机留档 `.work/umi-wireless-rx-20261119/wifi-session-on-pad.json`，SHA-256 `c2813b2dc917030f7d5c2bd1d9a7cefb8fefaf8f5d1739b33b93cdfd97cc869b`。

MiSeed 侧主动 RX 前另验输入切换、限流、温控与异常停机。采集器离线验证为仓库 L0 通过，Windows unittest 316 项、125 跳过；内嵌采集脚本经 WSL ShellCheck error 级通过。上座专项的正例和有线超时反例通过。
