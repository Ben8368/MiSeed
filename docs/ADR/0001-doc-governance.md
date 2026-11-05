# ADR 0001：建立 docs 单一事实源与 Agent 署名规范

- 状态：已接受
- 日期：2026-11-05

## 背景

MiSeed 从空仓库起步，计划并行推进移动设备的 UEFI 与 Linux 主线适配。需要在引入源码前固定文档权威位置、状态用语、技术债/风险台账，以及 Agent 提交署名与 PR 交付方式，避免状态漂移和未测冒充通过。

## 决策

- 治理与协作权威文档统一放在 `docs/`；项目说明 `README.md` 与 AI 入口 `AGENTS.md` 置于仓库根目录。
- 采用单一事实源、按需加载、证据分级（L0–L3）、CONTEXT 三项优先级预算与 ADR 追加制。
- Agent 实质改动经工作分支与 PR 进入主分支；Conventional Commit 类型前缀英文、标题正文中文；工具级 `Co-authored-by` / 人工 `Human-authored: true` 见 [AI_RULES](../AI_RULES.md)。
- 本 ADR 只固化制度，不引入具体机型、固件树或内核补丁。

## 后果

后续改阶段只更新 CONTEXT 等权威位置；长期边界变更新增 ADR。治理检查脚本与 git 钩子可在仓库骨架阶段补齐，不回溯放宽本决策。
