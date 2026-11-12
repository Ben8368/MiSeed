# 2026-11-10 内核 DT/schema 与 WSL 构建时钟复核

## 范围与输入

维护者在修复有线 SMB5 status 时要求完善 DT/schema 专用环境及 Clock skew 警告。分支 `codex/linux-standard-interfaces`，基线 HEAD `ed941eaab307149bcfe713ca90c80af4a1683eb8` 加本轮未提交改动；没有提交、推送、发布或合并。手机复测与载荷身份见[有线 ABI L3](../hardware/2026-11-10-umi-battery-fg-smb5-l3.md)。本记录负责构建验证环境；不涉及无线 RX/TX、手机供电设置或存储写入。

环境：Windows 10.0.26200 x64 / PowerShell 7.6.9；WSL Ubuntu 24.04 x86_64、6.6.87.2-microsoft-standard-WSL2；GCC 13.3.0 / GNU make 4.3。内核固定 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`。来源锁列举并哈希校验实际构建 runner、DT 检查器、专用 requirements 与 DT 反例。

## DT 专用入口与 CI

新增 `tools/validate_kernel_dt.py`：必须使用 dtschema 2025.8，核对工具和固定源码 marker，执行定向 dt-doc-validate、dt_binding_check、强制重建板级 DTB 的 CHECK_DTBS、最终打包 DTB 的严格 dt-validate，以及 `test_wired_dt.py` 四项正反例。缺依赖、缺 schema、非零退出、schema 诊断或跳过测试均失败；普通跨平台 L1 仍可跳过缺硬件构建输入的用例，CI L2 job 必须实际执行专用入口。

范围为既有有线 power_supply、电池、thermal 与 SM8250 pinctrl schema，另加 SN100T NCI 与 simple-framebuffer。不是整机全量 schema 合规声明。只失效 Kbuild 生成的单个 `umi-csot.dtb`，保留最终打包副本，不修改 DTS 内容/时间戳。

依据：[Linux binding 检查说明](https://docs.kernel.org/devicetree/bindings/writing-schema.html)要求 binding 与 DTS 分别验证。dtschema 来自[官方 PyPI 2025.8](https://pypi.org/project/dtschema/2025.8/)，Linaro/Arm BSD-2-Clause；仅作为宿主验证工具，不打包进镜像。`tools/requirements-dtschema.txt` 和 toolchains 锁固定直接版本；传递 wheel/.deb 哈希、runner digest 闭包仍归 TD-001。CI 新增已核对版本的 python3-venv/python3-dev 3.12.3-0ubuntu2.1 与 swig 4.2.0-2ubuntu1。

实际入口：

```sh
cd /mnt/c/MiSeed
/tmp/miseed-dtschema/bin/python tools/validate_kernel_dt.py \
  --source /tmp/miseed-boot-l2/linux-standard-source-20261110 \
  --build /tmp/miseed-boot-l2/linux-standard-build-20261110 \
  --dtb .work/smb5-status-l2/umi-csot.dtb
