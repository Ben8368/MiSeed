# CI 运行与接线

## 本地用法

需要 Python 3.11+、Git，无 pip 依赖：

```text
python -X utf8 tools/check_repo.py
python -X utf8 -m unittest discover -s tests -v
git diff --check
```

提交检查：`python tools/check_commits.py --base <完整SHA> --head <完整SHA>`。
workflow 修改后用 actionlint 检查。验证要求由 [QUALITY](QUALITY.md) 定义，命令结果写入归档。

## 自动化覆盖与盲区

- [check_repo.py](../tools/check_repo.py)：必需文件、编码/空白、活动文档预算、内联相对文件链接、ADR 索引/状态、三项优先级、来源候选 schema；拦截被扫描到的产物/密钥、超大文件、符号链接。
- Git 下扫描已跟踪和未忽略新文件；无 Git 时排除约定的本地目录。不能代替凭据扫描、许可审查、锚点/reference-style 链接与语义去重。
- [check_commits.py](../tools/check_commits.py)：Git 原生 trailer 解析；只检查 PR base..head 的非 merge 新提交。自动检查中文标题与署名分类；正文语言、真实归属和 squash 消息需人工核对，规则见 [AI_RULES](AI_RULES.md)。
- 校验/回归不联网、不访问手机；runner 和 setup-python 的工具获取仍依赖网络。产品构建覆盖缺口见 [TD-001](TECH_DEBT.md)。

## GitHub 接线

[ci.yml](../.github/workflows/ci.yml) 在所有 PR、main/master push 与手动触发时运行，无路径过滤。PR 测合并测试树，提交检查使用事件 base/head SHA；默认分支改名时同步 push 配置。

| required check 名称 | 执行范围 |
| --- | --- |
| Repository (ubuntu-24.04, Python 3.11) | L0/L1，最低 Python |
| Repository (ubuntu-24.04, Python 3.14) | L0/L1，新版 Python |
| Repository (windows-2025, Python 3.14) | L0/L1，Windows 路径 |
| Repository (macos-26, Python 3.14) | L0/L1，macOS / Apple Silicon |
| PR commits | PR 新提交与 diff 空白 |

安全约束：仅 contents: read，checkout 不留凭据，action 固定完整 SHA，有限超时/同 PR 取消旧任务；使用 pull_request，不用 pull_request_target，PR 文本不拼入 shell。PR 只用托管临时 runner，不使用 secrets、手机或可写持久 self-hosted runner；不发布、不刷写。

维护者先在真实 PR 核对检查名，再设默认分支规则：强制 PR、五项检查、独立审查、更新后重审，禁止 force push/删除/绕过。workflow 不能自行启用规则；结果须记录 PR HEAD SHA、运行链接和最终状态，不能以本地检查代替远端结果。

2026-11-05 HEAD `69caa92e21892c58fdc800321428513c0d79c9d3` 的 [push run](https://github.com/Ben8368/UmiSeed/actions/runs/41985238351) 与 [workflow_dispatch run](https://github.com/Ben8368/UmiSeed/actions/runs/41985050729) 均 completed/success；Ubuntu 24.04 / Python 3.11、Ubuntu 24.04 / Python 3.14、Windows 2025 / Python 3.14 三个 Repository job 均 success；PR commits 因非 PR 事件 skipped。Branch protection/rulesets API 对当前连接返回 403，Require PR、Required checks、Force Push 与 bypass 均 `UNVERIFIED`。推荐要求 PR、所有相关 CI 成功、禁止 Force Push、禁止未经授权绕过；未修改远端设置。

源码工作包引入时同步接入固定输入 L2 构建与产物清单；Linux/UEFI/rootfs 分别构建，打包增加错误输入回归。上传产物前审查分发条件并设短保留期；L3 使用独立授权的设备流程。
