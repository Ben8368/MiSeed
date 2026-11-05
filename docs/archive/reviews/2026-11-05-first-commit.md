# 首提交前验证

日期：2026-11-05。用户本轮明确授权完成工作区第一份提交，指定远端 `https://github.com/Ben8368/UmiSeed`。宿主 Windows 10.0.26200，OS/进程 X64，PowerShell Core 7.6.5；沿用已有维护者 Git 身份，不修改全局配置。

## Git 准备与范围

- 初查工作区没有 .git。`git ls-remote --symref https://github.com/Ben8368/UmiSeed HEAD` 与 `git ls-remote https://github.com/Ben8368/UmiSeed` 均退出 0、无输出，查询时远端没有 refs。
- `git init --initial-branch=chore/bootstrap-foundation` 与 `git remote add origin https://github.com/Ben8368/UmiSeed.git` 均退出 0；在工作分支准备根提交，不在 main/master 提交，不创建远端基础分支。
- 提交内容是工作区已有文档、来源候选、离线工具/回归与 GitHub Actions 定义；仅为本轮同步 CONTEXT 和 RISK-006 并添加此记录，不导入产品源码。
- 源码纳入提交前已输出 🚦 Audit Report；现有风险不关闭，CI/治理的独立专项复核仍是合并前要求。只有本地提交授权，不执行推送、发布或合并。

## 本地验证

以下在 `C:\UmiSeed` 运行：

| 命令 | 退出码 | 实际结果 |
| --- | ---: | --- |
| `python -X utf8 tools/check_repo.py` | 0 | L0 文本、编码/LF、链接、预算、ADR、来源候选通过 |
| `python -X utf8 -m unittest discover -s tests -v` | 0 | L1 完整 27 项通过，Git 变更均限于临时夹具仓库；不连接手机 |
| `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 -color .github/workflows/ci.yml` | 0 | workflow 静态检查无诊断；工具获取可能使用网络 |
| `git diff --cached --check` | 0 | 初次暂存树的空白检查通过 |
| `git diff --check` | 0 | 工作区空白检查通过 |

`git add --all -- .` 退出 0；`git diff --cached --stat` 与 `git status --short --branch` 核对暂存共 42 个文件，仅包含预期文档、源码、配置与测试，未纳入 local/、缓存、镜像、私钥或构建产物。常见凭据格式的文件名扫描无命中（rg 退出 1、无输出），此检查不替代完整凭据审计：

```text
rg -l --hidden -g '!.git/**' -g '!**/__pycache__/**' -g '!local/**' -g '!.work/**' -g '!out/**' -e 'BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY' -e 'gh[pousr]_[A-Za-z0-9]{30,}' -e 'github_pat_[A-Za-z0-9_]{30,}' -e 'AKIA[0-9A-Z]{16}' .
```

首提交没有 parent，不构造假的 base SHA 运行 PR 范围检查。提交消息文件为 `.git/FIRST_COMMIT_MSG`（不进入版本库），已运行下列准确命令，退出 0，原生解析结果只有预期的 Codex trailer，现有 message_errors 规则通过：

```text
python -X utf8 -c "from pathlib import Path; from tools.check_commits import git,message_errors; root=Path.cwd(); msg=(root/'.git/FIRST_COMMIT_MSG').read_text(encoding='utf-8'); parsed=git(root,'interpret-trailers','--parse',input_text=msg); errors=message_errors(msg,parsed); print(parsed,end=''); print('PASS: commit message' if not errors else errors); raise SystemExit(bool(errors))"
```

## 未覆盖

仅覆盖指定宿主的 L0/L1，远端 CI、分支保护、审查机器人与外部专项复核未运行；无 L2 编译或 L3 真机证据。自有许可、第三方许可审查、源码锁、设备契约与回退仍按现有风险管理，不能由首次 Git 提交推定可编译、可刷或可分发。

## 初始化验收覆盖

2026-11-05 在同一 Windows X64 / PowerShell 7.6.5 / Python 3.14.2 环境运行 `python -X utf8 tools/check_repo.py`，退出 0；`python -X utf8 -m unittest discover -s tests -v`，退出 0，完整 28 项通过；`git diff --check` 无空白诊断。
链接回归同时覆盖 URL 编码文件名、等价目录路径、缺失目标与实际存在的仓库外文件；比较前统一解析根目录和文档路径，越界检测保持启用。远端矩阵结果必须关联对应提交和 Actions 运行记录，不以本地通过代替远端验收。
