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
- macOS 本地产品 L2 通过 Colima/VZ 使用 native arm64 容器（Intel 使用 amd64）；命令和局限见[主机工具](HOST_TOOLS.md)。GitHub hosted L2 仍在 Ubuntu 24.04 执行。

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

MiSeed Linux/CSOT Boot L2 固定内核和 Android v2 头模板，重建 kernel、ramdisk、DTB 与 cmdline。CI 校验 rescue ELF、GENI console、overlay 符号及 FG/SMB5/PPS 内建对象、PPS/Type-C 链接符号；PPS/Type-C/I2C15/VBUS 节点保持 disabled；验证隔离的 soc/SPMI/TLMM overlay 入口、UART-only GPIO 掩码及两条 Type-C 电源 phandle。L2 环境强制运行 libfdt 叠加正反例，避免普通 runner 缺少 dtc 时跳过全部相关测试。初版全量 defconfig 超时后改为 allnoconfig 加板级配置，随后 hosted CI 成功；详情见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。参考资产下载遇瞬时失败时有限重试/切换同资产 API，始终核对原 SHA-256，不安装部分缓存。L2 不证明运行时行为；保护和其他变体仍需适用 L3。apt 传递包哈希与 runner 镜像 digest 未闭合；上传产物须审查分发条件并设短保留期。

PR #1 commit `e5186ecb442a72eecaefb5c2c54311e193ba9508` 的 [CI run 4](https://github.com/Ben8368/MiSeed/actions/runs/42046511356) 于 2026-11-05 completed/success，Boot L2、PR commits、Ubuntu 3.11、Ubuntu 3.14、Windows 2025 和 macOS 26 六项 job 全部 success。Hosted Boot L2 编译约 4 分 47 秒；152 个 ABL overlay symbols 检查通过，hosted `boot.img` 为 2,453,504 bytes，SHA-256 `e00640c502647b8e675442198028db426090563943eeb0bd8a520bdbbe1d620c`，与本机 L2 完全一致。该证据闭合首个 PR HEAD 的真实 CI 和独立 hosted 复建；apt 传递包哈希、runner image digest 仍待闭合。CI 通过不代表设备已启动，分支保护仍按维护者决定延期。
