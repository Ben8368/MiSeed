# 2026-11-16 Linux 接口收尾与有线限流修复

## 范围与基线

本轮从 `9eb413386c9646a0e9fc0b8ed98165442602e0ad` 开始，在 PR7 工作分支实施。宿主 Windows 11 build 26200 / x86_64 / PowerShell 7.6.9；目标编译在 WSL Ubuntu 24.04 x86_64，GCC 13.3.0、make 4.3。固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`（v6.18.60）。未连接设备、刷写、修改分区或供电；不合并 PR。已有 L3 结论保持原证据范围。

## 全项目接口盘点

| 功能/目标 | 锁定 Linux 子系统与 ABI | 标准消费者 | 当前本地差异及本轮处置 |
| --- | --- | --- | --- |
| 电池 FG | power_supply；电流/电压 µA/µV，电池温度 0.1°C | power_supply sysfs/uevent、UPower | 本地 FG 驱动，无 Android HAL/Binder 依赖；硬件代际与标度按原记录，非上游已合入 |
| **有线** SMB5 固定 PD | power_supply charge_behaviour/input_current_limit/阈值/status/health；IIO ADC | `tools/wired_power.py`、标准属性/事件 | 删除 charge_test sysfs/Kconfig/诊断命令和有界测试状态；保留自动策略、失败关断与 watchdog；修复两项限流缺陷 |
| **有线** BQ/PPS 协作 | platform/I2C/regmap/auxiliary、power_supply、thermal、Type-C/TCPM | 标准充电属性与 thermal zone/cooling device | 所有本地供电跨模块导出撤除，SMB5/BQ/PPS 合入一个模块；互锁保留为私有实现细节，非新 ABI；注册失败回滚与卸载顺序新增测试 |
| USB PD/PPS 协商 | Type-C/TCPM；ONLINE/VOLTAGE_NOW/CURRENT_NOW，INPUT_VOLTAGE_LIMIT | power_supply；Type-C select_usb_power_delivery | 前三项由固定 TCPM 实现；固定档限压是标准属性的本地扩展，尚未上游合入。保留硬件实测与故障锁，不能把属性写入成功当保护有效 |
| **有线** 温控 | IIO_TEMP、thermal zone/cooling、power_supply TEMP | thermal sysfs/内核 governor | 固定 PD 与 PPS 热模型分别保留；本地虚拟温度模型/温标 L3 与独立复核未闭，未改阈值 |
| **无线 RX** | 应为独立 power_supply/thermal | 未实现 | 没有 RX 驱动，不套用有线接口、参数或验证 |
| **无线 TX** | 应为独立反向供电/thermal 模型 | 未实现 | 没有 TX 实现，不能从 RX、OTG 或有线测试推导 |
| NFC | NFC/NCI、generic netlink、AF_NFC、gpiod/I2C | `rescue_nfc_netlink.c` / 标准 NFC 工具 | 当前无 NQ 私有字符设备/ioctl；SN100T transport/binding 为本地实现；共享 VEN 禁复位、eSE 未实现 |
| 显示 | DRM/KMS、MIPI DSI、backlight、gpiod、时钟/genpd | KMS、标准 backlight；fbdev 兼容诊断 | CSOT 面板及 90Hz 本地驱动；继承供电、SMMU/根属性交接仍登记 TD-006/RISK-010；未伪造 PM 完成 |
| DT | 标准 DT 核心、供应/资源 phandle 与 YAML binding | OF 驱动模型 | 本地芯片专用属性/compatible 有 YAML；未冒用近似芯片 compatible，定向 schema 不等于已上游合入或整机合规 |
| 日志/USB 救援 | configfs ACM/TTY、标准 power_supply/thermal/NFC | 静态 Linux initramfs | CHARGE_LOG/printk/TCPM debugfs 仅诊断；不承担产品控制或保护；移除诊断枚举中的 vendor pd_current_max |
| 启动/UEFI/Debian/触控等 | EFI Stub、input/evdev、cfg80211 等规划 | 尚未完成产品消费者 | ABL/header-v2 是诊断封装；仓库无完整 UEFI/Debian/触控实现，本轮不将缺失功能虚报为接口迁移完成 |

接口依据直接核对固定源码的 `Documentation/ABI/testing/sysfs-class-power`、`sysfs-class-typec`、`Documentation/power/power_supply_class.rst`、`Documentation/driver-api/auxiliary_bus.rst`、`include/linux/auxiliary_bus.h`、`drivers/usb/typec/tcpm/tcpm.c` 与 NFC/DRM 文档。网页查询无可用内容时不替代该锁定源码证据。

### 为什么不以属性写入替代原子互锁

上游 power_supply 没有“独占 SMB5、停止电池充电、暂停系统输入、交接 PPS、携带单调 SOC 阶段”的原子事务。`charge_behaviour=inhibit-charge` 只表示禁止充电，`force-discharge` 也不是策略所有权锁。将这些私有控制重新编码到 ONLINE 或标准枚举会篡改语义，并暴露并发窗口。本轮将机制限制在同一驱动模块，外部只保留已定义的 ABI；auxiliary bus 负责设备绑定与生命周期，而不是声称提供充电协议。BQ 后端保留独立源文件和 I2C 所有者，原贡献者/许可证不变。

TCPM 原有 `select_usb_power_delivery` 是用户态 Type-C 档案选择，固定 `include/linux/usb/typec.h` 没有提供等价内核策略调用；不能通过内核写 sysfs 来替代。现有 input_voltage_limit 通用扩展继续作为未合入的本地实现登记 TD-004，需子系统评审。ABI 文档明确该属性不能作为安全关键保护，实际安全仍依赖独立电流/电压/温度验证、停止与硬件 watchdog。

## 修复与反例

1. **输入限流越过策略**：用户写入先验证范围及所有权，读取当前有效 ICL。更低上限立即写入；更高上限仅保存请求，在锁内安排 worker，以新鲜遥测、PD 合约、SOC 功率、温度及 100mA 渐升计算；读回仍为当前生效 ICL。回归覆盖 1A 有效限流时请求 3A 不直接升流、零请求、读写失败与互锁拒绝。
2. **零限流无法恢复**：独立 `policy_icl_valid` 区分未初始化与合法零值；温度降到零不覆盖保存上限。连续回归验证 2A→0→清除温降→50 轮恢复 2A；显式用户零上限仍保持零，失败不消费上限。
3. **驱动边界**：注册按 SMB5→PPS auxiliary driver→BQ I2C；退出先停 PPS，再移除 BQ 与 SMB5，保证 stop 时 supplier/IRQ/regmap 仍在。auxiliary ID 与新的 KBUILD_MODNAME 一致；无重复 module_init/exit。源代码测试和真实模块 L2 分别验证。
4. **退出并发**：用户行为 setter、限流 setter 和内部 release 的 work 排队都在 stopping 同一锁内完成，避免解锁后才排队越过 cancel。

## 🚦 Audit Report

- 范围/来源：未导入外部代码、未更改电气保护阈值/采样电阻；保留原版权并更新构建输入锁。
- 接口：私有用户态充电控制与跨模块本地供电导出移除；标准 ABI 的本地实现、上游合入状态和 L2/L3 分开。
- 验证：最终命令、退出码与产物哈希见后续验证节；任何未完成检查不标绿。
- 未覆盖：本次新镜像 L3、开发者以外独立专项复核、远端精确 HEAD CI；三者不因本地 L2 通过而关闭。无线 RX/TX、UEFI、Debian、eSE、PM/根交接仍按权威记录。
- 篇幅：SMB5 policy 删除诊断后降至 300 行以下；其他既有黄灯见 TD-008。PR7 规模按最终 GitHub additions/changed_files 复核，不扩展无关功能。

## 验证记录

所有以下最终命令退出 0。日志/产物仅存忽略目录 `.work/`，没有加入 Git；供电参数不变且未进行手机操作。

| 验证 | 最终结果 |
| --- | --- |
| `./tools/test_host.ps1` | 仓库 L0 通过；Windows Python 3.14：283 项/89.866s，69 项平台跳过；WSL Python 3.12：283 项/57.487s，4 项缺 DT 环境跳过；ShellCheck error 级通过 |
| 带真实 DT/schema 的 Linux 全量 | `python -X utf8 -m unittest discover -s tests -v`，283 项/65.149s，零跳过；包含故障/恢复、注册回滚、实际函数限流反例 |
| 原生显示 ARM64 L2 | Image、DTB、静态 rescue-init、initramfs、boot.img 全部成功；无编译或时钟告警 |
| 默认 simpledrm ARM64 L2 | 同上，使用独立干净源码/构建目录，对应 CI 默认配置 |
| 两套 DT/schema | `dt_binding_check`、`CHECK_DTBS`、最终打包 DTB 定向验证及四项 DT 正反例分别成功，无告警/跳过 |
| 模块 L2 | `CHARGER_MISEED_PM8150B=m`、BQ/PPS 子功能=y；完整 Image+modules 重建通过；生成单一 `qcom_pm8150b_charger.ko`，vermagic `6.18.60 SMP aarch64` |
| 模块/内建边界 | 均含 SMB5/BQ/PPS probe 与唯一 umi_wired_init；无 `__ksymtab_umi_smb5*`/`__ksymtab_bq25970*`/charge_test；auxiliary alias 与实际 KBUILD_MODNAME 一致 |
| 产物闭包 | 两套打包 Image/DTB/initramfs 与源载荷逐字节一致；build-source-lock.json 与当前来源锁一致；两套 staged power 源码与工作区全部 28 个输入一致 |
| 文本/版本 | `git diff --check`、仓库文本/来源锁/预算检查通过；旧篇幅黄灯留 TD-008，SMB5 policy 已降为绿灯 |

### 可重跑命令

在 WSL Ubuntu-24.04、仓库 `/mnt/c/MiSeed` 内执行；所有路径均为离线构建缓存，不接触手机。

```sh
base=/home/xiaoluo/.cache/miseed-l2-20261113
export PATH="$base/dtschema/bin:$PATH"
# 原生；默认配置将 source/build/output 后缀改为 interfaces-simple-20261116，DSI=0
MISEED_WORK_ROOT="$base" \
MISEED_EXTRACTED_SOURCE="$base/source-interfaces-20261116" \
MISEED_BUILD_ROOT="$base/build-interfaces-20261116" \
MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/interfaces-l2-native-20261116 \
MISEED_NATIVE_DSI=1 MISEED_JOBS=6 bash tools/build_kernel_l2.sh
python tools/validate_kernel_dt.py \
  --source "$base/source-interfaces-20261116" \
  --build "$base/build-interfaces-20261116" \
  --dtb .work/interfaces-l2-native-20261116/umi-csot.dtb
MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/interfaces-l2-native-20261116/umi-csot.dtb \
MISEED_DT_SCHEMA="$base/build-interfaces-20261116/Documentation/devicetree/bindings/processed-schema.json" \
  python -X utf8 -m unittest discover -s tests -v
# 模块测试目录为独立配置副本，不能仅复用内建 vmlinux 导出表运行 modules
python3 tools/run_build_command.py -- make \
  -C "$base/source-interfaces-20261116" \
  O="$base/build-interfaces-module-20261116" ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- -j4 Image modules
```

收尾中曾出现测试桩未补新增字段、旧诊断断言、编辑过程来源锁暂时不匹配，均修正后重新执行完整最终回归。首轮从内建构建复制的模块目录只运行 modules 时，旧 vmlinux 导出表导致 regmap-i2c 重复导出；完整 Image+modules 重建后通过，没有跳过 modpost 或关闭验证。

### 产物 SHA-256

| 产物 | 原生90Hz | 默认simpledrm |
| --- | --- | --- |
| Image | `6f6cd89c0da7c2b205ac572ab1aaffb575ef6b8f667b691eaa8c4292444984e8` | `0a1aa2ae46badb8dfe36f41a603952cff143295ac2e07cc00954ad76577e157d` |
| umi-csot.dtb | `8be099adb994ba2f7e44ebfa3c72568896a36b55f72474fe9789bf80647a0573` | `0020226ac3b50875cbe876ca1ce229429d4b38798d30aeb85612a1956c61cfba` |
| rescue-initramfs.cpio.gz | `c936b7fc6c6dd946773e69f70bacab716d55ea7552e520081a3418669f66342e` | `c936b7fc6c6dd946773e69f70bacab716d55ea7552e520081a3418669f66342e` |
| resolved.config | `f03cf5a515ff9a457d08e4853b9600e7a6c89422270c22fc6a13643a3a076f38` | `870275f59d8ff02f8cc5a112e3861c69a6852cc1833dda3943b78cc37c11bd57` |
| boot.img | `23c40418a5494301e4119687cbef5a6f09f733b29e488e1d96f2a86a73e128d8` | `6fd05f2fdc026dd623afa7ea57d044a4c6f6d2df55352dbc466622aa2fdde486` |
| build-source-lock.json | `c6ba5cd6f5867246cd982ceeb4c409b0980838041a9d5b4fe548c707d374c913` | `c6ba5cd6f5867246cd982ceeb4c409b0980838041a9d5b4fe548c707d374c913` |

模块 `qcom_pm8150b_charger.ko` SHA-256：`a3503dcb3bcfa374eee3306a645b03a3a5fff7269f0222554343e5f6417c16e8`。

| 最终日志（`.work/`） | SHA-256 |
| --- | --- |
| interfaces-host-final.log | `43cff55241ba399c4f9e231a31716a6d8ed6cec915b01fb9e9fa9709ef27ab1a` |
| interfaces-linux-all-final.log | `dc78e07523a674c9fbc54ff7908167d57d1709a94b55e3d7c02aaf69794e4999` |
| interfaces-l2-native-final.log | `15c4f8167ef61398bed8e818eda48d275a0ef772815bd65475dd7b83106ebda9` |
| interfaces-l2-simple-final.log | `97578f9e949a791b7568af2697a804dbae6e70338451fcfacd03f7088de1e261` |
| interfaces-schema-final.log | `072c21b4513831529fe3d679ed2b73e26f9b71596977da091857f5990d61763f` |
| interfaces-schema-simple-final.log | `1c44be3879b9f6bbeb819eb479e39909f09f1d5694f7a66ec54b7445e7bf5c0a` |
| interfaces-module-final.log | `6cd9378cfebbe08b0532b942502b4e93a002de808633d90db18e8331e29fb380` |

🟢本轮标准接口收尾、限流修复及所列 L0/L1/L2/schema；🟡本地 TCPM/binding 尚未上游合入及已有技术债；🔴新镜像 L3、开发者以外专项复核与精确 HEAD CI 未齐时，合并验收仍不通过。此结论不否定历史 L3 已闭合能力，亦不把本轮编译冒充新 L3。按授权提交/推送 PR7，不等待 CI、不合并；PR 中记录最终 HEAD、规模与当时 CI 状态。
