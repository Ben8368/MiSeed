# 2026-11-08 小米 10 电池与充电硬件只读调查

## 结论摘要

本次在维护者指定的 Mi 10（umi）上对 LineageOS Android 系统执行临时 `adb root`，仅读取 sysfs、设备树、属性、Android 电池服务和内核日志；结束时执行 `adb unroot` 并确认恢复 `uid=2000(shell)`。未重启、刷写、读写分区、调节充电或修改设备状态。

设备实际报告电池 profile `j2nvtbm4n_4780mah`、锂聚合物、设计容量 4780 mAh，学习满充容量 4758 mAh，循环计数 567。BMS 同时报 `soh=52`，与满充容量接近设计值的读数不一致；该厂商专用字段定义未经核实，不能据此判断健康度。运行时设备树还含 `j2gybm4n_4780mah` 另一候选 profile，启动日志曾分别打印两个 profile 名称；需要确认机内电池和最终 profile 匹配后，才可把容量/健康度用于充电决策。

读取时 USB PD 显示有输入电压，但电池状态为 `Discharging`、USB/PC 端 `online=0`、输入暂停为 1，BQ2597x 充电泵报告禁用。因此这次电脑 USB 连接期间没有证据表明电池正在充电。PD 电流上限、充电器设定电流和电池实测电流是不同字段，不应相互替代。

## 环境、授权和范围

| 项 | 记录 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08，Codex 执行只读采集 |
| 宿主 | Darwin 25.6.0 / arm64 / zsh |
| 设备代号 | A；小米 Mi 10，Android `umi`，SoC `SM8250` / 板级平台 `kona`。未记录序列号、IMEI、ROM ID、MAC 或电池认证页数据 |
| 系统 | Android 16 / API 36 / userdebug；Linux `4.19.325-cip131-st15-perf-g71b13e62f057`，aarch64 |
| 授权 | 用户明确授权本次 ADB 和临时 root，只读尽可能完整的充电硬件信息 |
| 操作边界 | 不 reboot、不写 sysfs/设备树/PMIC、不访问 NV/分区、不控制供电、不复制认证芯片标识 |
| root 恢复 | `adb unroot` 后 `adb shell id` 为 `uid=2000(shell)`；设备保持 Android 运行 |
| 仓库基点 | PR #2 分支采集时 HEAD `7b4677b6198955a69e59d5d8a13ba5aa0cf5fd4a`；本报告不依赖 PR2 日志镜像 |

## 电池组与燃料计读数

