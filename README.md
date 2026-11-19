# MiSeed：小米 10 的主线 Linux 与独立 UEFI 平台

让小米 10（代号 `umi` / 骁龙 865）变身真正的 Linux 便携设备。本项目提供独立的第二阶段 UEFI 固件、Linux 主线内核适配与 Debian 发行版支持，告别 Android 驱动依赖，走向真正的纯主线 Linux 生态。

## 核心亮点与当前体验

- **原生高刷点屏**：CSOT 面板原生 DSI 驱动点亮，支持 1080×2340 分辨率与 90Hz 高刷新率，界面滚动流畅。
- **安全供电与充电**：支持标准 USB PD/PPS 快充与温控降流；具备 80% 旁路供电，插电当微型主机不伤电池。
- **近场通信**：SN100T NFC 芯片 Linux 驱动已跑通初始化、实体 MIFARE 寻卡与关闭重开。
- **零风险体验**：开发测试采用纯内存加载（`fastboot boot`），不触碰手机存储，完全保留原 Android 与数据。
- **主线标准接口**：告别私有 HAL 与 Binder，屏幕 (DRM)、供电 (power_supply)、NFC (NCI) 均遵循主线标准。

## 快速导航

| 我想了解 | 推荐入口 | 说明 |
| --- | --- | --- |
| **最新进展** | [当前状态](docs/CONTEXT.md) | 查看已闭环功能、最新真机实测记录与近期优先级 |
| **硬件支持** | [设备与运行契约](docs/platforms.md) | 屏幕、供电、按键、NFC 等实测能力与状态一览 |
| **技术架构** | [架构与启动路径](docs/ARCHITECTURE.md) | ABL → UEFI → Linux → Debian 启动链路与设计划分 |
| **动手编译与调试** | [主机工具指南](docs/HOST_TOOLS.md) | macOS / Linux / WSL 镜像编译、ADB 与 Fastboot 使用 |
| **产品目标** | [产品范围与需求](docs/PRODUCT.md) | MVP 定义、功能边界与需求追溯 |
| **救援与安全** | [Recovery 契约](docs/RECOVERY.md) | 独立救援环境、无自动写盘与防变砖安全守则 |
| **参与开发** | [实施计划](docs/implementation-plan.md) / [协作规则](docs/AI_RULES.md) | 工作包依赖、红黄绿灯治理与提交规范 |

## 本地极速自检

无需安装任何第三方 pip 库，标准 Python 3.11+ 与 Git 即可运行完整离线校验：

```text
python -X utf8 tools/check_repo.py
python -X utf8 -m unittest discover -s tests -v
```

更多 CI 与门禁说明查阅 [CI 配置](docs/CI.md)，文档职责分工见 [文档治理](docs/GOVERNANCE.md)。
AI 协作开发请参考 [AGENTS](AGENTS.md)。代码遵循 [AGPL-3.0-only](LICENSE) 许可，第三方与内核派生文件依各自许可证。