```

四项实际回归已通过且无跳过，binding、板级和最终 DTB 均无所列范围诊断。第一次入口试验使用 make -B，导致 Kconfig included-file 重建报错，退出 1；改为仅失效生成 DTB。第二次发现直接脚本入口没有将仓库根加入 Python 模块路径，DT 反例导入失败；补齐后实际入口成功。actionlint 首次发现 runner context 不允许在 job env 使用；移至 step env 后静态检查通过。这些前置失败不计为通过。

## 时钟根因与本机处置

原先 make 返回 0 却有文件时间领先 2.6–2.7 秒及 `Clock skew detected`。新增 `tools/run_build_command.py` 原样转发 stdout/stderr，固定诊断语言为 C，保留工具失败码；发现任一未来时间/clock-skew 诊断即使 make 返回 0 也返回 1。所有 L2 make 调用经此 runner，含递归 make。回归覆盖正常告警、原始失败码、两类时钟诊断，不自动重试或改写文件时间。

本机 journal 持续出现 `Time jumped backwards` 与 `Clock change detected`；timesync-status 为 NTP active、offset 约 −5.795 秒，而 `/proc/cmdline` 同时有 `hv_utils.timesync_implicit=1`。运行中的 WSL 发行版仅 Ubuntu-24.04。实际守卫拦住一次 compile.h 领先 2.7 秒的构建（退出 1），说明警告已不再被记成成功。

[Ubuntu 官方时间同步说明](https://ubuntu.com/wsl/docs/stable/explanation/time-sync/)指出 24.04 的 systemd-timesyncd 可能与 Hyper-V 隐式同步冲突，建议采用默认宿主同步时停用来宾 NTP。先以 root 临时 stop 本机服务；过渡期 40 秒采样仍包含一次约 5.10 秒时钟跳变，不作为修复完成证据。随后执行 `wsl.exe -u root -e systemctl disable --now systemd-timesyncd.service`，服务从 enabled/active 变为 disabled/inactive；保留 Hyper-V 同步、Windows 时间服务、内核/clocksource 和其他 WSL 进程。无需重启 WSL。

回退命令：`wsl.exe -u root -e systemctl enable --now systemd-timesyncd.service`。此为本机可逆配置，仓库脚本不会自动修改任何宿主服务。

后续独立 Windows Stopwatch/UTC 与 WSL 采样：Windows 调用总耗时 15.266 秒（含进程启动），来宾 MONOTONIC=15.000138 秒、RAW=15.000155 秒、REALTIME=15.000159 秒；末端来宾与随后读取的 Windows UTC 相差约 −29 毫秒。完整 L2 重建退出 0，日志未出现 Clock skew / future-time 警告；最终 boot.img 仍为 `2a5f03ec62e5278666d2a8b7867c1db8dd8705e6851ad9329e31e50aca1cb084`，与 L3 实测载荷一致。原警告日志的 make 退出 0 不作为无告警构建证据，后续成功由守卫和实际日志共同举证。

## 验证命令与审计

L2 实际命令为 `bash tools/build_kernel_l2.sh`，设置 MISEED_JOBS=12、WORK_ROOT=/tmp/miseed-boot-l2、EXTRACTED_SOURCE/BUILD_ROOT 为上文目录，OUTPUT_ROOT=/mnt/c/MiSeed/.work/smb5-status-l2（变量均带 MISEED_ 前缀）。日志、产物和采样只留忽略的 `.work/smb5-status-*`。

| 检查 | 实际结果 |
| --- | --- |
| WSL 完整 L1 | 设置下方 DT 环境后 `python3 -X utf8 -m unittest discover -s tests -v`；190 项，退出 0、零跳过 |
| Windows 完整 L1 | 临时 PATH 加 w64devkit 与修正后的本机 DTC 工具后同命令；190 项，退出 0，136 项实测、54 项平台/环境跳过；所有项另由 WSL 实跑 |
| L2 | 上述固定环境完整构建退出 0；时钟告警门禁启用，日志无 future/clock-skew 警告；与 L3 同哈希 |
| DT 专用 | 本文命令退出 0；binding/板级/最终 DTB 定向验证及四项反例全部通过、无跳过 |
| 工具静态 | ShellCheck error 级检查两份构建 shell；actionlint 1.7.12 检查当前 ci.yml；来源锁检查均退出 0 |
| 日志完整性 | `_validate_buffered_snapshot` 校验 L3 原始导出，返回 `(0, 96, 96, 0, 0)`，无缺段/丢失 |

WSL 完整 L1 使用 PATH 前置 `/tmp/miseed-dtschema/bin`、`MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/smb5-status-l2/umi-csot.dtb`、`MISEED_DT_SCHEMA=/tmp/miseed-boot-l2/linux-standard-build-20261110/Documentation/devicetree/bindings/processed-schema.json`。Windows 使用 Python 3.14 与进程级 PATH，不改用户/系统 PATH。最初全量运行遇到尚未写入的本记录链接而失败；创建链接目标并压缩 CONTEXT 至预算后重跑成功，不将首次失败计为通过。

actionlint 来自 rhysd 官方 v1.7.12 release，下载前后核对 GitHub release asset digest：linux_amd64.tar.gz SHA-256 `8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8`。只在忽略目录运行，未引入仓库构建依赖。完整 CI 远端未运行；未做独立专项复核。

## Windows libfdt 工具补正

Windows 首次全量测试还有 6 项 overlay 错误：`FDT_ERR_BADSTRUCTURE`、`Base blob is incomplete (1024 / 1239 bytes read)`。根因是 DTC 1.7.0 原生移植的 `util.c` 用 `open(..., O_RDONLY)` / 普通写标志读取和写入二进制 DTB，Windows 文本模式会处理 Ctrl-Z/换行。此前小样本通过不能覆盖扩展后的 DTB 内容。

来源仍为[既有官方 DTC 1.7.0 包及编译器](2026-11-09-pr3-coderabbit-verification.md)，源码 tar.xz SHA-256 实核为 `29edce3d302a15563d8663198bbc398c5a0554765c83830d0d4c0409d21a16c4`。只在 `.work/tools/dtc-1.7.0/util.c` 给文件 open 的读写标志加入 O_BINARY，非 Windows 未定义时设为 0，保留原许可证/作者；不改 Git 中的内核或 libfdt。原 util SHA-256 `68376fceb65c498edad09f4eee02e3058de38b4281b9cf998611142f831a31b0`，修改后 `a73eaecddef699f70183003ebd691f4f80899a85a42a4a17b0870d476d394b6f`。

用 w64devkit GCC 16.2.0 执行 `gcc -D_GNU_SOURCE -O2 -I. -Ilibfdt <tool>.c util.c <LIBFDT_SRCS> -o <tool>.exe`，分别重建 fdtget/fdtput/fdtoverlay；LIBFDT_SRCS 是原 `libfdt/Makefile.libfdt` 的十份 C 文件。原文件与三份 exe 保留 `.original` 备份，可复制回退。修复后原生 6 项 overlay 回归与全部适用 Windows 回归通过。源码、许可和程序都留忽略目录，不分发产物。

| 本机工具 | 新 SHA-256 |
| --- | --- |
| fdtget.exe | `b1311138ac42b22a2937855608a30ecf268370296da45d79ed61bbd0fc5aba11` |
| fdtput.exe | `6bf8d0ad3256108d4e03ed34c2109406609190089cb8589c86879c1953192a16` |
| fdtoverlay.exe | `5f2c7af36d3fdc99298f6481fb954a7d8211db8587298e15971ca8833eb92b28` |

🚦 最终自查：范围/接口/故障处理/存储供电凭据 🟢，限定本轮证据；没有改变手机写接口或保护。源码篇幅 🟡：8 项仍归 TD-008，本次 SMB5 hw 为 330 行，保留硬件访问职责与下次按内聚性偿还条件；新增工具/测试均≤300 行。文档预算由最终 L0 验证。PR 规模/硬上限仅有本地估算：相对 origin/main `d9a65dca4a1a9530a44047273f61cabbb5f8b572`，约 1200 新增行/50 文件，低于绿灯阈值；远端 additions/changed_files 未查询。高风险合并验收 🔴：仍须开发者以外专项复核及精确 PR HEAD CI，补齐前不合并；本轮局部 L0/L1/L2/L3 不替代这些门禁。

## 2026-11-12：PR6 Boot L2 apt DNS 失败与本地修复

PR HEAD `bcfd241729f1d285b7d39d598d5556c760efba6d` 的 [Boot L2 job](https://github.com/Ben8368/MiSeed/actions/runs/43051898270/job/130630979722) 在 Install pinned kernel build tools 失败，未执行编译或 DT/schema。日志连续出现 `Temporary failure resolving 'azure.archive.ubuntu.com'`，索引失败后沿用 runner 缓存继续安装，最终 apt 退出 100；不是本轮充电代码的编译失败。其余五项约定 CI 通过，CodeRabbit skipped。

本地仅改 workflow 安装步骤及实际脚本回归：在 RUNNER_TEMP 写独立 sources.list/空 sourceparts，仅使用 archive.ubuntu.com 的 noble/noble-updates 与 security.ubuntu.com 的 noble-security，显式 signed-by Ubuntu archive keyring。update/install 共同使用该配置、3 次重试、HTTP/HTTPS 20 秒超时及 APT::Update::Error-Mode=any，安装步骤上限 10 分钟；不改宿主 DNS/全局源、权限或 14 个工具包版本，不绕过签名校验。配置语义见 [Ubuntu apt.conf](https://manpages.ubuntu.com/manpages/noble/man5/apt.conf.5.html)及[归档签名验证](https://documentation.ubuntu.com/security/software-integrity/archive-verification/)。

Windows 10.0.26200 x64 / PowerShell 7.6.9、WSL Ubuntu 24.04 x86_64；本地核验：

- `python3 -X utf8 -m unittest discover -s tests -p test_build_validation.py -v`：9 项通过，新增直接执行 workflow 安装脚本的来源/版本正例与索引失败不继续安装反例，含带空格路径。
- `.work/actionlint-1.7.12/actionlint .github/workflows/ci.yml` 与 `shellcheck --severity=error .work/ci-dns-l3-20261112/apt-install.sh`：退出 0。
- 用同一源配置在独立 `/tmp/miseed-ci-apt-20261112` 下载 35.6 MB 新签名索引，18 秒完成；`apt-get --simulate install` 成功，随后 apt-cache madison 核实所有 14 个精确版本确实存在于配置仓库，未以已安装版本冒充远端可用。没有实际安装/升级宿主包。
- 全量 Linux 回归 240 项、0 跳过，52.237 秒通过；仓库/来源锁/预算与 diff 检查通过。
- `.work/ci-dns-l3-20261112/build-l2.sh simpledrm` 调用锁定 `tools/build_kernel_l2.sh` 及 `validate_kernel_dt.py`：ARM64 L2、定向 binding/最终 DTB/schema/四项 DT 反例通过，均退出 0。boot.img SHA-256 `71619df18f35ff4c0b58c0c96b0dddeab5c48986cb6739d59b2ef1baa5aebbc6`，与前一轮一致；充电源代码没有改变。

修复最初按维护者要求保留本地并进入真机 L3；之后维护者要求保持实测策略，核实已有数据后提交推送 PR #6。本地通过不替代新 HEAD 的远端 CI，旧失败是否关闭须查新 run；材料与现场证据见[有线记录](../hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。
