# MiSeed 新远端导入验证

日期：2026-11-05。维护者指定 `https://github.com/Ben8368/MiSeed` 并授权推送。宿主 Windows 10.0.26200 X64 / PowerShell Core 7.6.5 / Python 3.14.2；沿用维护者 Git 身份，不改全局配置。

## 范围与边界

- 本地 `C:\MiSeed` 初查没有 `.git`；`git ls-remote https://github.com/Ben8368/MiSeed.git` 退出 0，无 refs。GitHub 元数据确认目标为空的私有仓库。
- 初始化 `work/initial-import` 工作分支并设置 origin；导入当前文件快照，不伪造或恢复历史提交。归档内旧 SHA、UmiSeed 链接和验证仅保留为历史记录。
- 除同步 CONTEXT 和新增本报告外，保留已有文件内容。提交含文档、许可证、来源/工具链清单、离线工具、测试及 CI 定义；未导入上游源码、固件、镜像、原始日志或凭据。
- 推送前已输出 🚦 Audit Report。未修改 CI/设备工具；没有访问手机或运行刷写、恢复、供电操作。
- 远端没有 `main` 基础分支，本次仅推送工作分支，不创建/推送/合并主分支。PR 须待基础分支建立后补建；合并前专项复核要求继续适用。

## 本地验证

以下在 `C:\MiSeed`、上述 Windows 环境执行：

| 命令 | 退出码 | 结果 |
| --- | --- | --- |
| `python -X utf8 tools/check_repo.py` | 0 | L0 PASS；编码/LF、链接、预算、ADR、来源候选检查通过。 |
| `python -X utf8 -m unittest discover -s tests -v` | 0 | L1，28 项通过；隔离夹具，不连接手机。 |
| `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 -color .github/workflows/ci.yml` | 0 | 无诊断；仅静态检查既有 workflow。 |
| `git diff --cached --check` | 0 | 首次暂存树无空白错误。 |
| `git diff --check` | 0 | 工作区无空白错误。 |

三个 manifests JSON 均可解析。常见私钥、GitHub/AWS 凭据及 password/api-key/secret 赋值模式扫描无匹配（rg 退出 1 表示无匹配），人工核对暂存文件清单；不声称穷尽凭据检测。根提交没有 parent，不伪造 PR base 运行范围检查；提交消息按规则验证中文 Conventional Commit 标题和 Codex trailer。

## 未覆盖

当前导入提交的远端 CI、分支保护、Linux/macOS/Python 3.11 结果未核验。workflow 只对 main/master push、PR 和手动事件运行，工作分支首次推送不会自动触发。没有新增 MiSeed 板级 L2 或 L3 证据，既有阶段与风险不关闭。
