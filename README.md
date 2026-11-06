# MiSeed

面向小米 10（umi / SM8250）的独立 UEFI、Linux 主线适配与 Recovery 平台。
按功能移植多个来源，维护板级配置、补丁和构建配方；完整上游源码及发行版软件包使用外部固定输入。

## 开发者从这里开始

1. [产品范围与需求追溯](docs/PRODUCT.md)：区分用户要求、项目设计和待验证事项。
2. [当前状态](docs/CONTEXT.md)：确认已做、未做和近期优先级。
3. [实施计划](docs/implementation-plan.md)：确认里程碑、工作包和开工输入；接口与语言分工查 [架构](docs/ARCHITECTURE.md)，救援行为查 [Recovery 契约](docs/RECOVERY.md)，本机核实与只读采集查 [设备契约](docs/platforms.md)。
4. 修改前查 [协作规则](docs/AI_RULES.md) 和 [验收要求](docs/QUALITY.md)；引入外部实现时查 [来源规范](docs/PROVENANCE.md)。
5. macOS L2 构建、ADB/Fastboot RAM 验证步骤查 [主机工具](docs/HOST_TOOLS.md)。

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
