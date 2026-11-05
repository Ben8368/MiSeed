# WP1 输入调查与本地验证

日期：2026-11-05。基线 Commit：`69caa92e21892c58fdc800321428513c0d79c9d3`。分支：`work/wp1-input-lock`。宿主 Windows 10.0.26200 x64，PowerShell 7.6.5，Python 3.14.2，Git 2.53.0.windows.2。修改均为文档；未改工具/工作流、未导入上游代码。通过忽略目录中的 Google Android SDK Platform Tools 37.0.1 完成一次只读设备采集，无设备状态改变操作。

## 工作与远端证据

- 隐私审查发现内部对话 URL 和个人主目录只出现在历史调研/审查文档；已改为普通需求背景说明与 `<USER_HOME>` / `<TEMP_DIR>` 占位符，保留原环境、命令结构、退出码和结果。凭据常见格式扫描未命中；没有重新记录敏感值。
- GitHub Actions 对上述 HEAD：push run `41985238351`、workflow_dispatch run `41985050729` 均 completed/success。Ubuntu 24.04 Python 3.11、Ubuntu 24.04 Python 3.14、Windows 2025 Python 3.14 三个 Repository job 均 success；PR commits 因非 PR 事件 skipped。不是 PR 检查结果；远端运行基于原始 HEAD，不包含本轮未提交文档改动。
- 当前 GitHub 连接读取 `/branches/main/protection` 与 `/rulesets` 返回 HTTP 403。Require PR、Required checks、Force Push、bypass 均记 `UNVERIFIED`；未修改远端设置。建议要求 PR 和相关 CI 成功、禁止 Force Push、禁止未经授权绕过。
- 已将候选设备事实标注为 USER_REPORTED / DEVICE_OBSERVED / UPSTREAM_VERIFIED / UNVERIFIED。`adb devices -l` 在用户确认 RSA 授权后显示一台已授权设备；未记录设备标识。`python -X utf8 tools/collect_device.py --serial <已明确选择的设备> --output local/device-identity-20261105.json` 退出码 1 / PARTIAL。脱敏观察值：device `umi`、model `Mi 10`、platform `kona`、Android release `16`、Lineage `23.2-20261101-NIGHTLY-umi`、fingerprint 含 Android `13` 字段、Verified Boot `orange`，by-name 清单有 122 个链接名且无 `_a`/`_b` 后缀；属性缺失和此结果的限制见设备调查表。原 JSON 保留于忽略的 `local/`，没有进入 Git。未刷写、解锁、重启、读原厂分区或改供电。
- 候选源码 SHA 是默认分支的调查快照；执行过 GitHub API 树/文本查询并检查真实 DTS、EDK II DSC/C 源码和 panel driver。未克隆、构建或导入。所有 manifest `revision` 仍为 null，不形成正式源码锁。
- 历史 bootstrap 17 项、recovery-foundation 27 项、first-commit 27/28 项及 development-basis L0 结果分别属于历史快照；原文未改测试数量/命令/结果，只清理隐私路径和内部对话链接。本轮结果另列。

## 本轮本地验证

初次执行（当时 CONTEXT 指向尚未创建的本报告，因此 L0 及测试中的仓库检查均失败）：

| 命令 | 退出码 | 结果 |
| --- | ---: | --- |
| `python -X utf8 tools/check_repo.py` | 1 | 报告链接目标尚未创建。 |
| `python -X utf8 -m unittest discover -s tests -v` | 1 | 运行 28 项，1 项失败；测试夹具检测到同一个缺失报告目标。 |
| `git diff --check` | 0 | 无空白错误。 |

完成本报告并修复目标链接后，按同一基线重新运行：

| 命令 | 退出码 | 实际结果 |
| --- | ---: | --- |
| `python -X utf8 tools/check_repo.py` | 0 | `PASS: repository text, links, budgets, ADRs and source candidates (L0 only)`。 |
| `python -X utf8 -m unittest discover -s tests -v` | 0 | Ran 28 tests；OK。 |
| `git diff --check` | 0 | 无空白错误。 |

设备资料更新后的最终复验（文档改动后，Commit SHA 仍为 `69caa92e21892c58fdc800321428513c0d79c9d3`；Windows 10.0.26200 x64 / PowerShell 7.6.5 / Python 3.14.2）：

| 命令 | 退出码 | 实际结果 |
| --- | ---: | --- |
| `python -X utf8 tools/check_repo.py` | 0 | L0 PASS；文档、链接、预算、ADR 与候选清单检查通过。 |
| `python -X utf8 -m unittest discover -s tests -v` | 0 | Ran 28 tests；OK。 |
| `git diff --check` | 0 | 无空白错误。 |

