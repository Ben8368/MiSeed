# 执行与协作规则

## 执行顺序

定位任务事实源 → 确认环境和分支 → 修改 → Audit → 按 [QUALITY](QUALITY.md) 验证 → 更新权威记录 → 汇报。保留无关本地修改，不扩大用户排除的范围。

- Windows：用 `$PSVersionTable`、`[System.Runtime.InteropServices.RuntimeInformation]::OSDescription`、`OSArchitecture` / `ProcessArchitecture` 确认 OS、Shell 和架构。
- Unix：用 `uname -s`、`uname -m` 并确认 Shell；WSL 标记 Windows + WSL。不要假设宿主具备 Linux 构建工具或目标 arm64 环境。
- 先检查 Git 状态；位于主分支则切工作分支。没有 .git 时如实报告，不伪造分支；不为模拟手机验证而运行手机 init、刷写或充电操作。

## 🚦 Audit Report

源码修改后、完整验证前报告：总体评价、🔴 阻断项、🟡 跟进项、验证计划。检查范围、事实真实性、错误路径、数据/供电/凭据安全。🔴 不得合并；🟡 登记风险或技术债，登记不解除阻断。

## Git 与署名

分支/PR 和默认授权边界见 [AGENTS](../AGENTS.md)。授权提交或推送不等于授权合并；不重写已推送历史、不改全局 Git 配置。一个请求默认交付一个面向主分支的 PR。

提交使用 Conventional Commit：类型前缀英文，标题和正文中文。主作者保留维护者身份；工具实质参与时，末尾空行后追加对应 trailer，不记录模型：

| 工具 | trailer |
| --- | --- |
| Codex | `Co-authored-by: Codex <codex@openai.com>` |
| Claude Code | `Co-authored-by: Claude Code <noreply@anthropic.com>` |
| Cursor | `Co-authored-by: Cursor <cursoragent@cursor.com>` |
| Jules | `Co-authored-by: Google Labs Jules <161369871+google-labs-jules[bot]@users.noreply.github.com>` |

机器人使用固定 `Bot-authored-by:`；纯人工使用 `Human-authored: true`，不得与 AI/机器人标记混用。多段正文使用文件或独立 `-m`，不用字面量 `\n`。提交后以 `git show -s --format=%B HEAD | git interpret-trailers --parse` 核对；只检查新提交，不回溯历史。

## PR 与合并

- 推送前完成适用自查/专项复核；授权推送后报告 PR、HEAD SHA 与 CI 当前状态，不必等待 GitHub CI 结束即可完成对话；未结束或未查询须明确标记，不写为通过。
- 创建或更新 PR 前查看 GitHub 的 `additions`（保守按全部新增行计数），结合本地预估控制规模，更新后复核；单个 PR 不得超过 7000 行，预计超限先拆分范围。超过 5000 行即建议收尾、停止扩展范围并评估合并准备；合并仍须满足明确授权、CI 和审查门禁。
- CodeRabbit 等自动审查意见仅作参考，不作为权威结论或单独的合并门槛。逐项对照当前 HEAD、源码、可复现检查和适用的真机证据核实；确认的问题修复，未采纳的意见记录核实依据。自动审查无反馈或未给最终结论本身不阻断合并。
- 合并须用户明确授权，并确认当前 HEAD 的约定 CI 为 success；仓库规则要求的人工审查仍须满足。新提交后重走适用门禁。squash 消息另按署名规则核对。
- queued、in_progress、cancelled、skipped 或失败的约定 CI 均不算通过；自动审查状态不得替代 CI、独立复核或真机证据。
- 不自动合并或发布。PR 复选框只记录任务完成，不代表验证通过或操作授权。
