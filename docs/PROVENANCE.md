# 来源、归属与分发

候选身份与用途只维护在 [sources.json](../manifests/sources.json)，来源比较历史见 [调研归档](archive/research/2026-11-05-background.md)。候选不构成构建输入，不默认采用某个整仓 fork。

## 引入准入

每次移植记录：

1. 关联 [需求编号](PRODUCT.md)和工作包、功能/目标路径、候选比较、选择理由与本地修改；附固定 mainline 子系统/ABI/binding 对照及 Android 私有依赖去留，不以来源系统可用替代 Linux 适配审查。
2. 原仓库、完整 commit、原始文件/补丁路径、递归依赖、补丁顺序与 SHA-256。
3. 逐文件许可证、原贡献者、文件头与 NOTICE；分别审查子模块、驱动和二进制。
4. 适用变体及验证记录；外部来源报告与本项目证据分列，标明原验证的设备、固件、源码、载荷及方法是否可复核。
5. 形成带日期的功能级评估记录，归档到 docs/archive/research/；说明准入/暂缓结论、缺失证据与风险编号。记录来源不等于通过许可或验证门禁。

WP1 先维护人工审查的 `manifests/sources.lock.json` 版本锁。构建器/schema 接入后必须校验 id/url/revision/license/paths、子模块、补丁、工具链及 rootfs snapshot 的完整闭包；未通过前锁文件不可自动消费。候选 revision 为空或许可未审的来源不得自动下载执行。

当前 [check_build_lock.py](../tools/check_build_lock.py) 仅做一致性检查：核对 Boot L2 的来源身份、脚本固定值及完整列举的本地输入哈希；仓库检查和构建前均调用，异常即失败。它不从锁文件选择、下载或执行来源，不表示传递依赖或许可闭包已完成；新增构建依赖须人工审查并扩充清单。运行状态只维护在 platforms，历史镜像留在证据归档。

Boot L2 的 Linux 源仍以锁定的 kernel.org stable tag/commit 为权威身份。若 kernel.org 拉取失败，构建可改从锁定的 `gregkh/linux` 镜像读取同一 annotated tag；tag object 与 peeled commit 都须逐字匹配 `sources.lock.json` 后才继续，镜像只作为传输回退，不改变源码身份。

## 既有 fork 与验证记录

RQ-03 的既有成果是优先核对的线索，不指定默认基线。先固定 fork 与对应上游的完整 commit，区分继承代码、个人修改及依赖；不能用领先提交数代替功能清单，也不能用对话中的短 SHA 或移动分支名锁定输入。
原始验证可复核时保留其来源和适用范围；无法对应本次源码/配置/固件/设备的记录只能列为来源报告。移植后的 L2/L3 按 [QUALITY](QUALITY.md)重新取得，不把旧项目验收复制为 MiSeed 已通过。

## 固件与分发

闭源固件另记来源、版本、SHA-256、获取方式及再分发条件；下载可用和哈希匹配不能替代许可。导入时保留原始归属，不用项目许可证覆盖第三方许可。

自有内容按 [ADR-0005](ADR/0005-project-license.md) 采用 AGPL-3.0-only，许可证文本见根目录 `LICENSE`；Linux GPL-2.0-only 派生文件和第三方组件维持自身许可。工具参与署名归 [AI_RULES](AI_RULES.md)，Git 禁入内容归 [AGENTS](../AGENTS.md)。

WP1 的仓库 SHA、源码路径、文件级 SPDX 观察与采用建议见[调查矩阵](archive/research/2026-11-05-wp1-input-review.md)。固定源码身份见 `manifests/sources.lock.json`；版本候选见 `manifests/toolchains.lock.json`。这些记录锁定提交/版本，不代表补丁闭包已构建或通过 L2。Linux GPL-2.0-only 内核派生驱动不能被顶层 AGPL-3.0-only 覆盖；EDK II/Mu、DTS、Recovery 包和固件依各自许可证与再分发条件。逐文件许可审计和镜像分发条件仍是发布门禁。
