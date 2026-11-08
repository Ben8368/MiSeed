# PR #1 CodeRabbit 意见核实与修复

日期：2026-11-06。核实对象为 [PR #1](https://github.com/Ben8368/MiSeed/pull/1)，review `5801072352`；7 条行内意见和 1 条补充建议。PR HEAD 为 `8f67d5f26dff8c68ab53acd0bab3f8de81b5248f`；远端查询确认 PR 已合并，主线仍为 `8d73348b3a29398b1804d0ceebe96fb6f4065fce`。以下是针对该源码及本轮工作区的核查，不把机器人结论当作验收依据。

## 逐条判定

| 意见 | 核查依据与结论 | 本轮处理 |
| --- | --- | --- |
| [kernel 大小限制说明](https://github.com/Ben8368/MiSeed/pull/1#discussion_r4493177976) | 准确。打包器仅限制完整镜像为 128 MiB，没有 kernel/ramdisk 地址间距检查。合成 16,744,449-byte kernel 打包成功，输出 16,760,832 bytes | 更正历史调研中的错误描述；保留 ABL/load map/解压布局未知及不能视为安全启动目标的警告，不凭地址间距新增未经硬件证实的限制 |
| [设备序列号](https://github.com/Ben8368/MiSeed/pull/1#discussion_r4493178035) | 准确。当前验证记录有 4 处同一设备标识；Git 全历史内容检索定位到 `8f67d5f` 引入该标识 | 四处统一改为 `<DEVICE_SERIAL>`。当前跟踪内容检索不再命中原值；历史仍保留，见 RISK-009 |
| [USB 探测日志](https://github.com/Ben8368/MiSeed/pull/1#discussion_r4493178054) | 准确。原过滤器漏掉仅含 DWC3/USB/PHY/UDC/pending 的信息级日志；旧版合成输入的 5 类消息全部被丢弃 | 增加大小写不敏感匹配，保留原过滤条件及有界环形拷贝逻辑 |
| [UDC 延迟注册](https://github.com/Ben8368/MiSeed/pull/1#discussion_r4493178073) | 准确。原 main 只扫描一次；空目录返回后 PID 1 永久 sleep。是否由本机 RPMh 造成延迟尚无新增 L3 证据 | configfs 配置与 UDC 绑定分离；子进程每秒重扫并重试绑定，成功后启动控制台。不反复重建 gadget，不阻塞 PID 1 的主循环 |
| [串口挂断后恢复](https://github.com/Ben8368/MiSeed/pull/1#discussion_r4493178084) | 准确。固定 Linux 的 `u_serial.c:gserial_disconnect()` 调用 `tty_hangup()`；原程序不替换已挂断描述符，也不清除 stdio 错误。旧版在伪终端重连反例中收不到新提示符 | 关闭旧标准描述符、重新 open ttyGS0、dup2 恢复三路 stdio、清除流状态；stdin 不缓冲。连续两次挂断后仍可处理命令的 L1 测试通过 |
| [macOS 架构缓存隔离](https://github.com/Ben8368/MiSeed/pull/1#discussion_r4493178101) | 准确。原输出目录分架构，但同一 volume 的 `/build/build-<revision>` 仍共享 host executables | volume 名称加入 `arm64`/`amd64`，隔离整套源码及 Kbuild 工作区；mock 两种架构确认卷和挂载不同 |
| [源码缓存身份](https://github.com/Ben8368/MiSeed/pull/1#discussion_r4493178130) | 准确。原标记文件无法检测 included DTS/C 等输入修改 | 新增 `verify_kernel_source.py` 对固定 Git tree 的全部 blob、文件类型、执行位和额外输入核查；只允许项目覆盖的两份 DTS、修订标记，以及精确的两条 Kbuild 规则。失败则从锁定 commit 导出干净源码并使用新构建目录，保留原缓存 |
| macOS Dockerfile 相对路径（review 补充建议） | 准确。脚本虽算出绝对 repo_root，原 `--file tools/macos-l2.Dockerfile` 仍相对调用目录 | `--file` 改为基于 repo_root 的绝对路径；从仓库外调用的 mock 回归通过 |

缓存核查以固定提交的 Git blob OID 为依据，没有用另一个自报标记替代来源校验。回归覆盖了 marker 正确但 DTS 被改动、缺失文件、额外被忽略的 `.o`、异常 Makefile 修改、执行位和覆盖文件符号链接；还执行真实脚本的源码准备段，确认干净缓存复用、脏缓存切换新源码/构建目录且不删除原目录。验证只在隔离 fixture 内运行。

## 未作为修改依据的提示

- CodeRabbit 的 80% docstring coverage 阈值不属于 [QUALITY](../../QUALITY.md) 或当前 required checks；不据此给未改逻辑批量加注释，也不以此声明 CI 失败。新增核查工具已写明职责。
- 机器人对 PR 描述缺少来源/许可、Audit、独立复核的提醒属于说明与证据检查，不能单独证明源码错误。远端 PR 描述确实较简略；来源身份仍以 `manifests/sources.lock.json`、许可规则以 [PROVENANCE](../../PROVENANCE.md) 为准，本记录补充本轮 Audit/验证/边界，不冒充独立复核已通过。未修改已合并 PR 的描述、评论或线程状态。
- Linked Issues / Out of Scope 标为 Passed 的原因是无关联 issue 而跳过，不能解读为需求范围已获得审查。

## 🚦 Audit Report

总体：改动限于已核实的 USB 诊断逻辑、主机构建缓存和记录纠错；工作分支 `codex/fastboot-charging-governance`。开始时 `AI_RULES`、`CONTEXT`、`GOVERNANCE`、`implementation-plan` 已有本地修改，均保留；本轮只在 CONTEXT 追加最新证据入口。

- 🔴 本轮修改没有远端同 HEAD CI、开发者以外专项复核或设备 L3。合并就绪性尚未成立；本轮不提交、不推送、不合并。
- 🟡 USB 延迟/重连只通过合成回归与 L2；真实线缆重插、supplier 永不就绪、实际 PON/SPMI 等行为仍属 RISK-008 的后续验证，不能因重试循环存在就声称硬件已恢复。
- 🟡 已推送历史中的设备标识副本未清除，登记 RISK-009，责任归维护者。没有改写已推送历史。
- 未修改 DTS、电压、电流、温控、充电保护或存储命令；测试只调用诊断函数，不执行手机 main、挂载、重启、真实块设备写入或充电。

## 验证结果

宿主 Windows 10.0.26200 x64 / PowerShell 7.6.9 / Python 3.14.2；L1/L2 使用 Windows + WSL2 Ubuntu 24.04 x86_64、Linux 6.6.87.2-microsoft-standard-WSL2、Python 3.12.3、aarch64-linux-gnu-gcc 13.3.0、GNU make 4.3。

| 范围 | 准确命令（仓库根目录，Linux 命令在 WSL 内） | 结果 |
| --- | --- | --- |
| L0 | `python -X utf8 tools/check_repo.py`；`git diff --check` | 均退出 0；包含文档链接、UTF-8/LF、预算检查 |
| Windows L1 | `python -X utf8 -m unittest discover -s tests -q` | 45 项：35 通过、10 项 Linux/POSIX 专属跳过，退出 0 |
| Linux L1 | `python3 -X utf8 -m unittest discover -s tests -q` | 45 项全部通过，无跳过，退出 0 |
| Shell | `shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` | 退出 0 |
| Bash 语法 | `bash -n tools/build_kernel_l2.sh`；`bash -n tools/build_kernel_l2_macos.sh` | 均退出 0 |
| L2 | `MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/pr1-review-l2 MISEED_JOBS=8 bash /mnt/c/MiSeed/tools/build_kernel_l2.sh` | 退出 0；固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的缓存逐项核查通过，Image/DTB、静态 rescue-init、initramfs 与 boot.img 构建/回读通过；152 个 ABL anchors、241 个 DTBO targets 校验通过 |

Windows 最初一次回归受受限临时目录权限阻止，使用正常临时目录权限重跑；随后发现新测试夹具追加 Makefile 时产生 CRLF，已让夹具明确写 LF，再取得上表最终结果。没有放宽产品源码的内容核查。macOS 仅运行 Darwin/arm64/amd64 命令模拟，未在本机声称 Colima 实际构建通过。

对照复现使用 `HEAD:kernel/rescue_init.c` 的临时副本，确认旧过滤器漏筛和旧控制台断线故障；临时合成 boot header 确认打包器接受超地址间距 kernel。这些文件、模拟终端和产物均位于临时目录或忽略的 `.work/`。源码 grep 最初受 Windows→WSL 参数引号影响返回 1；改用独立 `grep -e` 参数后退出 0，确认 `u_serial.c` 的真实 hangup 调用；不把这次参数错误当作内核错误。

L2 新镜像位于忽略目录 `.work/pr1-review-l2/boot.img`，大小 7,131,136 bytes：kernel 6,687,232、ramdisk 262,523、DTB 171,768 bytes，Android header v2 / page 4096。原 `.work/boot-l2/boot.img` 仍为既有 `33b986f64a57b4ee8d1c5528e657c3bc38e85f0ac40faf5700dd4ad9f76a3d6c`，未用新候选覆盖。

| 输入/产物 | SHA-256 |
| --- | --- |
| `kernel/rescue_init.c` | `6dac2371c4bb3316946a91fdc2b6bac2130afece8fff44efe3302b39c22a565e` |
| `tools/build_kernel_l2.sh` | `daab2141a9f030d0b0e2dc162e8042304ab8197615dce655bb62e58af35f0c91` |
| `tools/build_kernel_l2_macos.sh` | `95da127888562480f70361b8bdb85c76d1119ecaaf5e5e3a0ea1f09880a60aed` |
| `tools/verify_kernel_source.py` | `c5a86f3030bdd6d304f87ffc9789801a5ed2f1ae807996a3100f3daf9eba61b3` |
| `Image` | `45b4e25540d5c877869eca06c8372acea995abdb91bb8ba6259bffd696dfe9d1` |
| `umi-csot.dtb` | `32331bad24d382f7f3be99b8ed330b0a84a1ec4132bf1a5843c180e3408993e3` |
| `rescue-initramfs.cpio.gz` | `a8dd4b8a3d2648ddd52ad078b71e9765a9a44fb46c27db3779a259055d984ed1` |
| `boot.img` | `d5da122c7668f7122303c2ee469af418a0206f9021213e2b4f59ab0fdc9e11f2` |

L2 不证明设备可启动或 USB 断线恢复已达 L3；本轮没有操作手机。
