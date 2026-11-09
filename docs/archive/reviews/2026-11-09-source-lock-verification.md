# 源码锁评估核实与 PR #3 收尾

## 范围与依据

2026-11-09；Windows 10.0.26200 / x64 / PowerShell 7.6.9，Python 3.14；另用 WSL Ubuntu 24.04 / x86_64 / Bash / Python 3.12.3 验证。
维护者授权完善已核实问题、提交推送至 PR #3、处理草稿状态并触发 CodeRabbit；不等待自动审查，不合并或发布。本轮不操作手机。

引用评估停留于 `49b1f406dc6333160e0efa69b49384e90bad60f3`；本轮修复基线为 `b304fa7f9f7d7df4147870c734cb002226acbe3c`。该基线远端 CI [run 57](https://github.com/Ben8368/MiSeed/actions/runs/42590738828) 已 completed/success，不能移用为修复后 HEAD 的 CI。

## 已核实问题与修复

- 内核 URL/完整 SHA、boot 模板 URL/SHA 在脚本与锁中一致；但旧 profile 的 21 个本地输入哈希有 11 个不匹配，且漏列 PPS/PD 新增输入。旧评估所用提交也有相同数量的哈希不匹配。
- 补齐并重新核对当前 Boot L2 本地输入清单、哈希及主机导出工具哈希。新增离线检查同时核对来源身份、脚本固定值、执行中的参数和哈希；仓库检查与构建前调用，漂移即失败，且在下载/创建输出前拒绝。
- 锁仍为人工审查记录；检查器不动态选择或下载来源。最低已知输入集合防止同时删掉清单和哈希来掩盖遗漏，新增依赖仍须人工复核。传递依赖/工具链闭包继续归 TD-001。
- 移除锁文件里过期的快充禁用状态、旧运行验收标志与历史镜像产物副本；历史仍保留在原始归档，当前能力归 platforms。修正工具链记录的旧验证状态，以及 NOTICE/PPS 的旧节点状态和日志容量描述。
- 最新 PD/PPS 的运行成就及各自镜像证据保留；保护专项、旧故障根因、串口导出和发布门禁继续分开跟踪，见 [RISK-004](../../RISK_REGISTER.md#risk-004-p0-供电发布)。

## PR 处理

PR #3 已有可供正式审查的 PD/PPS 实现、L2 和本机稳定运行证据，本轮转为待审查，停止扩展功能范围。待审查只表示请求评审；既有 P0 和独立专项/精确 HEAD CI 门禁继续阻断合并。
PR 正文按最终实现重写，列出固定 PD 与 PPS 的真实进展、当前提交、验证来源和开放项；触发一次 `@coderabbitai full review`，不等待结果，不将机器人审核当作人工专项复核。

## 本轮验证

| 环境 / 命令 | 结果 |
| --- | --- |
| Windows：`python -X utf8 tools/check_repo.py`、`git diff --check` | 各退出 0；文本、链接、预算及 31 项本地构建输入和 1 项主机工具哈希匹配 |
| Windows：将 `.work/tools/w64devkit-2.11.0/w64devkit/bin` 加入本进程 PATH 后，`python -X utf8 -m unittest discover -s tests -v` | 129 项，退出 0；41 项因 POSIX/Linux/设备树工具条件跳过 |
| WSL：`python3 -m unittest discover -s tests -v` | 129 项，退出 0、零跳过；最终经独立 Bash 脚本记录退出码，34.479 秒 |
| WSL：`shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` | 退出 0 |
| WSL：下述完整 Boot L2 | 退出 0；5 项 libfdt 叠加回归全部执行通过 |

```sh
MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/source-pd-rx-20261109 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/build-pd-rx-20261109 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/source-lock-review/l2 bash tools/build_kernel_l2.sh
```

新增 11 项回归覆盖正常清单、源文件/主机工具变动、同时删除输入和哈希、脚本与执行参数漂移、错误 schema/重复身份/缺失来源、非法 JSON/缺文件、路径逃逸/目录/符号链接/FIFO，以及构建在下载或创建输出前拒绝不匹配输入。已有缓存复用/失败恢复用例改用完整隔离的匹配锁记录，没有关闭前置校验。

L2 的旧缓存因 `drivers/pmdomain/qcom/Kconfig` 与固定 Git 树不同而被拒绝，构建器保留旧缓存并用全新源码/构建目录完成构建。工具链为 GCC 13.3.0、make 4.3。生成 `boot.img` 7,811,072 bytes，SHA-256 `44854093bb63ddc45b60ee8c9940fb9114815cb7829fd16ee02150e98a89f94c`，与已验收的 PD/PPS 镜像完全一致；本轮无新真机操作。

原始输出、退出码及产物保存在忽略目录 `.work/source-lock-review/`，不随 PR 分发。本轮自查不替代开发者以外的专项复核；修复后的精确 PR HEAD CI 在推送后另行查询，CodeRabbit 只触发、不等待。