ADB 采集单独记录：Google Platform Tools 37.0.1；用户授权后单设备状态为 `device`。上述既有采集命令退出码 1 / PARTIAL。当前复验不是 GitHub Actions 结果；远端运行仍对应未修改的基线 Commit。

全仓库文本扫描（排除 `.git`、local、缓存与构建输出）未发现内部对话 URL、Windows/Unix 用户主目录、常见私钥头或 GitHub/AWS 令牌格式；扫描输出仅统计命中数，没有回显匹配内容。来源矩阵的 13 个 revision 均为 40 位 SHA。

## 未覆盖

本机仅 Python 3.14.2 Windows 环境；Python 3.11、Linux 当前有相同 HEAD 的远端 L0/L1 证据，不是本机运行结果。无产品交叉编译、QEMU 或 rootfs 构建（L2）；无真机启动/显示/触控/恢复验证（L3）。设备型号/硬件变体、底层固件与槽位待维护者提供脱敏资料；许可证待维护者选择；官方 ROM、数据恢复与可行返回 Android 的路径尚未实演。WP1 保持 PARTIAL，RISK-001/002/003/005/006 不关闭。

## 维护者后续指示与复核（2026-11-05）

- 维护者授权按强 copyleft 方向定许可，并要求由开发 Agent 固定源码和工具链。已采用 AGPL-3.0-only 作为 MiSeed 原创内容默认许可，添加根 `LICENSE`、ADR-0005 和现有 Python 工具/测试的 SPDX 标识。AGPLv3 增加远程网络交互时的源码提供义务。Linux GPL-2.0-only 派生代码及第三方组件明确排除在根许可证覆盖范围之外；逐文件许可与设备镜像安装信息审查仍未关闭。
- 已建立 `manifests/sources.lock.json`：Linux 6.18.60 LTS 精确到 commit/tag-object，EDK II 及 umi/Mu 参考仓库精确到 commit。锁的是源码身份；DTS/驱动补丁应用、Mu manifest 子模块、文件级许可闭包仍未确定。
- 已建立 `manifests/toolchains.lock.json`：选择 LLVM 23.1.5、Debian AArch64 GCC 14.2、Python 3.11.18/EDK Python 3.13.5、NASM 2.16.03、ACPICA 20250404、Rust 1.98.0、QEMU 10.2.4 与 Debian trixie snapshot。Debian apt 传递包哈希、容器 digest、LLVM/Python/Rust/QEMU 实际归档哈希、Cargo/rootfs 清单未闭合；没有生成可复现构建环境，也没有执行 L2。
- 采集结果中还发现本机 by-name 名称包含 boot/recovery/dtbo/vbmeta/super/userdata、abl/xbl/aop/tz/hyp/devcfg，以及 persist、modemst1/2、fsg/fsc、mdm1m9kefs 系列。仅保留名称汇总；不记录分区目标、内容或设备标识。已在 RECOVERY 契约中明确哪些资料需加密保管，以及不得通用回刷的分区类别。
- 最终复验环境：Windows 10.0.26200 x64、PowerShell 7.6.5、Python 3.14.2；仍基于 commit `69caa92e21892c58fdc800321428513c0d79c9d3`，当前文档/许可改动未提交。`python -X utf8 tools/check_repo.py` 退出码 0；`python -X utf8 -m unittest discover -s tests -v` 退出码 0，28 项；`git diff --check` 退出码 0。三个锁/候选 JSON 可解析，sources.lock 7 条提交均为 40 位 SHA，sources.json 的 11 个条目保持 `revision: null`/`license_review: pending`。
- 以上是本地 L0/L1 结果，不是新 GitHub Actions 运行。之前远端 job 仅对应未修改的基线 commit。无 L2、L3；备份/恢复未执行。

