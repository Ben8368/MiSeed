# 执行与协作规则

## 执行顺序

定位任务事实源 → 确认环境和分支 → 修改 → Audit → 按 [QUALITY](QUALITY.md) 验证 → 更新权威记录 → 汇报。保留无关本地修改，不扩大用户排除的范围。

- Windows：用 `$PSVersionTable`、`[System.Runtime.InteropServices.RuntimeInformation]::OSDescription`、`OSArchitecture` / `ProcessArchitecture` 确认 OS、Shell 和架构。
- Unix：用 `uname -s`、`uname -m` 并确认 Shell；WSL 标记 Windows + WSL。不要假设宿主具备 Linux 构建工具或目标 arm64 环境。
- 先检查 Git 状态；位于主分支则切工作分支。没有 .git 时如实报告，不伪造分支；不为模拟手机验证而运行手机 init、刷写或充电操作。

## 真机验证准备

通知用户接入设备、进入 Fastboot/ADB 或换线前，必须备齐所需镜像/固件（如适用）、哈希与配置核验、采集工具和脚本，完成适用离线验证，并明确操作步骤、预期、停止条件与回退方案。接入后只进行必要的设备身份、连接和授权核对及预定验证，不让用户等待构建、下载、安装工具或编写采集脚本。若发现准备缺项或需要重建，先保全现场并安全结束当前操作，告知用户可断开；材料就绪后再通知接入。

## 移植前接口审查

按 [Linux 接口准则](ARCHITECTURE.md#linux-接口准则)，先列“功能 → mainline 子系统/驱动模型 → ABI/binding → 标准用户态消费者 → 本地差异”，核对固定源码后再实施。涉及供电须分别标明有线、无线 RX、无线 TX；下游来源仅作为硬件线索，Android 服务不得默认进入 Linux 运行闭包。标准能力缺失时，实施前记录复用候选、未覆盖原因、通用化扩展方案及对应上游设计/维护要求的依据和评审路径。临时例外在 TECH_DEBT 指定责任工作包与退出条件，未知硬件能力登记 RISK_REGISTER。审查须检查现有例外是否扩大，并按 QUALITY 附接口验证证据。

## 🚦 Audit Report

源码修改后、完整验证前报告：总体评价、各维度灯色与处置、阻断项、跟进项、验证计划，按 [红黄绿灯治理](QUALITY.md#红黄绿灯治理) 标注证据与关闭条件。检查范围、事实真实性、错误路径、数据/供电/凭据安全及源码篇幅；分级、记录归属与阻断规则只在 QUALITY 维护。

## Git 与署名

分支/PR 和默认授权边界见 [AGENTS](../AGENTS.md)。授权提交或推送不等于授权合并；不重写已推送历史、不改全局 Git 配置。一个请求默认交付一个面向主分支的 PR。

提交使用 Conventional Commit：类型前缀英文，标题和正文中文。主作者保留维护者身份；工具实质参与时，末尾空行后追加对应 trailer，不记录模型：

| 工具 | trailer |
| --- | --- |
| Codex | `Co-authored-by: Codex <codex@openai.com>` |
| Claude Code | `Co-authored-by: Claude Code <noreply@anthropic.com>` |
| Cursor | `Co-authored-by: Cursor <cursoragent@cursor.com>` |
| Jules | `Co-authored-by: Google Labs Jules <161369871+google-labs-jules[bot]@users.noreply.github.com>` |

机器人使用固定 `Bot-authored-by:`；纯人工使用 `Human-authored: true`，不得与 AI/机器人标记混用。多段正文使用文件或独立 `-m`，不用字面量 `\n`。提交后以 `git show -s --format=%B HEAD | git interpret-trailers --parse` 核对；推送前运行 `tools/check_commits.py --base <PR base SHA> --head <HEAD SHA>`，失败即停止推送；只检查 PR 新提交，不回溯历史。

## PR 与合并

- 推送前完成适用自查/专项复核；授权推送后报告 PR、HEAD SHA 与 CI 当前状态，不必等待 GitHub CI 结束即可完成对话；未结束或未查询须明确标记，不写为通过。
- 创建或更新 PR 时按 [红黄绿灯治理](QUALITY.md#红黄绿灯治理) 评估规模、启动收尾并复核文件硬上限；该节统一维护门槛及收尾例外，本节仅维护审查与合并流程。
- CodeRabbit 等自动审查意见仅作参考，不作为权威结论或单独的合并门槛。逐项对照当前 HEAD、源码、可复现检查和适用的真机证据核实；确认的问题修复，未采纳的意见记录核实依据。自动审查无反馈或未给最终结论本身不阻断合并。
- CodeRabbit 是异步参考：按需触发后无需等待审核完成即可结束对话，报告当时 HEAD 与状态；queued、in_progress、skipped 或无结论不阻断对话。具体意见按上一条核实；CodeRabbit 不替代当前 HEAD CI 或开发者以外的人工专项复核。
- 合并须用户明确授权，并确认约定 CI 成功对应当前已核验的精确 PR HEAD SHA；合并时绑定该 SHA，若 HEAD 改变则重走适用门禁。还须确认当前 base 与 PR HEAD 的合并结果已验证；base 改变后重验，或由已核实的分支保护要求分支保持最新。仓库规则要求的人工审查仍须满足。squash 消息另按署名规则核对。
- queued、in_progress、cancelled、skipped 或失败的约定 CI 均不算通过；自动审查状态不得替代 CI、独立复核或真机证据。
- 不自动合并或发布。PR 复选框只记录任务完成，不代表验证通过或操作授权。
