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
| Boot L2 (Ubuntu 24.04) | 固定 Linux v6.18.60、MiSeed CSOT DTS/ABL 锚点、只读救援 initramfs 和 Android v2 头模板，构建并校验 `boot.img`；apt 传递包哈希与 runner 镜像 digest 尚未闭合 |
| Repository (ubuntu-24.04, Python 3.11) | L0/L1，最低 Python |
| Repository (ubuntu-24.04, Python 3.14) | L0/L1，新版 Python |
| Repository (windows-2025, Python 3.14) | L0/L1，Windows 路径 |
| Repository (macos-26, Python 3.14) | L0/L1，macOS / Apple Silicon |
| PR commits | PR 新提交与 diff 空白 |

安全约束：仅 contents: read，checkout 不留凭据，action 固定完整 SHA，有限超时/同 PR 取消旧任务；使用 pull_request，不用 pull_request_target，PR 文本不拼入 shell。PR 只用托管临时 runner，不使用 secrets、手机或可写持久 self-hosted runner；不发布、不刷写。

维护者决定将分支保护延期到项目跑通且仓库公开后再配置。届时先在真实 PR 核对检查名，再设置默认分支规则：强制 PR、六项检查、至少一项独立审查、更新后重审，禁止 force push/删除/绕过。workflow 不能自行启用规则；结果须记录 PR HEAD SHA、运行链接和最终状态，不能以本地检查代替远端结果。

2026-11-05 HEAD `69caa92e21892c58fdc800321428513c0d79c9d3` 的 [push run](https://github.com/Ben8368/UmiSeed/actions/runs/41985238351) 与 [workflow_dispatch run](https://github.com/Ben8368/UmiSeed/actions/runs/41985050729) 均 completed/success；Ubuntu 24.04 / Python 3.11、Ubuntu 24.04 / Python 3.14、Windows 2025 / Python 3.14 三个 Repository job 均 success；PR commits 因非 PR 事件 skipped。Branch protection/rulesets API 对当前连接返回 403，Require PR、Required checks、Force Push 与 bypass 均 `UNVERIFIED`。推荐要求 PR、所有相关 CI 成功、禁止 Force Push、禁止未经授权绕过；未修改远端设置。

MiSeed 首个 Linux/CSOT `boot.img` 构建已接入 `tools/build_kernel_l2.sh` 和 `Boot L2 (Ubuntu 24.04)` CI job；PR #1 的首个 hosted Boot L2 run（`42039446452`）因全 ARM64 `defconfig` 编译过多无关驱动、达到 30 分钟上限而取消，未发现编译错误。脚本现从 `allnoconfig` 合并板级配置，并断言救援 ELF、Qualcomm GENI console、设备树 overlay 等选项；本地新 L2 结果及哈希见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)，新的同 HEAD hosted CI 待验证。Android v2 模板只提供经过 SHA-256 校验的 header 元数据；kernel、ramdisk、DTB 和 cmdline 全部由 MiSeed 重建。apt 传递包哈希及 runner 镜像 digest 仍未闭合。L2 不等于设备可启动，L3 由维护者后续 RAM 启动测试确认。上传产物前审查分发条件并设短保留期。

PR #1 commit `e5186ecb442a72eecaefb5c2c54311e193ba9508` 的 [CI run 4](https://github.com/Ben8368/MiSeed/actions/runs/42046511356) 于 2026-11-05 completed/success，Boot L2、PR commits、Ubuntu 3.11、Ubuntu 3.14、Windows 2025 和 macOS 26 六项 job 全部 success。Hosted Boot L2 编译约 4 分 47 秒；152 个 ABL overlay symbols 检查通过，hosted `boot.img` 为 2,453,504 bytes，SHA-256 `e00640c502647b8e675442198028db426090563943eeb0bd8a520bdbbe1d620c`，与本机 L2 完全一致。该证据闭合首个 PR HEAD 的真实 CI 和独立 hosted 复建；apt 传递包哈希、runner image digest 仍待闭合。CI 通过不代表设备已启动，分支保护仍按维护者决定延期。