新增来源依据：Linux 6.18.60 [kernel.org stable release](https://www.kernel.org/releases.html)；LLVM 23.1.5 [LLVM releases](https://releases.llvm.org/)；Rust 1.98.0 [Rust release](https://blog.rust-lang.org/2026/08/20/Rust-1.98.0/)；QEMU 10.2.4 [official releases](https://www.qemu.org/download/)。许可证兼容边界依据 [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html.en)。

许可证细化：维护者“最严格开源许可证”的授权按 AGPL-3.0-only 执行，因 AGPLv3 在分发 copyleft 外增加远程网络用户源码获取义务；官方说明见 [GNU AGPL](https://www.gnu.org/licenses/agpl-3.0.en.html)。根 `LICENSE` SHA-256：`0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0`。

许可证改为 AGPL 后最终复验：`python -X utf8 tools/check_repo.py` 退出码 0；`python -X utf8 -m unittest discover -s tests -v` 退出码 0，28 项；`git diff --check` 退出码 0。之后仅补充本文件记录和 AOSP Recovery 文档引用；没有新远端 Actions 运行。

再次维护者限定（本轮后续）：无需个人数据备份；禁止对 `modemst1/2`、`fsg`、`fsc`、`persist*`、`mdm1m9kefs*` 及 `mdm1m9kefsc` 做任何分区内容读取、备份、散列、写入、擦除或恢复，IMEI/Wi-Fi MAC/SN 保持不变；`misc` 同样不触碰。主机 `.work/` ROM 包只计算文件级 SHA-256/MD5，与公开第三方目录摘要相符，但签名/来源和区域兼容未验证。未解包或运行 ROM/MiFlash，未访问设备分区节点。准确版本/摘要与禁区记录见研究文档；L0/L1 将在本次文档更新后重跑。

最终按维护者追加约束：个人数据无需备份；不读取或更改保护分区，也不访问设备分区节点。`.work/` 文件审查只证明文件摘要及其与第三方目录记录相符；中国区 ROM 与设备 fingerprint 显示的 Global 渠道不一致，官方签名、实机 SKU 匹配和回退能力均未确认。`boot.img` 仅作为未来主机审查候选。本轮没有解包、执行 MiFlash/ROM 脚本、Fastboot 命令或任何设备状态操作。更新后的 L0/L1 结果见下方最终复验记录。

### 维护者纠正与包内只读审查（2026-11-05）

维护者说明以上两段对包来源和区域差异的结论错误：`.work/` 中包为维护者准备的小米官方包（USER_REPORTED），目标是国行 umi；刷 Lineage 前设备运行官方 1.0.5，官方最新 Fastboot 包为 1.0.3。Lineage fingerprint 的 `MIXM` 不用于否定实物销售区域或包来源。旧文中“来源未验证/不能认证官方身份/区域不匹配”的判断撤回；文件摘要仅作为完整性记录。

随后只读检查 Recovery ZIP/TGZ 目录及脚本文本，未运行任何工具/脚本、未访问设备分区节点。Fastboot `flash_all_except_storage` 仍写 `misc` 和多项系统/固件分区；`flash_all` 还擦除 `metadata` 并写 `userdata`；`flash_all_lock` 还会锁定 Bootloader。因此不能按原样运行这些 Fastboot 脚本。Recovery ZIP 可读 `updater-script` 列出的目标包含 `boot`、ABL/XBL、modem、DTBO、VBMeta 与动态系统分区，没有列出 `misc`、userdata、metadata 或维护者禁止的身份/NV 分区；`update-binary` 未执行，Recovery 使用入口仍未验证。Recovery 1.0.7 ZIP 是首选官方恢复候选；其 `boot.img` SHA-256 为 `71acc2eb62b74d47da4d7ea856b21075073d8f767c6e74ae5780446a2b6fef6d`。Fastboot 1.0.3 `boot.img` SHA-256 为 `e81aae005fe1a768ab721bb9b248c665c974bfb8ec66b6911558b3fd1e5cf768`，作版本对照。没有执行刷写或恢复。

纠正与包脚本审查后的本地复验：Windows 10.0.26200 x64 / PowerShell 7.6.5 / Python 3.14.2；Commit `69caa92e21892c58fdc800321428513c0d79c9d3`，未提交工作树。`python -X utf8 tools/check_repo.py` 退出码 0（L0 PASS）；`python -X utf8 -m unittest discover -s tests -v` 退出码 0（28 项通过）；`git diff --check` 退出码 0。一次中间复验因 `platforms.md` 超出字符预算而失败，压缩文档后以上三项重新执行并全部通过。以上为本地结果，不是当前工作树的 GitHub Actions。

### 维护者放宽 misc 与 Bootloader 锁定禁令（2026-11-05）

维护者报告曾使用官方工具线刷，`misc` 随官方流程更新后未发现 MAC、SN、基带等身份信息丢失；据此允许 `misc` 随官方恢复流程更新，不再作为阻断。此许可不包含独立替换/手动写入 `misc`。维护者明确严禁重锁 Bootloader，原因是锁定后无法再次刷机且可能无法救砖；`flash_all_lock`、`flash_all_lock.bat` 及任何 lock 选项永久排除在恢复方案之外。此前“misc 不触碰”作为当时指示保留在历史记录中，以本条作为当前规则。

### 采用上游 CSOT 实机证据并推进 L2（2026-11-05）

前述“其他来源线索不能替代本机确认”没有充分区分证据层级，曾把上游已完成的实机验证错误地当成无效。现按来源身份和验证范围采纳 misaka-link/umi-linux `cf83073887899f485a5c606f9842c5e691ea796c` 的设备测试报告：其 README 报告 Xiaomi Mi 10 `umi`、SM8250、CSOT、ABL header v2 成功启动 kernel/initrd/DTB；CSOT DTS 的实际代码关闭原生 panel 驱动并依赖 bootloader simple-framebuffer。`boot-build/build-boot.sh` 检查 `dtc -@`、`__symbols__`、152 个 `/abl_ovf` anchors、DTB 与 kernel/initrd 不变性。我们将这些作为 MiSeed 的 headless boot contract 和上游实机证据，而不是 MiSeed L3。Touchscreen 与原生 panel 功能不在该验证范围。精确来源 SHA 已加入 sources.lock，标注 tested reference only；没有导入源码。

已在 WSL Ubuntu 24.04 / dtc 1.7.0 的隔离临时目录执行该上游自带的主机端 boot repack 脚本。基底官方发布 boot 的 SHA-256 为 `a7e4d341e35a663683bf41a8fe99d472bc6b8568aa1b54b05901dc7e9f2d842a`；DTS 编译产出的 DTB SHA-256 为 `b0e48ef96880567160182be4d31dc5721fa33ce3b3551909388b8e50612a99c2`，model 为 `Xiaomi Mi 10 (CSOT)`、anchors 为 152；重打包 boot 与基底 SHA 完全相同，kernel/initrd 字节不变。退出码 0。该结果复现的是上游主机自检，不是 MiSeed 自有启动镜像，也未接触设备。

回退证据同样校正：维护者报告此前官方 MiFlash 线刷已成功，且 `misc` 更新后身份信息正常。这是已存在的官方流程实机经验，作为 USER_REPORTED fallback evidence 采纳，不要求为本任务重复刷机。Fastboot 包脚本静态审查可补充操作边界：`flash_all_except_storage` 含 `misc` 且不命中禁访身份/NV 分区；`flash_all` 擦除 metadata/写 userdata；任何 `flash_all_lock` 或锁定选项绝对禁止。所有后续回退必须保持 BL 解锁。尚未覆盖的是 MiSeed 新载荷自身失败时的专项 L3，而非官方包是否值得信任。

Linux stable v6.18.60 精确源码 commit `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的 AArch64 `defconfig` Image 构建也成功。环境 WSL2 Ubuntu 24.04 x86_64、`aarch64-linux-gnu-gcc 13.3.0-6ubuntu2~24.04.1`、GNU make 4.3；该编译器不同于锁定候选 Debian GCC 14.2。先以 `make ... -j4 Image` 启动，因耗时主动中断并用相同输出目录 `-j12` 续建；最终完整命令为 `make -C /tmp/umseed-linux-v6.18.60 O=/tmp/umseed-linux-build-v6.18.60 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j12 Image`，退出码 0。产物 `Image` 为 49,744,384 bytes，SHA-256 `e7dd5ea295a79f68917b3ca42824e0546f767aabe237884e438fc6b88dfafbd0`；`.config` SHA-256 `b1d1e8b7e29babadca89288dad0355fd68e65ed0548d90a01c1baf57ce564627`。重新链接导致 Image 时间戳相关哈希变化，因此此结果仅证明泛化 arm64 构建链可工作，不声称可复现；不是 MiSeed/umi 配置或锁定候选工具链构建，也未触及设备。

### 最终工作树验证（2026-11-05）

源码仍以 HEAD `69caa92e21892c58fdc800321428513c0d79c9d3` 为基线；以下验证包含全部未提交改动。Windows 10.0.26200 x64 / PowerShell 7.6.5 / Python 3.14.2：`python -X utf8 tools/check_repo.py` 退出码 0（L0 PASS）；`python -X utf8 -m unittest discover -s tests -v` 退出码 0，28 项通过；`git diff --check` 退出码 0。Linux WSL2 Ubuntu 24.04 x86_64 / Python 3.12.3 执行相同三条命令，分别退出码 0、0（28 项通过）、0。Python 3.11 本机不可用；GitHub 上已有 Python 3.11 成功结果只对应基线 HEAD，不含当前未提交改动，故当前改动的 3.11 与 GitHub Actions 状态待后续 PR 验证。首次复验曾因 Recovery 文档超 3500 字符失败，压缩后上述最终复验均通过。

仓库含 macOS 26/Python 3.14 CI job；`go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12` 退出码 0。macOS job 尚无远端运行结果，须由此 PR 触发验证。源码锁 JSON 与工具链锁 JSON 可解析；`platforms.md` 为 3417 字符，`RECOVERY.md` 为 3486 字符，均未超仓库上限。当前 GitHub 连接对 branch protection/rulesets 返回 403，因此 main 规则仍 UNVERIFIED；未修改远端设置。全程无真机写操作。
