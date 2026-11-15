# CI 运行与接线

## 本地用法

需要 Python 3.11+、Git，无 pip 依赖：

```text
python -X utf8 tools/check_repo.py
python -X utf8 -m unittest discover -s tests -v
git diff --check
```

提交检查：`python tools/check_commits.py --base <完整SHA> --head <完整SHA>`。
workflow 修改后用 actionlint 检查。验证要求由 [QUALITY](QUALITY.md) 定义，命令结果写入归档；PR6 源码获取修复见[验证记录](archive/reviews/2026-11-10-kernel-validation-environment.md)。

## 自动化覆盖与盲区

- [check_repo.py](../tools/check_repo.py)：检查文本/预算/链接、ADR、优先级、来源与 Boot L2 锁；拦截产物/密钥、大文件及符号链接。源码分级、退出码与覆盖边界见 [QUALITY](QUALITY.md#源码篇幅门禁)。
- Git 下扫描已跟踪和未忽略新文件；无 Git 时排除约定的本地目录。不能代替凭据扫描、许可审查、锚点/reference-style 链接与语义去重。
- [check_commits.py](../tools/check_commits.py)：Git 原生 trailer 解析；只检查 PR base..head 的非 merge 新提交。自动检查中文标题与署名分类；正文语言、真实归属和 squash 消息需人工核对，规则见 [AI_RULES](AI_RULES.md)。
- 校验/回归不联网、不访问手机；runner 和 setup-python 的工具获取仍依赖网络。产品构建覆盖缺口见 [TD-001](TECH_DEBT.md)。
- macOS 本地产品 L2 通过 Colima/VZ 使用 native arm64 容器（Intel 使用 amd64）；命令和局限见[主机工具](HOST_TOOLS.md)。GitHub hosted L2 仍在 Ubuntu 24.04 执行。

## GitHub 接线

[ci.yml](../.github/workflows/ci.yml) 在所有 PR、main/master push 与手动触发时运行，无路径过滤。PR 测合并测试树，提交检查使用事件 base/head SHA；默认分支改名时同步 push 配置。

| required check 名称 | 执行范围 |
| --- | --- |
| Boot L2 (Ubuntu 24.04) | 固定 Linux v6.18.60、CSOT DT/ABL、只读 rescue 与 v2 头，校验 boot.img、定向 DT/schema 和四项 DT 反例；传递包/runner 闭包未完 |
| Repository (ubuntu-24.04, Python 3.11) | L0/L1，最低 Python |
| Repository (ubuntu-24.04, Python 3.14) | L0/L1，新版 Python |
| Repository (windows-2025, Python 3.14) | L0/L1，Windows 路径 |
| Repository (macos-26, Python 3.14) | L0/L1，macOS / Apple Silicon |
| PR commits | PR 新提交与 diff 空白 |

安全约束：仅 contents: read，checkout 不留凭据，action 固定完整 SHA，有限超时/同 PR 取消旧任务；使用 pull_request，不用 pull_request_target，PR 文本不拼入 shell。PR 只用托管临时 runner，不使用 secrets、手机或可写持久 self-hosted runner；不发布、不刷写。

维护者决定将分支保护延期到项目跑通且仓库公开后再配置。届时先在真实 PR 核对检查名，再设置默认分支规则：强制 PR、六项检查、至少一项独立审查、更新后重审，禁止 force push/删除/绕过。workflow 不能自行启用规则；结果须记录 PR HEAD SHA、运行链接和最终状态，不能以本地检查代替远端结果。

MiSeed Linux/CSOT Boot L2 固定内核和 Android v2 头模板，重建 kernel、ramdisk、DTB 与 cmdline。CI 校验 rescue ELF、GENI console、overlay 符号及 FG/SMB5/PPS 内建对象、PPS/Type-C 链接符号；Umi sink-only PPS test profile 的 Type-C/I2C15/VBUS 节点启用；验证隔离的 soc/SPMI/TLMM overlay 入口、UART/I2C15/BQ IRQ GPIO allowlist 及两条 Type-C 电源 phandle。L2 环境强制运行 libfdt 叠加正反例，避免普通 runner 缺少 dtc 时跳过全部相关测试。设备真机行为按当前 L3 记录，不由 L2 推断。apt 传递包哈希与 runner 镜像 digest 未闭合；上传产物须审查分发条件并设短保留期。

L2 另核对固定 PD PHY/TCPM 原始文件哈希，应用接收修正后记录输出哈希、检查 PD PHY 对象/ISR；见[接收修正](archive/research/2026-11-09-pd-receive-guard.md)。
内核获取以 kernel.org 为首选；连接失败或超时后回退到 stable 维护者镜像，核对锁定 tag object 与 commit。缓存 origin 仅允许已登记的两个来源；构建输入及镜像身份由来源锁校验。
