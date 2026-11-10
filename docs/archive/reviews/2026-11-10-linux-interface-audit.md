# 2026-11-10 Linux / Debian 接口审计

## 范围与结论

对应 RQ-14。审计工作树基于 `codex/nfc-wireless-charging` / HEAD `42f54a2d7a338a6e329ec432c5d23c3d085effd2`，含本轮未提交修改。Windows X64 / PowerShell 7.6.9，构建与 C 回归在 WSL Ubuntu 24.04 / Linux x86_64 / bash 执行。未提交、推送或合并。

核查仓库 kernel/、相关移植/构建/打包/采集工具、config/DTS、运行契约和用户空间计划；对照固定 Linux 6.18.60 revision `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的原始源码、ABI 文档和 binding。通过本地 bare Git 的 `git show <revision>:<path>` 检查，未以移动分支或“最新版”代替项目版本。在线文档抓取未提供可引用结果，以固定源码为事实依据。

**结论：底座使用 Linux，但不能认定内核适配整体完成。** 最明显的目标偏差是 NFC 保留 Android 私有字符设备/ioctl，并曾计划引入 Android 用户空间。其次是有线 BQ25970 缺少标准供电接口、私有诊断/协调、温控框架缺口和设备树语义/binding 问题。已有 FG/SMB5 的 power_supply、PD/PPS 的 TCPM、USB configfs 等不应整体推倒重做。

本轮完成接口层面静态排查与规则整改；不是全驱动正确性证明、完整 DT schema 检查或新真机验收。下表“标准”只表示已使用对应接口，不表示全部单位、错误处理、事件、休眠或硬件功能都已验收。未实现功能单列，不能当作审计通过。

## 功能与接口矩阵

| 功能 / 路径 | 当前接口与定位 | 判定 / 优先级 | 后续与验收 |
| --- | --- | --- | --- |
| NFC transport | [nq-nci.c](../../../kernel/nfc/nq-nci.c) 的 cdev、私有 ioctl；[nq-nci.h](../../../kernel/nfc/nq-nci.h)、[nfcinfo UAPI](../../../kernel/nfc/nfcinfo-uapi.h)；无 nci_register_device/nfc_register_device | 确认偏离 Linux 产品目标，P1 / TD-003 | SN100T 协议/共享 VEN 评估，接入 NFC/NCI；标准 netlink/AF_NFC 发现、读卡与恢复；旧 ABI 退出 |
| NFC DTS / 配置 | [umi-csot.dts](../../../kernel/umi-csot.dts) 的 `qcom,nq-nci`、`qcom,nq-*`；[config](../../../kernel/umi-l2.config) 仅本地 MISEED_NFC_NQ 路线 | 下游绑定、未启用标准 NCI 路线，P1 / TD-003/007 | gpiod/IRQ/PM 等资源管理与 YAML；不能只加 CONFIG_NFC 或替换 compatible |
| eSE / NFC 服务 | MiSeed 无实现；Android 对照依赖 AIDL HAL、NXP 库、QTEE/eSE | 功能未知 / RISK-011；旧 Android 集成计划已撤换 | Linux 下独立评估固件、协议/权限、共享 VEN；HCE、OMAPI/eSE、支付分别证明，不由读卡推导 |
| 电池 FG（有线/无线可共享的电池观测） | [qcom_fg.c](../../../kernel/power/qcom_fg.c)：power_supply 注册、属性与 changed 通知；PMIC regmap | 标准子系统已采用；本地驱动未出现在固定上游，非“已上游” | 复核电流符号、温度标度、无效/未就绪状态与通知；电池观测不能证明无线 RX/TX 可用 |
| **有线** SMB5 / PM8150B | [qcom_pm8150b_charger.c](../../../kernel/power/qcom_pm8150b_charger.c)：power_supply；另有 `charge_test` 和泵所有权接口 | 标准主体 + 私有诊断/内部耦合，P1 / TD-004 | 保留标准属性，限制诊断用途；协同控制/失败退出审查与有线回归，不冒用别的 PMIC compatible |
| **有线** Type-C / PD / PPS 协商 | [umi_pps.c](../../../kernel/power/umi_pps.c) 经 TCPM power_supply 请求；[PD PHY 补丁](../../../kernel/power/pmic_pdphy_receive.inc) 为内部实现 | TCPM 属于标准路径；ONLINE=2 与 PPS 电压/电流 setter 在固定上游存在 | 保留 Type-C/TCPM 所有权；核对协商、断线、错误/回退；本地 PHY 修复不等于整个路径采用 Android ABI |
| **有线** BQ25970 泵 | umi_pps.c 未注册独立 power_supply；`fastcharge_test`、printk 提供状态/停机 | 产品接口缺口，P1 / TD-004 | 建立适用 power_supply 状态、通知及受保护控制；先审查新接口如何协同 SMB5，保留现有安全退出 |
| **有线** 温控 | umi_pps.c / [umi_pps_policy.h](../../../kernel/power/umi_pps_policy.h)：IIO 温度 + 私有虚拟温度/FCC 限流 | IIO 标准，thermal zone/cooling 策略未接入，P1 / TD-005 | 验证板级标度/model，按适用 thermal 接口集成，独立保留传感器失效和硬件保护；不因接入框架删除保护 |
| **无线接收 RX** | 本仓库未移植 RX 驱动、供电模型与控制服务；Android 基线见 RX1619 | 未实现，不能判定符合/不符合或继承有线验收 | 核实实际器件/协议、power_supply 与 thermal，再单独验证 RX 与 NFC 共存 |
| **无线反充 TX** | 本仓库无 TX 控制/保护实现 | 未实现，不能由 RX 或 USB OTG 推导 | 单独核实能力、保护、标准接口和 NFC 共存，禁止借用有线使能流程 |
| PMIC 观测 / 日志 | [power_observer.c](../../../kernel/power_observer.c) 只读 regmap + printk；[charge_log.h](../../../kernel/charge_log.h) / [采集器](../../../tools/collect_charge_log.py) 为 RAM 诊断协议；TCPM debugfs 日志扩容 | 诊断扩展可保留，不能成为产品 ABI，P2 / TD-006 | 产品读取 power_supply/事件；禁止依赖 printk/debugfs 文本实现唯一状态或保护通道，诊断独立验收 |
| DTS / ABL overlay | [umi-pm8150b.dtsi](../../../kernel/power/umi-pm8150b.dtsi)、[umi-pps.dtsi](../../../kernel/power/umi-pps.dtsi)、ABL 符号隔离；本地扩展缺 YAML 与 schema 证据 | 确认属性语义错误 + 待核拓扑，P1 / TD-007；运行时另有 RISK-010 | 同时修改驱动消费者与 DTS，补 binding、定向 schema 检查及运行时 DT/受影响外设 L3 |
| GPIO / 电源 / 时钟 / I²C / 重启 | 主线 TLMM、GENI、SPMI、regulator、RPMh/genpd、PON restart；有 GPIO 保留范围与 ignore_unused | 标准框架 + bring-up 例外，TD-002/006 | 逐消费者恢复板级资源，验证 PM/关停恢复与 ABL 交接；不能把仍保电的资源算作完整电源管理 |
| USB 诊断 / 登录入口 | DWC3 + configfs ACM / ttyGS0，[rescue_init.c](../../../kernel/rescue_init.c) 诊断协议 | Linux 标准 USB/TTY，命令集是项目诊断 | 标准 Linux/Debian 登录服务后续单独集成；现有 shell/协议不需改成 Android ADB |
| 显示 | simple-framebuffer / FB_SIMPLE / fbcon，继承 ABL framebuffer | 现有 Linux 接口，不是 Android 私有 API；完整 DRM/DSI 未实现，TD-006 | DRM/KMS 与面板资源、刷新/休眠独立验收；已有横屏控制台 L3 保留 |
| 触控、GPU、Wi-Fi 等 | 当前仓库无完整对应实现/本机验收；触控变体仍待核 | 功能缺口，不列成“已迁移” | 分别采用 input/evdev、DRM、cfg80211 等适用子系统，核实硬件再移植 |
| 启动、打包、用户空间 | [pack_android_boot_v2.py](../../../tools/pack_android_boot_v2.py)、Fastboot RAM boot；静态 C initramfs；UEFI/BootShim、完整 Recovery、Debian 构建尚未交付 | ABL 启动兼容封装合法；救援 init 是 Linux 用户空间，非 Debian 完成 | 保留启动契约，产品 UEFI/EFI Stub 与 Debian 目标按 WP3–WP5；不引入 Android HAL 作为硬件功能前提 |
| 宿主离线工具 | 固定源码/哈希检查、设备采集、镜像封装、Windows 串口导出 | 构建/维护接口，非手机内核产品 ABI | Android ADB 只作对照采集，Fastboot 作装载/回退；未实施文件系统/分区安装或产品存储管理 |

## 关键依据与避免误判

### NFC：必须改变内核接口，不能仅补 Debian

`nq-nci.c` 的 `nfc_open()` 在本轮源文件约 756 行、cdev 注册约 1758 行；`umi-csot.dts` NFC 节点约 202 行。保留 `/dev/nq-nci` 及 downstream ioctl 可解释既有 probe/node 成果，不能使 Linux NFC core 自动发现控制器。

固定上游 `Documentation/networking/nfc.rst` 定义 generic netlink 控制及 AF_NFC 数据接口；`drivers/nfc/nxp-nci/` 和 `Documentation/devicetree/bindings/net/nfc/nxp,nci.yaml` 提供候选主线路径。该 binding 的显式芯片列表包含 NQ310/PN547/PN553，没有 SN100T；generic compatible 也不能替代硬件兼容性证明。先评估 NCI 帧/版本、固件、IRQ 和共享 VEN，再选择扩展已有驱动或实现新的 NCI transport。

当前诊断的 NFC open 禁止是针对 NQ 驱动副作用的设计；不把它泛化成所有 Linux NFC API 禁止打开。新 `nfc-status` 只读元数据，扫描上限 256 项，兼容 NUL 字符串列表，校验 DT 属性长度，报告缺失/截断/未绑定与非字符节点，不向控制器发送命令。

### 有线：标准 TCPM 保留，泵和策略分层迁移

固定上游 `Documentation/ABI/testing/sysfs-class-power` 642–658 行明确 ONLINE=0/1/2 对应离线、固定供电、可编程供电；`drivers/usb/typec/tcpm/tcpm.c` 7748–7783 行存在 ONLINE、VOLTAGE_NOW、CURRENT_NOW 的 PPS setter。不能据其可写就定性为 Android 私有接口。

FG/SMB5 注册 power_supply，而 `umi_pps.c` 789 行的 `fastcharge_test`、SMB5 1019 行的 `charge_test` 是本地 sysfs 扩展；`umi_smb5_claim/check/release` 是内部内核模块间接口，不是用户空间 UAPI。问题是产品标准状态/控制覆盖与耦合的可维护性，不是所有内部函数都必须变成 UAPI。

固定上游 `drivers/power/supply/bq25980_charger.c` 1269–1280 行只匹配 BQ25980/25975/25960；`qcom_smbx.c` 1034–1035 行匹配 PMI8998/PM660。未覆盖的器件不能靠改名接入；新扩展仍须遵守所属子系统接口和保护语义。

本轮没有修改有线驱动/配置/功率/温度参数。既有有线 PD 与 PPS 分别约 10 分钟、充电中无故障的稳定 L3 继续成立，见 [稳定记录](../hardware/2026-11-09-umi-pps-windows-l2.md)。迁移验收另需覆盖接口语义及既有保护，不将开放保护专项写成否定已闭合的稳定运行。

### DT：标准属性有固定含义

固定上游 `Documentation/devicetree/bindings/power/supply/power-supply.yaml` 的 `monitored-battery` 引用 simple-battery；`power-supplies` 表达供电来源。

[umi-pps.dtsi](../../../kernel/power/umi-pps.dtsi) 37 行将 `monitored-battery` 指向 `umi_fg`；`umi_pps.c` 895 行通过它取得运行时 power_supply，这是**已确认的语义误用**。相对地，FG/SMB5 的 `monitored-battery = <&umi_battery>` 引用 simple-battery，本身不是这个问题。

SMB5 的 `power-supplies = <&umi_fg>` 用于 BMS 查找；FG 是否应被表达为给该 charger 供电的 source 需要核对，列为**拓扑语义待核**，不与上述确定错误混同。不能只更换 DTS phandle：属性和驱动查找必须成套迁移，避免 probe 失败或失去安全观测。

仓库本地 NFC/FG/SMB5/PPS/observer 扩展没有配套 YAML。L2 的 dtc 编译成功不是 dt_binding_check/dtbs_check 通过；本轮未运行后两者。后续须按子系统提供 schema 与诊断处置，不用宽松 schema 隐藏错误。

### 无线 RX/TX 与共享传感器

`umi_pps.c` 读取的 `wireless_therm` 来自匹配板级 ADC 接线；这里只参与**有线 PPS** 温度模型。它不是无线接收/反充驱动，也不能证明无线功率、保护或 NFC RF 协调已完成。RX、TX 的状态/供电控制与测试必须各自建立，不能用 USB 在线或 PD 状态替代。

## 处理顺序与退出条件

1. **本轮完成方向整改**：PRODUCT RQ-14 替代旧 NFC 后续目标；ARCHITECTURE 维护规范，AGENTS 路由，AI_RULES 移植前对照，QUALITY 标准接口/DT/Debian 验收，PROVENANCE 来源对照；NFC/WP10 改 Linux NCI 路线。
2. **WP10 / TD-003**：先解决 SN100T 内核标准 NCI transport 与 binding，再标准用户空间验证，之后接 Debian 服务/权限。HCE/eSE 独立评估；不能把 Android HAL 作为默认下一步。
3. **WP9 / TD-004/005/007**：先审查有线接口和 binding 设计，分步迁移泵状态/控制、温控与消费者，不在无授权/回退时修改供电；标准 ABI、异常恢复和已验证 PD/PPS 一并回归。未知保护按 RISK-004 管理。
4. **WP2–WP6 / TD-002/006**：逐项撤除 bring-up 依赖，完成 DRM/input、PM 与 Debian；诊断保留独立用途。新增无线 RX/TX 按相同规范但独立验证。

责任工作包与偿还条件的权威正文在 [TECH_DEBT](../../TECH_DEBT.md)；SN100T 未知能力在 RISK-011，运行时 DTBO 问题在 RISK-010。本记录是带日期的审计基线，不另建重复进度表。

## 本轮验证

先输出 🚦 Audit Report 再执行完整验证。以下命令在仓库根目录执行；WSL 命令工作目录为 `/mnt/c/MiSeed`。

| 检查 | 命令 / 结果 |
| --- | --- |
| NFC 被动诊断 L1 | `python3 -m unittest discover -s tests -p test_nfc_diagnostics.py -v`：9 项通过，退出 0；fixture 包含动态 bus、异常 compatible、无绑定/缺失/非法属性、FIFO、节点类型、扫描上限；拦截设备 open 和写入 flags |
| 原控制台 L1 | `python3 -m unittest discover -s tests -p test_rescue_init.py -v`：19 项通过，退出 0 |
| 来源锁 L1 | `python3 -m unittest discover -s tests -p test_build_lock.py -v`：11 项通过，退出 0 |
| ARM64 L2 | `MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/boot-l2-linux-audit-20261110 MISEED_JOBS=8 bash tools/build_kernel_l2.sh`：退出 0，含 6 项 overlay 回归；Linux Image/DTB/modules、静态 initramfs 与 boot.img 生成 |
| 工具链 | `aarch64-linux-gnu-gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`；rescue init 以 `-Os -static -s -Wall -Wextra -Werror` 编译；readelf 确认 AArch64 ELF |
| 文档/来源检查 | `python -X utf8 tools/check_repo.py` 与 `git diff --check` 均退出 0；检查活动文档预算、链接与来源锁，人工核对接口边界和历史证据 |

构建检测既有缓存无法验证，保留它并自动使用全新 source/build 临时目录；没有复用未知构建树。make 报告部分新生成文件时间领先约 0.65–5.4 秒的 Clock skew 警告，最终退出 0 且产物齐全；记录该 WSL 时钟限制，不宣称零告警或可复现性已经验证。此次修改的 rescue init 独立交叉编译通过，不受旧对象复用影响。原始构建日志仅存忽略目录 `.work/boot-l2-linux-audit-20261110.log`。

| 输入 / 产物 | SHA-256 |
| --- | --- |
| kernel/rescue_init.c | `89f26b556bf8b581625395a20a833b8ca907e4ef6065a50f870eb2ef8ac47d61` |
| resolved.config | `e0161123b254782fc55d0487a55a507152057098ce692ccc354b6944f50163d7` |
| Image | `f69ec93fb1fa881895f3305595dfefba5590db89e0a87a80fb2fa5ffa1c010ba` |
| rescue-initramfs.cpio.gz | `88c5de96dec28f294d58669e9e663a87feb537ad281f81c98a59a1bc6ec6fbe9` |
| umi-csot.dtb | `896688420465aa8b4ebef182d77018fc02984c8dc5ca17d86b763fcbe528d2d0` |
| boot.img | `12195083ce94a8003e20b242957c325e086b786193700f7b40b31d9700b4cc38` |

**未覆盖**：未对本次镜像执行 RAM boot L3，未做 NFC 传输/RF/HCE/eSE 测试，未改供电参数或执行刷写/分区操作；未运行 DT schema 检查、Debian 集成或远端 CI。之前的 ADB Android 基线与本轮 L2 分属独立证据，设备保持 Android/普通 ADB。治理/驱动合并仍须按 QUALITY 做开发者以外的专项复核。
