# 2026-11-10 Umi 电池 FG / SMB5 标准 ABI 真机只读验证

## 输入与边界

| 项 | 结果 |
| --- | --- |
| 日期 / 主机 | 2026-11-10；Windows 10.0.26200 x64 / PowerShell 7.6.9 |
| 授权 | 维护者要求验证电量、电压、电流、温度和标准 ABI；只读，无充电/PPS 控制、刷写或存储操作 |
| 设备 | Umi / `umi`；序列号未留档 |
| 源码与载荷 | 源码 `b53705e17e1b9db15a66f0e972c6a3978cb5c1de`；之后 `ed941ea` 仅提交文档。Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，`boot.img` SHA-256 `9f4713dbcb3b502d3f1db8a06d9ee61b6f6154935be8bfff2162baed69e32014` |
| 路径 | 有线 USB host/SDP 接入；只读核对电池 FG 与 PM8150B SMB5。未测无线 RX/TX，未确认 USB-PD 合约 |

## 操作与读数

启动前 ADB 基线为 `sys.boot_completed=1`、Android kernel `4.19.325-cip131-st15-perf-g71b13e62f057`。`adb reboot bootloader` 后确认单台 Fastboot `product=umi`、已解锁、非 userspace；`fastboot boot <boot.img>` 返回 0，Sending/Booting 均 `OKAY`。未刷写。

USB CDC `COM6` 上执行只读救援命令 `charge`。该消费者以 `O_RDONLY` 读取 `/sys/class/power_supply/*` 的标准 sysfs 属性；导出 `CHARGE_LOG_BEGIN first=107 next=139 count=32 storage_errors=0` 至 `CHARGE_LOG_END next=139`，完整读取 4 组 battery 与 SMB5 样本。它是最小 Linux rescue 消费者，不代表 Debian 已集成。

| 标准 power_supply 节点 | 4 组样本的观测范围 |
| --- | --- |
| `qcom-battery`（FG） | `present=1`、`capacity=95%`、`temp=280` deci°C（28.0°C）、`voltage_now=4,389,148–4,390,369 µV`、`current_now=+598,144–615,234 µA`、`status=Charging` |
| `pm8150b-charger`（有线 SMB5） | `online=1`、`voltage_now=4,605,872–4,619,376 µV`、`current_now=1,084,125–1,085,745 µA`、`input_current_limit=3,000,000 µA`、`health=Good`；USB type 输出标记 `[SDP]` |

FG 电量、电压、电流和温度属性均可经标准 ABI 读取；SMB5 有线输入电压/电流及限值亦可读，单位符合 power_supply 的 µV/µA 与 deci°C 约定。FG 报充电且电流为正，与驱动将充电映射为标准正电流一致。未用外部仪表核对模拟读数精度。

**修复前差异：** 同 4 组样本中 `qcom-battery/status=Charging`、电池正电流，但 `pm8150b-charger/status=Not charging`；SMB5 同时报告 `online=1` 和约 1.08 A 输入电流。该状态不一致阻止宣称 status 语义交叉一致，需只读追查 SMB5 状态寄存器/映射；本轮未更改策略或触发测试。

验证后串口请求 bootloader、Fastboot `reboot`，ADB 恢复并确认 `sys.boot_completed=1`、原 Android kernel `4.19.325-cip131-st15-perf-g71b13e62f057`。未测试有线充电控制、PD/PPS 协商、温控保护、传感器精度、无线 RX/TX 或 Debian 消费者。

## 有线 SMB5 status 修复与复测

维护者本轮要求“看下对话……修复完善”，延续对话末尾“查清楚后修复完善，然后再上机测试”的范围。仅修正有线 SMB5 标准状态解码/读取错误处理；无线 RX/TX 未涉及；不调整阈值、保护配置或充电控制。源码基于 `ed941eaab307149bcfe713ca90c80af4a1683eb8` 工作树，未提交。`qcom_pm8150b_hw.c` SHA-256 `0df1d2db7a12929a1762f69ab902c7784b022091201b060b847474d329247ea2`；其他内核输入沿用上述 L2，来源锁已同步。

