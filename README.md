# MiSeed

面向小米 10（umi / SM8250）的独立 UEFI、Linux 主线适配与 Recovery 平台。
按功能移植多个来源，维护板级配置、补丁和构建配方；完整上游源码及发行版软件包使用外部固定输入。

## 用户进度一览（2026-11-18）

**这台 Umi 已能从 RAM 启动 Linux、使用 USB 救援控制台并返回 Android；还不是可安装的 Debian 手机系统。** L1 是离线测试，L2 是目标构建，L3 是指定镜像的真机实测。不同镜像的结果分开记录，详见[当前状态与证据](docs/CONTEXT.md)。

| 模块 | 已实现或验证 | 还差什么 |
| --- | --- | --- |
| 工程、CI、设备与输入锁 | 有回归门禁、只读设备采集、固定内核源与工具链校验 | 板卡变体、固件及依赖闭包；具体提交的 CI 与复核 |
| Linux、USB、Recovery | Linux 6.18.60、独立 initramfs、USB CDC 控制台、Fastboot 往返 Android 已过 L3 | 产品救援入口、触摸 UI；安装、更新、备份恢复未验 |
| 第二阶段 UEFI、设备树交接 | Linux 直启已验证硬件子树和 RAM | UEFI → Linux → Recovery 未验；运行时 FDT 根身份仍 FAIL |
| Debian 13 ARM64 | 已定义最小用户空间与启动契约 | 未生成验收 rootfs，登录、系统选择与故障救援未验 |
| CSOT 显示 | 原生 DSI 点屏、亮度、90 Hz 模式；DRM 回调实测 89.846 Hz | 面板物理扫描率、SMMU 交接、休眠恢复 |
| 输入与振动 | 电源键和音量键已过 L2 | 按键事件与唤醒待 L3；触控 IC 未定，AW8697 马达待适配 |
| GPU、Wi-Fi、UFS | 设计阶段 | 加速、联网、持久存储及桌面未验 |
| 电池、电量计、有线固定 PD | FG/SMB5 充电读数、固定 PDO 协商、降至 5 V、80% 旁路有 L3 | BQ25970 ADC、温标、保护、回差与独立复核 |
| 有线 PPS、温控与日志 | 历史镜像完成 PPS 调压充电；可读热区、记录并导出 RAM 日志 | 当前镜像的泵与故障恢复专项；TCPM 电池/身份应答为 MVP 后计划 |
| 无线接收 RX、无线反充 TX | 两条路径均未移植 | 各自的驱动、保护和 NFC 共存须分别验收 |
| SN100T NFC、eSE | Type 4 探针过 L2 | 11-19 镜像通过初始化、MIFARE 寻卡和关闭。重开、NDEF、PM、HCE/eSE、Debian 待验。[NFC](docs/NFC.md) |
| 安装与发行 | Fastboot **临时启动**可用，非持久安装 | Fastboot 发行包、持久化与多系统后置；EDL/9008 仅研究，须有授权且兼容的 Programmer |

里程碑与完成条件见[工作包 WP0–WP11](docs/implementation-plan.md)，硬件证据见[本机记录](docs/platforms.md)。

## 开发入口

[产品范围](docs/PRODUCT.md) · [架构](docs/ARCHITECTURE.md) · [Recovery 契约](docs/RECOVERY.md) · [主机工具](docs/HOST_TOOLS.md) · [协作规则](docs/AI_RULES.md) · [验收要求](docs/QUALITY.md) · [来源规范](docs/PROVENANCE.md)

## 本地检查

Python 3.11+、Git，无 pip 依赖；Windows PowerShell 与 Linux 使用相同命令：

```text
python -X utf8 tools/check_repo.py
python -X utf8 -m unittest discover -s tests -v
```

这两项仅检查文本治理与离线工具（含模拟 adb 回归，不连接手机）。构建、远端门禁和真机验证的实际结果查 CONTEXT。

CI 的运行与托管设置见 [CI](docs/CI.md)，文档职责见 [GOVERNANCE](docs/GOVERNANCE.md)。
使用 AI 协作时从 [AGENTS](AGENTS.md) 进入；日常开发不要求通读 docs。

MiSeed 自有内容按 [AGPL-3.0-only](LICENSE) 许可；第三方与内核派生文件按各自 SPDX 标识和原始许可证分发。
