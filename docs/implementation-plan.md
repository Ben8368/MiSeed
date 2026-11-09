# 工作包与依赖

这里只定义顺序、交付和完成条件；当前进度归 CONTEXT，证据等级归 QUALITY。

| 包 | 前置 | 交付与完成条件 |
| --- | --- | --- |
| WP0 文档 / CI | 需求 | 权威文档与 L0/L1 工具；本地回归、同 HEAD 远端门禁与专项复核 |
| WP1 输入锁定 | WP0 | 本机只读身份/变体、恢复方案、来源比较/许可及源码/工具链锁；关键未知有依据，采集入口见 [platforms](platforms.md) |
| WP2 Linux 直启 / Recovery | WP1 | config/DTS/补丁、独立 Recovery initramfs、RAM 包；L2 构建，L3 Shell/USB/显示/触控及默认无自动写盘 |
| WP3 第二阶段 UEFI | WP1 | BootShim、板级描述、EFI 测试载荷；L2 构建，L3 EFI 执行/映射/返回 |
| WP4 交接集成 | WP2、WP3 | EFI Stub + DTB + initramfs；同设备/输入锁上 L3 UEFI → Linux → Recovery Shell |
| WP5 Debian | WP4 | ARM64 快照配方、包清单与登录；L2 用户空间生成，L3 同内核的正常/Recovery 选择、登录及 rootfs 缺失救援 |
| WP6 外设 / 持久化 | WP5、安全审查 | GPU/Wi-Fi/温控逐项验收；Recovery 包验证先过 L1，安装/更新/备份恢复依 [契约](RECOVERY.md) 审查并独立授权 L3 |
| WP7 Fastboot 包 | WP6 | 设备/槽位/输入校验与恢复说明；L1 打包、L2 固定构建、独立授权 L3 安装/恢复 |
| WP8 EDL 可行性 | WP7、外部前提 | 证明 Programmer 来源/授权/兼容性后才实现包格式；无前提则仅研究 |
| WP9 USB 启动状态与基本供电 | WP1、WP2 | 放大横屏日志、USB 控制、Linux→Bootloader Fastboot→RAM Linux 往返及返回 Android，分项记录 L2/L3 和板级限制。基于匹配本机数据实现电源/充电，完成过流、过压、过温、故障保护审查、独立复核及授权 L3；保护不完整时不充电。任何 BCB/misc 写入须另行授权和回退方案；当前能力见 [platforms](platforms.md) |
| WP10 Android 兼容 NFC / eSE | 内核接口与真机 probe 阶段完成；完整 Android HAL 需 Android 用户空间基座 | 已固定并移植 NQ-NCI `/dev/nq-nci` 和 ioctl ABI、设备树/I²C/GPIO 资源，L2 构建及 Linux RAM boot/NFCC probe L3 完成；完整 Android NFC 仍需固定 HAL、eSE middleware、配置和固件许可闭包，并在目标 Android 用户空间验证 discovery/NDEF/HCE/eSE。当前边界见 [NFC](NFC.md) 和 [L3 记录](archive/hardware/2026-11-09-umi-nfc-l2-ramboot.md) |

WP2、WP3 可独立推进，WP4 汇合两条路径。工作包不授权设备操作或发布；未实现的构建 CLI 不写成可用命令。

## WP9 充电日志验收

RQ-11：后台记录独立于 USB 终端，断线期间继续采集只读输入/硬件充电状态；温度、电量及电池 V/A 缺少可信驱动时明确不可用。日志保存在有界 RAM，标明时间、序号、覆盖或内核丢失；导出不消耗记录、不隐式启动充电。
同次启动内完成电脑→充电器→电脑切换，导出核对启动标识、连续序号及接入/拔出事件；另验证重复导出、终端再次挂断、缓冲覆盖和读取错误。重启后保留不在本阶段范围。

## 里程碑与需求对应

需求编号来自 [PRODUCT](PRODUCT.md)，以下仅定义验收范围，不预先承诺版本号或发布日期。

| 里程碑 | 对应需求 / 工作包 | 必须取得的结果 |
| --- | --- | --- |
| M1 首个集成启动 | RQ-01/02/05/06/07；WP1–WP4 | 固定输入上完成第二阶段 UEFI → 自建 Linux → Recovery Shell，同机验证 USB/显示/实际触控，并保留 Android 正常启动路径 |
| M2 完整 Linux MVP | RQ-01/02/06；M1 + WP5 | Debian ARM64 最小用户空间登录，正常/Recovery 两种入口分别通过，正常 rootfs 缺失时救援仍可用 |
| M3 安装与发行 | RQ-04/06；WP6–WP7 | 包校验与计划生成先过离线反例；独立授权的安装、中断/恢复和数据影响验证后，才声明 Fastboot 安装能力 |
| M4 条件式 EDL | RQ-04；WP8 | 外部前提成立后独立验证 Programmer 与设备写入/恢复；无前提则仅保留研究结论 |

RQ-03 的旧成果审查归 WP1，RQ-05 的多来源比较及 RQ-07 的语言边界贯穿后续工作包；M1 不等于 M2，也不替代 L2/L3 证据。

## WP1 开发输入交付

1. 按 [platforms](platforms.md)采集脱敏身份，补齐真实变体、底层固件与启动契约；未知字段逐项关联风险，不能猜值。
2. 建立可核查的备份/回退方案，明确失败停止条件及哪些动作还需要当轮授权；不能以 EDL 假设代替回退依据。
3. 按 [PROVENANCE](PROVENANCE.md)完成逐功能来源评估、许可与依赖审查，再交付完整源码/工具链/配置锁；调查 SHA 不算构建锁。评估已有 fork 时单列旧验证与待重验范围。
4. 为 WP2/WP3 列出构建入口、目标产物、L2 环境和 L3 验证计划；缺失项登记到 [风险](RISK_REGISTER.md)，阻断受影响的构建/设备操作，不以清单已填写代替通过。

WP0 的远端门禁约束托管后的 PR 验收与合并；本地文档维护和只读调研可先进行，不据此声明 WP0 已完成。每个工作包的证据记录都要关联需求编号和输入身份，归档后再更新相应权威状态。
