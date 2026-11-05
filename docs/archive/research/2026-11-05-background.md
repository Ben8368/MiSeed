> 历史快照：仅用于需求/来源追溯，不是当前产品规范或能力证据。

# 项目背景与需求依据

本轮初始化依据是用户指定的小米 10 调研对话及本轮请求。于 2026-11-05 读取全部四个用户回合；旧示例中的“具体机型未选定”等产品内容作废，不作为历史硬件证据。协作与安全规则继续适用。

## 用户明确表达的需求

- 产品名 MiSeed；从全新独立仓库开始，从多个相关仓库按功能移植，不偏向单一仓库。
- 首要实现 UEFI 与 Linux，内核要求 6.18 或以上；长期希望设备可以运行多种系统，Linux 是 MVP。
- 用户空间考虑 Debian，也愿意评估更新较快的发行版。
- 希望后续可构建类似小米官方形式的 Fastboot 线刷包，并研究高通 9008/EDL 包。
- 用户报告曾 fork `Ben8368/umi-mainline` 并做过部分开发验证；没有向本仓库提供可复核的 commit、测试记录或镜像哈希。

设备自述集中记录在 [platforms](../../platforms.md)，避免在各文档重复硬件状态。

## 本次采用的设计方向

采用外部源码锁定、模块补丁、Linux 独立诊断路径、第二阶段 UEFI、救援环境、Debian 用户空间配方的分层设计。首个目标是建立可复核启动链并保留现有 Android；详细边界见 [PRODUCT](../../PRODUCT.md) 和 [ARCHITECTURE](../../ARCHITECTURE.md)。

对话中助手提到的具体内核补丁号、已验证功能、地址、分区路径与命令都是调研线索。未经来源核对及本项目验证，不进入设备配置、构建锁或能力声明。Fedora、其他操作系统与 EDL 均不因出现在建议中而自动进入 MVP。

## 对话之外的核查

本轮只阅读候选仓库公开说明及官方接口文档，未执行其构建/刷机脚本，也未导入源码。外部声明和链接见 [PROVENANCE](../../PROVENANCE.md)；候选列表见 [sources.json](../../../manifests/sources.json)。候选列表不是可构建的锁文件。

## 候选来源

2026-11-05 阅读下列公开仓库说明；这是来源自述的调研快照，没有复现，也没有导入源码。完整机器可读候选 URL 只维护在 [sources.json](../../../manifests/sources.json)。

| 来源 | 调研价值 | 不能由此推定 |
| --- | --- | --- |
| [colorgarden/umi-mainline](https://github.com/colorgarden/umi-mainline) | 显示、ST FTS、DTS、GPU 与 Wi-Fi 的板级适配线索 | 华星屏必配 ST；本机外设已经可用 |
| [Ben8368/umi-mainline](https://github.com/Ben8368/umi-mainline) | 用户既有 fork，优先核对其改动与验证记录 | 用户旧验证等同于本仓库验收 |
| [misaka-link/umi-linux](https://github.com/misaka-link/umi-linux) | Debian、USB gadget、GPU/视频与 SM8250 集成配方 | 可直接复用安装流程；该来源说明屏幕/触控未实现，并存在存储与供电策略差异 |
| [reAAAq/uefi-umi](https://github.com/reAAAq/uefi-umi) | Mu-Silicium、继承显示、按键与 RAM payload | 已验证 Linux EFI Stub 或完整独立显示初始化 |
| [reAAAq/UniXNU-umi](https://github.com/reAAAq/UniXNU-umi) | 启动交接、UFS/SMMU 与诊断线索 | XNU 实现可直接移植 Linux；多系统已可用 |
| [LonelyFool/EDK2-SM8250](https://github.com/LonelyFool/EDK2-SM8250) | 历史 EDK2/SM8250 实现供比较 | 可替代当前板级验证或维护承诺 |

Linux 基线取自 [kernel.org](https://www.kernel.org/)，UEFI 通用基线评估 [EDK II](https://github.com/tianocore/edk2) 与候选工程的实际 Mu 依赖；Debian 输入取自[官方来源](https://www.debian.org/releases/trixie/)。不把任何候选默认指定为整仓基础。