标准 power_supply 字段遵循 Linux 单位：电压 µV、电流 µA、电荷 µAh、温度 0.1°C；`_NOW` 为瞬时值，`_AVG` 为硬件平均值。采集跨数十秒，状态和瞬时数会变化。厂商扩展字段单独标注，不推定其单位或算法。参考 [Linux power_supply class](https://docs.kernel.org/power/power_supply_class.html)。

| 字段 | 观测值 | 说明 |
| --- | ---: | --- |
| `battery/present` / `health` | `1` / `Good` | 内核 power_supply 状态 |
| `battery/technology` | `Li-poly` | 驱动报告化学类型；没有取得可验证的电芯制造商/完整物料号 |
| `bms/battery_type` | `j2nvtbm4n_4780mah` | 当前 BMS 导出的 profile 名 |
| `qcom,battery-type` / 活动候选节点 | `j2nvtbm4n_4780mah` / `qcom,xiaomi_j2nvtbm4n_4780mah_averaged_masterslave_sept12th2019` | 运行时设备树中的匹配数据 |
| 另一设备树候选 | `qcom,xiaomi_j2gybm4n_4780mah_averaged_masterslave_aug26th2019` | 同容量、不同 profile；内核启动日志也出现该名称，需核对实际电池变体 |
| `charge_full_design` | 4,780,000 µAh = **4,780 mAh** | profile 设计容量，不是拆机实测容量 |
| `charge_full` | 4,758,000 µAh = **4,758 mAh** | gauge 当前学习/满充阈值；约为设计值 99.54%，不能单独视为 SOH |
| `bms/constant_charge_voltage` | 4,389,899 µV ≈ **4.390 V** | 燃料计导出的编程电压字段；不是采集期间的实时电池电压 |
| `capacity` / `capacity_raw` / `cc_soc` | 83% / 8323 / 8340 | 后两者为厂商扩展原始量纲，未解码 |
| `charge_counter` | 约 3,954,000 µAh（读数随采集变化） | 标准电量计数值；Android 同期服务也报告约 3,959,671 µAh |
| `cycle_count` | **567** | battery 与 BMS 两处一致 |
| `bms/soh` | **52** | 厂商专用字段；与 `charge_full/design` 比值明显不一致，语义/有效性未核实 |
| `authentic` / `chip_ok` | `1` / `1` | 电量计报告认证/芯片状态通过；不保存 ROM ID 或认证挑战数据 |
| `voltage_now` | 约 4.15–4.21 V | 多次瞬时读数范围 |
| `voltage_ocv` / `voltage_avg` | 约 4.180 V / 4.194 V | BMS 报值；不是电芯厂家规格 |
| `current_now` | 约 +0.05 至 +0.39 A | 多次瞬时读数；采集时变化明显 |
| `current_avg` | 约 −0.26 至 −0.14 A | 同一采集期的厂商 BMS 平均值；与瞬时电流符号/幅值不一致，方向约定未做厂商源码核验 |
| `temp` | 约 33.2–34.5 °C | battery/BMS 与 Android 服务读数在该区间；不是环境温度 |
| `charger_temp` / `usb/connector_temp` | 约 33.3 °C / 33.7 °C | 同期瞬时值，来自充电器/USB 连接器传感器 |
| `charge_now` / `charge_now_raw` | `0` / 约 3,968,000 µAh | 字段彼此不一致；不能把 `charge_now=0` 当作空电池 |
| `resistance` / `resistance_id` | `110595` / `100300` | 厂商扩展字段，单位/算法未核实，不作内阻或电芯识别结论 |

`battery/charge_full` 接近设计容量，但 BMS `soh=52`、`charge_now=0`、`charge_now_raw≈3.97 Ah` 及平均/瞬时电流的关系都显示出数据质量或厂商语义需要进一步核实。该快照不足以判定电池老化程度，也不能替代容量标定或电芯鉴定。

## 充电输入、限值和 PMIC

| 项 | 观测值 | 解释边界 |
| --- | ---: | --- |
| SoC | `QTI SM8250`，板级 `kona` | 系统属性和 `/proc/cpuinfo` |
| 充电/燃料计 PMIC | `qcom,pm8150b@2`，启动日志 `PM8150B v2.0`，集成 QPNP SMB5 与 QPNP FG Gen4 | 多个 SPMI SID 可见；具体芯片封装/板级线路未拆机确认 |
| 其他 SPMI 节点 | PM8150 SID0/1；PM8150B SID2/3；PM8150L SID4/5；PMXPrairie SID8/9；PM8009 SID A/B | `OF_NAME` 与驱动枚举，不等于对板级功能的完整鉴定 |
| 电池 profile 充电参数 | `nom-batt-capacity=4780 mAh`；`max-voltage=4.48 V`；`fastchg-current=6000 mA`；CC/CV threshold `4390 mV`；FFC threshold `4460 mV` | 来自运行时设备树属性名/值，属于配置数据，不是本次实际充电电压/电流；不作为调参建议 |
| 温控 | `sw_jeita_enabled=1`；设备树有 JEITA FCC/FV 范围与 cool/warm/hot 阈值 | vendor 范围表及阈值单位未独立核实；未据此导出安全温区 |
| 电池侧充电器字段 | `constant_charge_current=4.8 A`，`constant_charge_current_max=6.0 A`，`voltage_max=4.48 V` | charger 对象的编程值/上限，不代表当时流入电池的实测值 |
| USB/PD | `present=1`、`real_type=USB_PD`、`pd_active=1`；VBUS 约 4.75 V；PD 上限 5.0 V / 3.0 A | 上限/输入源状态不是实际电池充电量；`input_current_now` 单次约 0.206 A |
| 当时在线状态 | `usb/online=0`、`pc_port/online=0`、`battery/status=Discharging`、`battery/input_suspend=1`、`battery_charging_enabled=1` | 电缆/PD present 与充电状态分离；该快照未显示正在对电池充电 |
| BQ2597x | `/sys/.../i2c-4/4-0066`；`present=0`、`charging_enabled=0`、`ti_vbus_present=1`、总线约 5.15 V | 该充电泵在此快照未启用；不据此推定硬件损坏 |
| 无线充电 | `dc/present=0`；`rx1619/present=0`，`wireless/power_good=0` | 此次未接收无线功率；仅是采样状态 |
| 电池认证器 | `ds28e16`，`authen_result=1` | 仅记录成功状态，认证 ROM ID 与页数据脱敏丢弃 |

配置中报告 4.8 A/6 A 或 PD 3 A 不能说明电池实际得到该电流；采集时输入暂停、系统状态为放电。系统连接电脑的 USB 供电形态与专用充电器协议并未作对照测试。

## 采集方式与隐私

主机使用本机 ADB platform-tools；设备已在线 Android，未执行重启：

```sh
adb devices -l
adb root
adb wait-for-device
adb shell id
adb shell 'for k in ro.product.model ro.product.device ro.board.platform ro.soc.model ro.build.version.release ro.build.version.sdk ro.build.type; do getprop "$k"; done'
adb shell dumpsys battery
adb shell 'for d in /sys/class/power_supply/*; do for f in "$d"/*; do ... read selected attributes only; done; done'
adb shell 'find /sys/firmware/devicetree/base -maxdepth 6 ...'
adb shell 'for d in /sys/bus/spmi/devices/*; do ... read uevent; done'
adb shell 'dmesg | grep -iE "battery|bms|fuel.?gauge|charger|smb5|pm8150|pmic|spmi|qg"'
adb unroot
adb wait-for-device
adb shell id
```

执行时具体读取了过滤后的标准属性、相关设备树电池/SMB5/FG 节点、SPMI 枚举、`dumpsys battery` 和电源相关内核日志，没有导出 `getprop` 全量结果、设备序列号、IMEI、Wi-Fi/蓝牙地址、DS28E16 ROM ID/原始页、分区或 NV。临时 raw 输出仅在本机忽略的 `.work` 下处理，不作为仓库证据或提交内容。

## 结论与后续验证缺口

- 当前 Android 内核确有电池/BMS、PM8150B SMB5/FG、USB-PD 与额外充电泵驱动，可提供多类实时 telemetry；这不意味着 PR2 的独立 Linux RAM 日志镜像也含这些 Android 驱动。
- 当前能够确认的软件 profile、设计容量、循环计数、温度、电压和部分充电器状态；不能从软件读数确认真实电芯厂商/单体拓扑、实际可用容量、准确 SOH、保护阈值安全性或硬件净充入电流。
- 两个 4780 mAh profile 同时存在、启动日志命中过不同 profile，且 SOH 与 FCC 读数冲突。解决前不得以单次 `charge_full` 推断电池健康，也不应据此修改限流/限压。
- 未做充电器类型对照、长时间充电/温升、从低 SOC 到满电容量测试、插拔边沿测量、热保护实测、PMIC 原始寄存器读取或拆机电芯确认。
