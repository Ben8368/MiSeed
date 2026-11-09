# 产品范围

MiSeed 为小米 10 提供独立 UEFI、Linux 主线适配与 Recovery，以及构建组合和镜像打包。长期面向多系统，首个目标是 Linux；完整内核树与发行版包由外部固定来源提供，本仓库不维护发行版包全集。

## MVP

- 内核满足 6.18+；第二阶段 UEFI 加载 Linux 并进入 Recovery 救援 Shell。
- USB 诊断/登录、目标面板显示及实际触控变体可用。
- 集成 Debian 13（trixie）ARM64 最小用户空间并登录。
- 初期采用 RAM 或经核实的外部存储，保留现有 Android 数据和正常启动路径。

具体接口归 ARCHITECTURE，设备与能力证据归 platforms，工作包及完成条件归 implementation-plan。

## 需求追溯

依据为[原始需求背景](archive/research/2026-11-05-background.md)与[Recovery 补充](archive/research/2026-11-05-recovery-followup.md)对应的引用对话；轮次按原对话时间顺序编号。下表区分用户要求与项目解释，编号供移植记录和工作包引用。

| 编号 | 用户表达与来源 | 开发依据 / 解释 |
| --- | --- | --- |
| RQ-01 | 第 1 轮：UEFI、Linux 6.18 以上，多系统愿景，Linux 为 MVP；考虑 Debian 或更新及时的发行版 | 本文 MVP；Debian 13 ARM64 是项目首选设计，其他发行版后置；实现接口见 [架构](ARCHITECTURE.md) |
| RQ-02 | 第 1 轮：给出本机硬件、现有 Android 与底层固件自述 | 唯一设备身份及待核实项见 [platforms](platforms.md)，自述不能替代实测 |
| RQ-03 | 第 2 轮：已有个人 fork，并做过部分开发验证 | 按 [PROVENANCE](PROVENANCE.md)评估改动与原证据，不默认整体采用或继承验收 |
| RQ-04 | 第 3 轮：希望将来生成类似官方线刷包，探索 9008 | Fastboot 与 EDL 分阶段交付；包格式、刷入兼容性和恢复能力分别验证 |
| RQ-05 | 第 4 轮：从全新仓库开始，多来源移植，不倾向某个仓库 | 独立集成仓库，按功能比较来源；候选不等于构建锁，见 [PROVENANCE](PROVENANCE.md) |
| RQ-06 | 第 5 轮：将 Recovery 纳入项目 | 独立救援用户空间的设计与行为见 [RECOVERY](RECOVERY.md)，共用内核不代表共用正常 rootfs |
| RQ-07 | 第 5 轮：结合整个项目选择开发语言 | 语言职责集中在 [ARCHITECTURE](ARCHITECTURE.md)；语言选型是项目设计，不是用户指定所有底层实现 |
| RQ-08 | 2026-11-06：日志横屏放大；通过 USB 控制 Linux 诊断内核、Fastboot 与 Android；基础电源管理和充电。2026-11-07：要求 RAM 镜像直接返回 Fastboot，不经 Android ADB | USB 控制与直接 Bootloader Fastboot 往返归 WP9，当前能力及证据见 [platforms](platforms.md)；PR1 充电延期决定保留，充电须锁定实际板级参数并通过保护审查与真机专项验收 |
| RQ-11 | 2026-11-08：完全回到 `b97f1d9977caf211e5c219a0fb3431b922247e0c`，优先完成 USB 断开期间的充电信息记录，接回电脑后可导出全过程 | WP9 先交付只读日志；同次启动内持续采集、重连导出和缺失字段说明，验收不替代主动充电/温控能力 |
| RQ-12 | 2026-11-08：指定长期真机使用稳定的 LineageOS `lineage-23.2` 内核，要求新分支移植快充/PPS | WP9 采用固定提交逐功能适配；来源稳定性作为用户报告，本项目需单独完成 PD/泵/保护闭环及 L2/L3，不继承原项目验收 |
| RQ-13 | 2026-11-09：为小米 10 移植 Android 兼容完整 NFC，保留 `/dev/nq-nci` 与 NXP HAL/eSE 接口，并构建、启动镜像 | WP10 固定 NQ-NCI/ABI 与设备树适配；内核节点、Android HAL 与完整 NFC 用户体验分层验收，详见 [NFC](NFC.md) |

对话助手的具体补丁版本、短 SHA、硬件组合、目录、命令与版本路线图均属建议；未经来源审查或本项目证据，不作为锁定配置、可用命令或已完成能力。

## 后续与非目标

后续逐项评估 Recovery 安装/备份/更新与回滚、触摸 UI、GPU/Wi-Fi/桌面、其他 ARM64 rootfs、持久化与多系统切换、Fastboot 包。EDL/9008 仅在 Programmer 来源、签名/授权及兼容性成立后进入实现，不能承诺可恢复。

“类似官方线刷包”仅指可审查的发行组织形式，不表示官方签名、MiFlash 兼容、可重锁 Bootloader 或 9008 授权。保留 Android 的 MVP 不等于已经设计了双系统分区；后续替换安装和保留 Android 的安装须分别定义数据影响与验收。

不替换厂商底层安全启动固件，不宣称通用 Windows/XNU 支持或日用就绪。安全与授权边界见 [AGENTS](../AGENTS.md)。
