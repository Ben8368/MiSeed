# 2026-11-17 Linux 接口设计全量审计

## 范围与判定口径

基线为 PR #7 当前 HEAD `210e0b873f8c3a292fb1b71a06ef6f5e22076567`，产品固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`（v6.18.60），目标链路为第二阶段 UEFI → Linux → Debian。静态盘点仓库 `kernel/`、DTS/bindings、救援用户态和活动接口文档，并对照该锁定内核的子系统 ABI、UAPI 与 binding；不是上游合入审查，也不把源码存在或编译结果视为 Debian/设备能力完成。

标签含义：**符合**＝采用 Linux 已定义的子系统/ABI 语义；**本地扩展**＝接口形态依托标准子系统，但驱动实现、属性或 binding 尚未上游合入/评审；**私有内部**＝内核内部协作或诊断协议，不是产品 UAPI；**不符合/待整改**＝语义、生命周期或产品入口与目标标准不符；**未实现**＝设计目标尚无产品实现。

## 接口清单

| 功能 | 设计接口与消费者 | 判定 | 私有/不符合项、状态与证据边界 |
| --- | --- | --- | --- |
| 电池与燃料计 | Linux `power_supply` 属性、uevent；标准 sysfs 消费者/UPower | 符合标准模型；本地驱动 | FG 按上游 power_supply 单位和能力建模；SoC/芯片驱动及 compatibles 是本地支持，未合入上游。驱动/API 存在不代表 Debian 已集成。 |
| **有线** SMB5 固定 PD | `power_supply`（status/health、输入电流限制、charge behaviour 等）、IIO ADC、Type-C/TCPM；Linux 标准消费者及 `tools/wired_power.py` | 标准模型；本地驱动 | 已删除 `charge_test` sysfs/Kconfig、测试窗口控制和诊断写入口。固定档准入/限流策略属于本机驱动策略；安全锁、watchdog 和互锁留在内核内部。按 `docs/archive/reviews/2026-11-16-linux-interface-closure.md`，外部标准限流写入不能绕过有效策略。 |
| **有线** PPS 与 BQ 泵 | `power_supply`、Type-C/TCPM、I2C/regmap、IIO、thermal zone/cooling；标准属性和 thermal 消费者 | 标准模型；若干本地扩展 | SMB5/BQ/PPS 作为单一驱动模块协作，内部 `umi_smb5_claim/check/release` 是私有内核实现接口，不导出给用户态/其他模块；auxiliary 用于设备生命周期，不是私有协议 ABI。TCPM `INPUT_VOLTAGE_LIMIT` 与本地 thermal/binding 实现尚未上游合入，见 TD-004/005/007；子系统评审及剩余错误恢复未闭。 |
| **有线** 温度与限流 | IIO 温度通道；thermal zone/cooling；power_supply 电池温度；thermal 用户态 ABI | 标准框架；本地模型 | thermal 使用毫摄氏度，power_supply 温度使用 0.1°C；实现显式换算/校验。虚拟有线温度模型及控制映射属本地扩展，温标、保护专项和独立复核仍开放。不得将模型节点或 L2 当作硬件保护证据。 |
| **无线 RX** 接收充电 | 目标应独立提供 `power_supply` / thermal，并描述自身供电关系 | 未实现 | 仓库无无线 RX 驱动、控制 ABI 或验证；不得复用/冒用有线 PD、PPS 节点与参数。 |
| **无线 TX** 反向供电 | 目标应独立定义反向供电能力、`power_supply` / thermal 与保护路径 | 未实现 | 仓库无 TX 实现；OTG、有线或 RX 结果不能证明 TX。 |
| NFC RF / NCI | Linux NFC/NCI core；控制面 generic netlink，数据面 AF_NFC；I2C/GPIO/IRQ 与 `nxp,sn100t` binding | 标准 UAPI；本地 transport/binding | 不再采用旧 NQ 字符设备和私有 ioctl/read/write ABI。`kernel/nfc/sn100x.c` 的 SN100T transport 和 binding 未上游合入。现有真机证据覆盖 reset/init、target、close/reopen；NDEF、PM、完整恢复、Debian 消费尚未闭合。 |
| NFC eSE / HCE | NFC 子系统安全元件回调/UAPI 与用户态权限模型需按具体芯片能力评估 | 未实现/待设计 | 驱动未实现 SE 回调；HCE/eSE/支付不得从 NCI、RF target 或 API 可用推导。Android OMAPI/HAL 私有 ABI 不构成 Linux 产品入口。 |
| 显示与背光 | DRM/KMS、MIPI DSI、backlight class、GPIO/clock/genpd；标准 KMS/backlight 消费者 | 标准框架；本地面板驱动/binding | CSOT 1080×2340@90Hz 属本地面板时序与 GPIO50 模式选择扩展。新 HEAD 的 KMS 模式及目视 L3 已记录，vblank/实际节拍、GPIO 重亮、启动条纹、SMMU/供电 PM 及根 DT 交接仍开放（RISK-010/TD-006）。fbdev 仅兼容/诊断，不是产品显示接口。 |
| 触控与输入 | 目标为 Linux input 子系统、evdev；按键也使用 input | 未实现/局部存在 | 仓库尚无完整触控驱动和 Debian 产品消费者；不能用 Android touch HAL 或私有事件接口替代。已接按键代码按 input 归类，需以最终设备节点/消费者验证。 |
| Device Tree / 资源管理 | OF、标准属性、GPIO/regulator/clock/IIO/power-supply phandle；YAML binding + `dt_binding_check`/`dtbs_check` | 框架符合；板级描述为本地扩展 | 已为 FG/SMB5/BQ/PPS、SN100T、CSOT 提供本地 schema；芯片专用 compatible/属性并非上游已接纳。不得改写标准属性语义或冒用近似 compatible。定向 schema 通过不代表整机/运行时 DTBO 全面合规；ABL 根身份/资源交接仍有 TD-006/RISK-010。 |
| 救援 Shell 与硬件诊断 | 静态 initramfs；power_supply/thermal/NFC 标准节点；configfs ACM/TTY；`CHARGE_LOG` 文本采样 | 临时诊断/bring-up 入口 | `charge-stop` 使用标准 power_supply；部分 `thermal-stop/clear` 通过本地 cooling 状态。救援命令、日志文本、`printk`/debugfs 是私有诊断接口，不是稳定产品 ABI，不应成为 Debian 唯一功能入口或保护路径；TCPM debugfs 只作观测。 |
| UEFI、EFI Stub、启动交接 | EFI Stub/EFI 配置表与标准 Linux 启动契约；Debian 标准服务和驱动消费者 | 设计中/未完成 | 产品目标要求 UEFI → Linux → Debian，但本仓库当前不含完整 UEFI 固件、Debian 集成与运行时服务。ABL/Fastboot/header-v2 仅诊断/封装路径，不能替代产品接口验收。 |
| Wi-Fi/蓝牙、传感器等其余设备 | 目标分别核对 cfg80211/mac80211、Bluetooth、IIO/input/thermal 等主线子系统与对应 UAPI/binding | 未实现或范围外 | 当前没有可据以判定的完整产品驱动/消费者；本审计不将原厂 Android HAL/Binder、VINTF、Bionic 私有 ABI 认作 Linux 接口。 |

