# 架构与接口

## 启动路径

```text
厂商 BootROM → XBL → ABL（保留）
                     ├─ 诊断：Linux + DTB + 救援 initramfs
                     └─ 产品：BootShim / 第二阶段 UEFI
                                  → arm64 EFI Stub Linux
                                  ├─ Recovery initramfs → 救援环境
                                  └─ OS initramfs → Debian 等用户空间
```

直启用于隔离硬件与 UEFI 交接故障，不能替代产品路径验收。设计理由见 [ADR-0002](ADR/0002-independent-integration.md)。

## 模块接口

以下是目标布局，交付对应实现时才创建目录。

| 模块 | 输入 → 输出 |
| --- | --- |
| device/xiaomi/umi/ | 本机资料 → 板级/变体配置、内存保留与启动契约 |
| firmware/uefi/、firmware/bootshim/ | 固定 UEFI 源与板级配置 → payload、符号、映射 |
| kernel/ | 固定 Linux、补丁、config、DTS → Image、DTB、modules、最终配置 |
| recovery/ | 共享内核对应的工具/模块/必要固件 → 独立救援 cpio；行为契约见 [RECOVERY](RECOVERY.md) |
| rootfs/ 的 OS initramfs | 同一内核对应的启动配方 → 正常用户空间入口；与救援 cpio 分离 |
| rootfs/ | 包快照、清单、配置 → 用户空间树/镜像；无通用默认凭据 |
| packaging/ | 产物与设备契约 → RAM/Fastboot/条件式 EDL 包；打包器不连接设备 |
| manifests/ | 候选及经审查输入 → 来源身份和构建锁 |
| tools/、tests/ | 仓库或合成输入 → 离线检查与回归 |
| .work/、out/ | 下载源/中间物 → 本地工作区/产物，Git 忽略 |

## 跨层交接

- Linux 使用 AArch64 EFI Stub，启用 CONFIG_EFI / CONFIG_EFI_STUB；不能混用 ABL 直启压缩镜像规则。
- 通过 EFI 配置表传 DTB；initramfs、命令行与载荷范围不得重叠或进入固件保留区。
- 核查 memory map、ExitBootServices、异常级别、缓存/MMU、运行时区域及 framebuffer 交接；继承 framebuffer 不代表独立显示初始化。
- Linux 仍负责外设驱动。具体地址、boot header 和 DTB/DTBO 组合只写入 [设备契约](platforms.md)。
- OS 与 Recovery 复用同一内核/DTB 构建产物，分别加载外部 initramfs；模块匹配内核配置及 ABI，不为 Recovery 复制驱动树。

接口依据：[AArch64 启动规范](https://docs.kernel.org/arch/arm64/booting.html)、[EFI Stub](https://docs.kernel.org/admin-guide/efi-stub.html)；按选定源码树版本复核。

## 构建接口

构建器只消费经审查的固定输入；下载缓存与输出隔离，缺少身份/保护配置则失败，来源锁字段归 [PROVENANCE](PROVENANCE.md)。内部调用 EDK II/Mu、Kbuild 与发行版原生工具，构建、打包和设备执行各自分离。
产物清单关联 MiSeed 修订、设备变体、内核/DTB/模块配套关系、固件要求、输入锁/配置/工具链摘要、逐文件 SHA-256、用途与证据级别；Fastboot/EDL 不复用未经核实的布局。可复现性须由独立构建比较证明。

## 语言与工具分工

| 层 | 选型与边界 |
| --- | --- |
| UEFI / BootShim | C，必要的 AArch64 汇编；沿用 EDK II/Mu 原生构建描述 |
| Linux 板级支持 | C、DTS/Kconfig/Kbuild；复用上游驱动，初期不引入 Rust 内核依赖 |
| Recovery 核心 | Rust 用户空间；包解析、校验和后续安装状态机，内存安全不替代写入授权 |
| 早期启动 | 小型 POSIX Shell + 经审查工具；复杂镜像/分区逻辑归 Recovery 核心 |
| 宿主工具 | Python 3.11+ 标准库优先，调用原生构建器；不把 Python 放入手机早期启动闭包 |

以上为 RQ-07 的项目设计选择。Windows 支持当前离线工具；产品交叉构建使用待固定的 Linux 环境，原生 Windows/WSL 的 L2 支持须分别验证。具体编译器与 Rust 版本随来源锁审查，不凭方案填版本。
