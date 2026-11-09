# 2026-11-08 LineageOS Umi 快充 / PPS 主线适配

## 范围与来源身份

对应 RQ-12 / WP9；承接 RQ-08 的电源管理与 RQ-11 的断线记录。用户指定 `https://github.com/LineageOS/android_kernel_xiaomi_sm8250/tree/lineage-23.2`，并报告长期真机使用稳定。2026-11-08 `git ls-remote` 解析该分支为 `71b13e62f057a649b77fe4062feb73ee72ad609c`，与此前 Android 版本记录相符。来源用户报告不是本次 MiSeed 构建的 PPS L3，也没有提供逐项保护故障或当前板级采样参数。

本地从干净 `main` 的 `109225e` 创建 `codex/umi-pd-pps-fastcharge`。宿主 Windows 10.0.26200 x64、PowerShell 7.6.9；离线 C 和目标构建使用 Ubuntu 24.04 WSL x86_64。源码验证阶段未提交、推送或创建 PR；后续用户已授权提交、推送并创建草稿 PR，发布状态由 CONTEXT 记录。本轮未启动手机或修改真实供电。

## 选择与依赖闭包

| 层 | 采用 / 处理 |
| --- | --- |
| 参考策略 | TI 目录 `pd_policy_manager.c/.h` 的 2:1 目标、20 mV 步进、6000 mA 电池 / 3000 mA 总线限制、温区和退出思路；用主线接口重写集成 |
| 充电泵 | `bq2597x_charger.c` 与 `bq25970_reg.h` 的 BQ25970 寄存器编码；明确拒绝共享地址的 SC8551/NU2105/BQ25968，不套用错误 ADC 比例 |
| 板级 | `umi-sm8250.dtsi`、`umi-pinctrl.dtsi`；递归核对 `kona-qupv3.dtsi` 的 SE15 地址 `0x884000`、IRQ 583，以及 Kona pinctrl。GPIO68 中断和 I2C 地址 `0x66` 仅作为禁用接线描述 |
| PD 传输 | 参考 `drivers/usb/pd/policy_engine.c`、`qpnp-pdphy.c`，不复制；重用固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 原生 TCPM、qcom PM8150B Type-C/PD PHY |
| 主充 / 遥测 | 保留已有 MiSeed FG/SMB5，新增独占与输入暂停接口，防止两条充电控制路径并行 |
| 构建 / 操作 | 受控 staging、源码缓存白名单、内建配置/对象/符号检查、禁用 DT 节点检查；救援控制命令与已有 RAM 日志 |

GitHub recursive tree API 返回 `truncated=true`，本次没有把它当完整目录清单；所用文件由确定路径在完整 SHA 下逐个下载，必要依赖单独获取。原始路径和 SHA-256 位于来源锁的 `lineage-umi-power-runtime-reference`，不提交下载树、PDF 或产物。构建无需联网下载这些参考文件；本地适配文件直接 staging 到固定 Linux，原生 TCPM 文件不修改。新增文件与现有 FG/SMB5 同批 staging，无额外补丁排序依赖。

对照：colorgarden 的 BQ 路径缺少 PD/PPS 且存在保护关闭/延迟启泵，未采用；整体搬运 Lineage 私有 power_supply/usbpd/pmic-voter ABI 会扩大内核修改且不适配 6.18，未采用；主线 TCPM 已提供可读回的 PPS voltage/current/online 接口，故复用协议栈，仅实现板级协调和策略。

## 许可证与归属

BQ 原驱动：Copyright (C) 2017 Texas Instruments、GPL v2 notice、MODULE_AUTHOR Texas Instruments。策略：MODULE_AUTHOR Fei Jiang <jiangfei1@xiaomi.com>、MODULE_LICENSE GPL；策略头注明 a0220433 / Mar 27, 2017。根 COPYING 明确 GPL version 2 only（syscall exception 不扩展至这里的内核内部派生文件）。寄存器头与 DTS 在该固定内核的许可框架下选择性适配；本地派生文件标为 GPL-2.0-only 并保留上述归属。未导入厂商二进制、认证或电池 profile；Linux 原生驱动许可不变。详细 [NOTICE](../../../kernel/power/NOTICE.md)，分发审查仍按 RISK-005。

## 准入与风险

- 允许禁用状态的源码和 L2 编译移植；**不准入主动快充或默认启泵**。本分支不能声称“手机已实现快充”。
- 参考 Umi DTS 关闭 BAT_OCP、TSBAT/TSBUS/TDIE 等保护，sense-resistor 与电池 OCP 参数被注释；驱动忽略若干初始化错误并关闭 watchdog。拒绝照搬；缺失板级参数无默认值，probe 失败。
- TI 公开 `bq25970.pdf`（SLUSD72B，13 页简版）没有完整寄存器/计时器细节。5 秒 watchdog 编码可从参考头核对，但刷新/超时停止、VOUT_OVP 位语义、实际热敏网络等仍需完整硬件依据和 L3。不得从“参考内核稳定”推导这些保护在新环境已经验证。
- RISK-004 持续阻断运行/发布；RISK-010 重复 PMIC 与 TD-002 TLMM 未修复。Type-C VBUS 供给、GPIO8/相关电源路径、实际泵 ID 也需核实。未通过添加假校准、关闭保护或放开全部 GPIO 绕过。
- 只实现 sink/device PPS；没有导入 Qualcomm QC、双泵、私有快充认证、温控降额表或长期自动充电。受控接口、参数及状态说明见 [PPS](../../../kernel/power/PPS.md)。