根因：继承实现使用 `STATUS_2` bit 5（本地宏 `VBATT_GTET_INHIBIT_BIT`）覆盖硬件充电阶段。旧日志 `STATUS_1=0x03` 是 PM8150B fullon，`STATUS_2=0x28` bit 5 为 1，却没有 bit 1 电池过压错误，造成持续误报。锁定主线 `f45ed76d…` 的 `drivers/power/supply/qcom_smbx.c:smb_get_prop_status()` 使用真正的 BAT_OV 错误覆盖阶段；但其 PMI8998 位布局和阶段枚举不能原样套用 PM8150B。设备匹配 [LOS `71b13e62f057a649b77fe4062feb73ee72ad609c` smb5-reg.h](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/qcom/smb5-reg.h)（SHA-256 `a3244822ce0d977369ca1dcf3d0d649559230932d83c0bdd37d91a8dcfd33c87`）确认 BAT_OV 为 bit 1、fullon 为 3。修复使用既有 `CHARGER_ERROR_STATUS_BAT_OV_BIT`；INHIBIT/PAUSE/DISABLE 仍为 Not charging，TERMINATE 仍为 Full。USB 在线读取失败直接返回错误，保留调用者输出。

FG 的可选 charger 引用在本板 DT 中未配置，标准 STATUS 由容量/实测电流推导；没有缓存错误 SMB5 状态。本次不引入反向依赖，也不强迫两路瞬时状态总相等。功能 → power_supply 驱动模型 → 只读 `POWER_SUPPLY_PROP_STATUS` / sysfs → Linux rescue 与后续 Debian 标准消费者；未添加私有 ABI、DT 属性或 Android 依赖，旧 TD-004/005 不扩大。

复测载荷 `.work/smb5-status-l2/boot.img` SHA-256 `2a5f03ec62e5278666d2a8b7867c1db8dd8705e6851ad9329e31e50aca1cb084`（9,617,408 bytes）。Windows x64 / PowerShell 7.6.9，WSL Ubuntu 24.04 x86_64、GCC 13.3.0 / make 4.3；设备仍为单台 Umi/CSOT，原 Android/底层固件认定沿用前表，未读取分区。

实际顺序：ADB 只读核对 `ro.product.device=umi`、`sys.boot_completed=1` 和原内核 → `adb -s <设备> reboot bootloader` → Fastboot `getvar product/unlocked/is-userspace` 全部符合 → `fastboot -s <设备> boot <上述镜像>`（发送/启动 OKAY，退出 0）→ COM6 115200 发送 `version`、`charge-log` → `bootloader` → Fastboot `getvar product` 后 `reboot`（退出 0）→ ADB 核对恢复。回退为同一返回路径；异常时电源键＋音量下进入 Fastboot。未发 charge-test/charge-stop/PPS 控制，未刷写、挂载存储或改分区。

完整导出 `first=0 next=96 count=96 storage_errors=0` 至 END 96，无缺段。启动约 1.58 秒的首组 SMB5 为 Not charging，对应日志 `hw_phase=disabled chg_enable=0 chgr1=0x47`；FG 此时为 Charging/+33,691 µA，属于两个不同硬件消费者的切换期观测，不能抹去。约 2.9–21.7 秒的后续 10 组，两路均为 Charging；持续 `hw_phase=fullon chg_enable=1 chgr1=0x03 chgr2=0x28`，旧误判已消失。

| 标准属性（后续 10 组） | 观测范围 |
| --- | --- |
| FG | SOC 95→96%；temp 280 deci°C；voltage_now 4,381,824–4,398,426 µV；current_now +560,058–691,894 µA；status=Charging |
| 有线 SMB5 | online=1；voltage_now 4,559,168–4,638,048 µV；current_now 1,035,790–1,137,000 µA；health=Good；status=Charging；SDP，input_current_limit 3,000,000 µA |

恢复后 `sys.boot_completed=1`、原 Android 内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。L3 闭合的是 USB host/SDP 下 fullon/bit5 误报修复及启动 disabled→charging 的观测；未做真实过压/温控故障注入、终止/满电/拔线场景、墙充 PD/PPS、无线或 Debian 验收。过压位/阶段/I/O 失败与恢复由实际生产 C 解码器离线回放覆盖（4096 组组合及旧样本），不冒充硬件故障验证。

原始脱敏前日志/镜像仅留 `.work/smb5-status-l2/`，不入 Git。构建时钟警告及 DT 专用环境的后续修正见[环境复核](../reviews/2026-11-10-kernel-validation-environment.md)。
