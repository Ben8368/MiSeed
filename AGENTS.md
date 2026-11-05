# AI 入口

每轮先读 [CONTEXT](docs/CONTEXT.md) 与 [LESSONS](docs/LESSONS.md)，仅获取阶段、优先级和适用经验。

## 按需读取

按本轮实际改动选文档；同一行中的文件分别按需选取，不是整组必读。

| 本轮问题 | 读取位置 |
| --- | --- |
| 产品范围 / 模块接口 / 本机契约 | [PRODUCT](docs/PRODUCT.md) / [ARCHITECTURE](docs/ARCHITECTURE.md) / [platforms](docs/platforms.md) |
| 实施、审查、提交 | [AI_RULES](docs/AI_RULES.md)；验证要求查 [QUALITY](docs/QUALITY.md) |
| 修改 CI / 引入外部代码或固件 | [CI](docs/CI.md) / [PROVENANCE](docs/PROVENANCE.md) |
| 任务依赖 / 未知假设 / 实现妥协 | [实施计划](docs/implementation-plan.md) / [风险](docs/RISK_REGISTER.md) / [技术债](docs/TECH_DEBT.md) |
| 文档职责、预算或长期决定 | [GOVERNANCE](docs/GOVERNANCE.md)；仅追溯决策时查 [ADR 索引](docs/ADR/README.md) |

- 先定位标题或关键词，再读相关章节；仅需全文上下文时读全文。
- 不因链接而递归读取，不默认读取 README、全部 docs、ADR、归档或长日志。
- 同轮未变内容不重复读取；换任务、文件变化或发现冲突时再加载对应内容。

## 不可跨越的边界

- 本仓库是移动设备 UEFI + Linux 主线适配层。源码存在、编译通过、真机可用必须区分。
- 刷写、格式化、分区、真实块设备写入和供电修改，必须有本轮明确授权及回退方案。
- 不猜测电压、电流、温控或采样电阻参数；保护不全时拒绝主动危险充电。
- 保留贡献者、固件来源与许可证；不提交凭据、原厂分区、镜像、长原始日志或构建产物。
- Agent 改动经工作分支和 PR 进入主分支，不直接在本地或远端主分支提交、推送或合入。默认不提交、不推送、不发布、不合并。

默认中文汇报，文件 UTF-8 / LF。实施前确认宿主 OS、架构、Shell；源码改后、完整验证前输出 🚦 Audit Report，执行细节见 AI_RULES。状态、规则和记录只更新其权威位置。