## 本地改动与失败处理

停主充并独占 → 保护配置/读回 → PPS 激活与能力读取 → 电压/电流请求与合约确认 → BMS/ADC/硬件故障复核 → 启泵。循环执行电压/电流限制、APDO 重检、双 VBAT 一致性、保护读回及绝对期限检查。软件阈值为参考上限加保守门控，不代表经本机校准的充电曲线。

停止按停泵（含 switching 读回）→ 固定合约（5 V / ≤0.5 A 读回）→ 释放主充独占排序；部分失败保留独占和错误状态，重试且不独立喂 watchdog。主充恢复仍 inhibit。并发普通充电准备/启用在独占期间返回 EBUSY，统一 `charge-stop` 先停 PPS。挂起失败拒绝进入睡眠；供应者 device link 先拆消费者，避免供应者卸载后访问已释放状态。

## 验证记录

最终本地结果如下；所有合成板级参数仅为测试夹具，不能复制为硬件配置。

| 等级 / 环境 | 准确命令（在仓库根目录） | 退出码 / 结果 |
| --- | --- | --- |
| L0 / Windows | `python -X utf8 tools/check_repo.py`；`git diff --check` | 0 / 通过；最终文档更新后已复查 |
| L1 / Windows Python 3.14 | `python -X utf8 -m unittest discover -s tests -v` | 0 / 81 项，42 通过，39 因 POSIX、C 编译器或 Linux 专属夹具跳过 |
| L1 / Ubuntu 24.04 WSL | `python3 -m unittest discover -s tests -v` | 0 / 81 项全部通过，无跳过 |
| Shell / WSL | `shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` | 0 / 无 error |
| L2 / WSL | `MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/pps-boot-l2 bash tools/build_kernel_l2.sh` | 0 / Image、DTB、modules、静态 rescue ELF、initramfs 与 Android v2 boot 打包通过 |

L2 为固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，`aarch64-linux-gnu-gcc 13.3.0`（Ubuntu 13.3.0-6ubuntu2~24.04.1）、GNU make 4.3。PPS/FG/SMB5 对象及 PPS/PMIC Type-C probe 链接符号存在；DTB 的 `umi_pps`、`pm8150b_typec`、`i2c15`、`tlmm`、`pm8150b_vbus` 全部经 fdtget 确认为 disabled，原 ABL 锚点检查通过。最终 20 个工作区构建输入哈希与构建日志逐个复核一致。

产物仅位于忽略的 `.work/pps-boot-l2/`，未发布；`boot.img` 7,806,976 bytes，SHA-256 `d2d52ba8c3f8f18b4204996eddb9ca4ab9c2fb682a7b0f5c78f38b7b76ad1c51`。`umi_pps.c` SHA-256 `265244681c391662f4d624f41ec44a67c977cb52c2c3bd3e0b2f6f49a6e51235`。完整日志保留在忽略的 `.work/pps-l2-build.txt`、`.work/pps-wsl-tests.txt` 和 `.work/pps-windows-tests.txt`，不提交长原始日志。

测试包含实际控制器函数的协商/合约读回、能力缩水、保护寄存器篡改、锁存故障、ADC/BMS 异常、协商期间温升、60/600 秒期限、拔线、部分停止失败，以及初始化/运行循环的逐 I/O 故障注入；实际 SMB5 独占函数、原 0.5 A 策略、PPS 设备唯一性、USB 命令和统一停充也有回归。测试的寄存器、时间、PD 完成和 watchdog 都是模拟，不证明实际硬件电气行为。

中途失败如实保留：首次 L2 被配置门禁拦截（TCPM 还依赖 USB 核心，已补齐且仍为 gadget）；第二次 L2 的 sysfs API 名称与固定基线不符，改为 `devm_device_add_group` 后通过。新增测试夹具曾缺少 stddef/命令分派及存在编译告警，已修复并完整复跑。它们不被记为早期通过。

未覆盖：本项目 PPS 真机协商/启泵、实际功率、长时充电、硬件保护与 watchdog 效果、其他芯片变体、独立人工专项复核、远端当前分支 CI。上述验证记录截至发布前；提交后的远端状态须按当前 PR HEAD 单独核验，不能把旧 PR #2 CI 当成本分支结果。结论仅为 L0/L1/L2；RISK-004、RISK-010 与 TD-002 继续阻断运行准入和快充可用声明。
