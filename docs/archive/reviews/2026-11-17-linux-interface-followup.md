# 2026-11-17 Linux 标准接口语义复核与修正

## 基线与核实结论

最新本地提交 `b88fb3a2a81af178b83beef94dba830a20c7339f` 仅更新接口审计与 CONTEXT；本轮在原工作分支 `codex/fixed-pd-sampling` 继续修改，未提交/推送。固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`（v6.18.60）。宿主 Windows build 26200、x86_64、PowerShell 7.6.9；L2 使用 WSL Ubuntu 24.04 x86_64、aarch64 GCC 13.3.0。

[前次审计](2026-11-17-linux-interface-audit.md)总体判断成立，但“本地扩展”应区分**标准 ABI 的本地实现**和**尚未评审的新 binding/硬件模型**。本轮直接核对锁定源码，未使用移动分支或网页摘要替代版本证据：

- `Documentation/ABI/testing/sysfs-class-power` 已定义 `usb_type`、`input_voltage_limit`（µV）及 `charge_behaviour`。TCPM 增加 `INPUT_VOLTAGE_LIMIT` 支持，不是发明同名私有 UAPI，也不能据此声称已经上游接纳。该 ABI 明确不能作为安全关键保护；硬件保护与限流互锁仍独立保留。
- `Documentation/ABI/testing/sysfs-class-typec` 已定义 `select_usb_power_delivery`；锁定 `drivers/usb/typec/tcpm/tcpm.c` 的 `tcpm_pd_set()` 会直接更新 PDO/profile，因此必须核查它与本地限压状态的交互，不能只检查 power_supply setter。
- NFC 已采用 NFC/NCI + generic netlink/AF_NFC，显示采用 DRM/KMS/backlight。SN100T transport、CSOT 面板、新 compatible/binding 和热模型仍需对应子系统评审；未上游合入本身不等于使用私有用户 ABI。
- 无线 RX、无线 TX、eSE/HCE、完整触控、UEFI 产品启动及 Debian 集成仍按前次审计列为未实现/未闭合；本轮只改**有线**供电接口，未复用其参数到无线。

## 实施前接口映射与选择

| 功能 | 上游模型/ABI | 标准消费者 | 本地差异与处置 |
| --- | --- | --- | --- |
| 有线电源类型 | power_supply `usb_type`、`usb_types` 能力位图、uevent | sysfs/电源服务 | SMB5 原先仅查询 APSD，PD/PPS 被归为 BC1.2；读取 TCPM 的标准属性并补能力声明 |
| 有线 sink profile 与限压 | Type-C `select_usb_power_delivery`、power_supply `input_voltage_limit` | Type-C/sysfs 与现有内核充电策略 | 两个入口共享 TCPM port lock；限压控制期间不同 profile 返回 `-EBUSY`，同一 profile 保持幂等；reset 后释放控制 |

复用候选：直接改用既有 Type-C selector、TCPM PPS ONLINE/VOLTAGE_NOW/CURRENT_NOW、power_supply INPUT_VOLTAGE_LIMIT。PPS 继续使用既有接口；Type-C selector 是能力选择，不能代替内核策略所需的 µV 上限与协商读回。`typec_port_set_usb_power_delivery()` 仅维护公开链接，不触发 PDO 更新/协商，不能把它误当内核 profile setter。故保留标准 power_supply 属性的本地 TCPM 实现，修补它与既有入口的冲突，不新增 ABI/导出函数。TD-004 仍由 WP9 负责向 USB Type-C/TCPM 与 power-supply 维护者提交通用限压/仲裁方案，审查锁序、协商失败、reset 语义、用户态兼容及多控制者权限，获接纳后再撤本地补丁；不能以本轮 L2 代替该评审。

## 已修正的问题与失败边界

1. `qcom_pm8150b_supply.c`：在线先读取 TCPM 的 `USB_TYPE`，PD/PD_PPS/PD_DRP 如实返回；普通非 PD 连接继续 APSD 的 SDP/CDP/DCP 路径。断开返回 Unknown，避免暴露残留能力；供应者/硬件读错直接传播，不把失败假报为普通 USB。供应者调用在 SMB5 lock 外，原有 power_supply_changed 周期通知继续覆盖属性变化。只调整报告，不改协商、充电阈值或实际限流。
2. `tcpm_fixed_profile.inc`、补丁应用器：原 `tcpm_pd_set()` 可在本地限压选择有效时改变 selected_pd/PDO，令限压读回与选中能力分离，且可能移除当前 PPS APDO。现在先在 port lock 内检查冲突，失败时不触碰 PDO、链接、AMS 或当前限压；默认无本地选择时保留原上游入口。reset 后重新允许选择，失败恢复标记仍阻止冲突。限制是本地仲裁实现，未声称多控制者设计已获上游认可。

回归直接编译实际 C 实现：USB 类型覆盖断开、固定 PD、PPS、PD_DRP、BC1.2、无 PD supplier、EIO/EAGAIN；profile 覆盖控制前、控制中、PPS 激活时、相同选择、reset/恢复及旧协商反例。补丁应用回归校验检查确实插入上游 setter 的状态修改之前；来源锁同步本轮输入哈希，原许可证与署名保留。

## 验证与 Audit

| 验证 | 命令/环境 | 结果 |
| --- | --- | --- |
| L0 仓库检查 | `python -X utf8 tools/check_repo.py`；`git diff --check` | 退出 0；已登记的旧源码篇幅黄灯仍在，本轮修改源码全部≤300行 |
| L1 宿主回归 | `tools/test_host.ps1`（含 Windows/WSL 回归和 ShellCheck error 级） | 退出 0；Windows 284 项、69 项平台跳过；WSL 284 项、4 项 DT/schema 条件跳过，随后专项补齐 |
| L2 arm64 | 下列原生 DSI 构建 | 退出 0；完整 Image、DTB、boot.img；包括 TCPM 与本轮 SMB5 对象；构建保存的来源锁与当前文件逐字相同 |
| DT/schema 专项 | 下列 `validate_kernel_dt.py` | 退出 0；定向 binding、板级/最终 DTB、4 项正反例均过且零跳过，补齐 WSL 全套跳过项 |

L2 在 WSL 运行 `bash tools/build_kernel_l2.sh`，环境为 `MISEED_NATIVE_DSI=1`、`MISEED_JOBS=8`、`MISEED_WORK_ROOT=/home/xiaoluo/.cache/miseed-l2-20261113`、`MISEED_EXTRACTED_SOURCE=$MISEED_WORK_ROOT/source-interface-followup-20261117`、`MISEED_BUILD_ROOT=$MISEED_WORK_ROOT/build-interface-followup-20261117`、`MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/interface-followup-native-20261117`。使用新源码与构建目录，从锁定 Git cache 提取；没有复用旧对象冒充新 L2。

DT 专项：`/home/xiaoluo/.venvs/miseed-dtschema-2025.8/bin/python tools/validate_kernel_dt.py --source /home/xiaoluo/.cache/miseed-l2-20261113/source-interface-followup-20261117 --build /home/xiaoluo/.cache/miseed-l2-20261113/build-interface-followup-20261117 --dtb /mnt/c/MiSeed/.work/interface-followup-native-20261117/umi-csot.dtb`。此为定向验证，不等于整机所有 binding 验收。

关键 SHA-256：

- Image：`1c410e2b57b1a5c3c791425c6e065823d01db116391a278aaddab6d90bea555e`
- 最终 DTB：`8be099adb994ba2f7e44ebfa3c72568896a36b55f72474fe9789bf80647a0573`
- boot.img：`7a13a5edffd6459e9a620a88e488b28f0daf4b7c61341e7f3a33f0c91cdbf918`
- 应用补丁后的 TCPM C：`7aeffa34575cb2441a5c0882951236c0b4d124f0750a20f96e4a24249317c936`

宿主完整日志留在 Git 忽略的 `.work/interface-followup-host-tests.log`、`interface-followup-l2.log`、`interface-followup-schema.log`；产物留在上述输出目录，不提交长日志或镜像。本轮未连接手机、刷写、修改分区或实际供电。

🚦 Audit：本轮改动范围/事实、标准接口语义、错误路径、来源哈希、L0/L1/L2 和定向 schema 为绿灯；修改的自维护源码最长 273 行，文档预算合规。既有源码篇幅、TD-004 本地实现与上游评审为黄灯，责任及退出条件沿用 TD-004/008。本轮共 10 个文件，未创建/更新 PR，不将本轮规模当作整个 PR 规模。合并验收仍为红灯，须按下述边界补证；完成编译不解除该门槛。

合并边界：本地扩展的上游评审、开发者以外供电专项复核、适用新源码 L3 与精确 PR HEAD CI 未由本轮补证。原 PPS/90Hz L3 保留其已完成结论与准确版本范围；本轮不将新源码自动计为 L3。