## 私有设计与不符合项汇总

1. **已移除的用户态私有 ABI**：SMB5 `charge_test` 属性、对应 Kconfig/诊断写入与受控测试状态；NFC NQ 字符设备/ioctl 路线。当前审计 HEAD 未发现它们作为产品 ABI 的设计入口。
2. **仍存在但边界合理的私有实现**：有线策略互锁及 `umi_smb5_*` 内部函数、驱动间状态/锁、救援命令和诊断日志。它们不是 Linux UAPI；内部实现应继续保持不可被 Debian 当作稳定产品接口依赖。
3. **标准形态下的本地扩展**：TCPM `INPUT_VOLTAGE_LIMIT` 实现、本地充电/热策略模型、芯片/面板/NFC compatible 与 YAML binding、90Hz GPIO 模式选择。它们使用标准框架，但未上游合入；接口语义/维护路径还需相应子系统评审。风险债务以 TD-004/005/006/007、RISK-004/010/011 为准。
4. **当前不符合产品完成条件的缺口**：无线 RX/TX、eSE/HCE、触控完整链路、UEFI 产品启动、Debian 服务/权限/应用消费，以及若干硬件 PM/DT 交接尚未实现或验证。它们是未完成能力，不应误写为已验证私有接口。
5. **明确的历史 DT 风险**：11 月 10 日审计发现 PPS `monitored-battery` 曾指向 fuel gauge 而非 simple-battery 的语义错误；后续收尾记录了修正。SMB5 `power-supplies` 拓扑历史上曾待核；本审计以当前 DTS/schema 与 11 月 16 日修正记录为准，历史问题不再作为当前已确认缺陷，但运行时 DTBO/根节点交接仍开放。

## 结论与剩余证据

当前已实现的有线电源、NFC RF/NCI 与显示产品接口总体采用 Linux 主线子系统，而非 Android HAL/Binder 私有 ABI。总体判定为：**接口方向符合 Linux 标准；存在未上游合入的本地扩展；多项产品能力仍未实现/未验证**。不能简化为“全项目已符合 Linux 标准”或“已上游”。

源码/API 形态证据、锁定内核 ABI/binding 对照、上游接纳状态、L2/schema、L3 真机、Debian 集成分别成立。11 月 16 日审计所列 Linux 283 项零跳过、两套 L2/schema 和模块构建结果仍为相应精确 HEAD 的历史证据；11 月 17 日 PPS/90Hz 真机记录只覆盖其中列明的项目，vblank 与上述开放能力不由此关闭。无线、NDEF/PM/eSE、UEFI/Debian、独立专项复核及当前 PR HEAD CI 均未由本静态审计补证。

后续实现按 `docs/ARCHITECTURE.md#linux-接口准则`；验收证据与门槛按 `docs/QUALITY.md#linux-接口验收`；例外退出与责任按 `docs/TECH_DEBT.md`、`docs/RISK_REGISTER.md`。详细实现/真机数据沿用[11 月 16 日接口收尾审计](2026-11-16-linux-interface-closure.md)、[NFC 设计](../../NFC.md)、[90Hz 移植](../research/2026-11-16-display-90hz-port.md)及[11 月 17 日真机记录](../hardware/2026-11-17-umi-pps-90hz-l3.md)。
