# 2026-11-10–11 有线 PD/PPS 短测复核、掉线修复与 Colima 时钟

## 输入与范围

macOS Darwin 25.6.0 arm64 / zsh；Colima 0.10.3、Lima 2.2.1、VZ aarch64、Ubuntu 24.04 native arm64 容器。分支 `codex/linux-standard-interfaces`，HEAD `899d64aa114ab4589bf8ba1544f54ef84b97b59b` 加未提交修复；接续维护者提供的验证对话及工作树。不提交、推送或合并。本轮没有设备操作、RAM 启动、存储写入或充电控制。

仅涉及**有线** TCPM PD/PPS、BQ25970 与 SMB5 互锁；无线 RX/TX 未改。沿用 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的 Type-C/TCPM、power_supply、IIO/thermal；标准 sysfs 消费者和既有板级策略接口未变，未增加 Android 依赖或私有 ABI。既有临时拆分归 TD-004/005，本次不扩大例外。

旧短测载荷 SHA-256 `2a5f03ec62e5278666d2a8b7867c1db8dd8705e6851ad9329e31e50aca1cb084`；启动授权与设备 `umi` 的核对来自提供的前次对话。原始日志仅保留在忽略目录 `.work/ramboot-899d64a/`，不入库。`charge-log` 为 941 条、first=0/next=941、storage_errors=0，BEGIN/END 完整；SHA-256 `a37c23d25d6942990ea0376783cb10c9ef933152781b21f03000f782124d6c69`，TCPM 日志 `e54a019958fd949e3d468a78e2583dc1d962f779ceeb70c8e0d6746a82aa5ad2`。

## 短测时序与修复

- `111.645806s` 的 `-ENOTCONN`：TCPM 先 soft/hard reset，`111.645762s` 记录 CC 断开；`119.254031s` 的同码对应 `119.234821s` Received hard reset、`119.253890s` CC 断开。可以解释直接错误链；源端 reset 的物理诱因仍未确认。
- `124.317411s` 的 `-EIO`：请求 8980 mV 时 TX complete status=2，随后固定 PDO 回退及 Get Source Cap 也发送失败，进入 Hard Reset，`125.996667s` 恢复 5 V。保留故障停机，不把发送失败变为无条件重试或正常掉线。
- `150.775033s` 的 `-ERANGE/fault=1`：BQ fault/latch=0、alarm=0x02、status=0、watchdog=0x44、control=0x07；软件汇总 fault 包含 RUN 中 CHG_EN 清零，并非非零硬件故障寄存器。TCPM 在 `150.780935s`（约 5.9 ms 后）记录 CC 断开，符合掉线竞争窗口；不能证明保护专项通过，也不能解释所有历史 BQ 故障。
- 固定 PDO 源上 `-EOPNOTSUPP` 保留既有回退与不启泵行为；不是把普通 PD 源强制当 PPS 源。

前次未提交实现把任意错误伴随 ONLINE=0 都归正常掉线，本轮收窄：只有 TCPM `-ENOTCONN`，或 RUN 丢失 CHG_EN/switching 且 fault/latch、状态、报警、watchdog 和电池/电流/电压绝对限值均符合条件时，才复查 TCPM ONLINE。仅确认 0 后走 WAIT_PLUG；在线、复查读错和其他异常仍保留 FAULT。若 ONLINE 尚未来得及更新，仍允许原故障路径安全停机，不额外等待或放宽阈值。

正常掉线也必须停泵并回退 PD；任何停机错误仍保留 SMB5 独占所有权，后续 worker 先重试清理。保留原始采样诊断。未修改电压、电流、温度、采样电阻或硬件保护配置。更新了该源文件的构建输入哈希。

`tests/pps_detach_cases.h` 覆盖：请求后掉线、旧 ONLINE/BQ 已停泵的窗口、在线时同样停泵仍报错、入场/运行请求返回失联、失联但仍在线、重连、停机失败保留所有权及恢复；同时掉线遇传感器 I/O、故障锁存、保护读回错误、超时、反向电流或低电压仍必须 FAULT。芯片后端与原有保护/互锁回归继续运行。

## 时钟根因、处置与回退

此处是构建 VM 时钟，非手机时钟或 BQ 时钟。WSL 的旧处置见[环境复核](2026-11-10-kernel-validation-environment.md)；本轮没有修改 WSL 或 Windows。

只读证据：来宾 timesyncd enabled/active，NTP offset +322.177 ms、jitter 440.664 ms、frequency +500 ppm；同时 Lima `ha.stderr.log` 每 10 秒出现宿主校时，23:45:31 单次漂移 2017 ms。四次宿主/容器往返取样中来宾会越出宿主区间，最后一次超过宿主返回时间约 869 ms。[Lima 2.2.1 实现](https://github.com/lima-vm/lima/blob/v2.2.1/pkg/hostagent/timesync.go)确认 10 秒宿主同步周期。双重调钟与停用前后对照支持同步源竞争这一根因；没有证据将它归于 VZ 计数器硬件故障。

仅对项目独立 profile 执行：

```sh
colima --profile miseed-boot-l2 ssh -- sudo systemctl disable --now systemd-timesyncd.service
```

保留 Lima 宿主同步。随后约 31 秒取样，全部来宾 REALTIME 位于宿主请求起止区间。但 NTP 留在内核的 +500 ppm 仍使时间每 10 秒增加约 5 ms，23:52:01 出现一次 102 ms 校正。因此在 L2 完成后，通过 `.work` 的临时 Linux C 诊断器读取 `adjtimex`：freq=32768000（65536 比例的 +500 ppm）、offset=0；以 `ADJ_FREQUENCY | ADJ_OFFSET` 将 freq/offset 清零，读回均为 0。未修改 tick、clocksource、文件 mtime、固定 KBUILD_BUILD_TIMESTAMP 或宿主时间。诊断器仅留忽略目录，不成为构建依赖。

清零后独立 35 秒测量：来宾 REALTIME=35.005343 s、MONOTONIC=35.005347 s、RAW=35.005364 s；宿主含容器启动总耗时 35.288266 s，末端来宾时间落在宿主往返区间。后续读回 freq/offset 仍为 0，Lima 漂移约 47 ms 保持稳定。原 `run_build_command.py` 的未来时间/Clock skew 非零退出门禁保持有效。重建未检出两类告警；未用 touch、忽略警告或自动重试掩盖问题。

回退只针对该 VM：`colima --profile miseed-boot-l2 ssh -- sudo systemctl enable --now systemd-timesyncd.service`，重新交由 NTP 管理频率（可能重现冲突，需先核对 Lima 同步）；新建/更换 profile 须重查。`colima status <profile>` 在本机未正确选择实例，诊断使用明确的 `colima --profile miseed-boot-l2 status/ssh`。

## 验证与审计

首次 L2：`MISEED_JOBS=4 bash tools/build_kernel_l2_macos.sh`，退出 0；固定输入、overlay 正反例、配置、链接符号、打包均通过，无 future-time/Clock skew。旧源缓存 `qcom,pmi8998-charger.yaml` 与锁定提交不同，验证器按设计保留旧缓存并使用干净源码/构建目录，没有绕过校验。

产物 `.work/boot-l2-arm64/boot.img` SHA-256 `9f7534c122669da97defb0d4566ee9aa1732adfd567d5c2e2f291a2aa51872f3`；DTB `8c7f8455cf3b6310648971daf38861a495ad354b359dc0572f58f700f03cb8de`；initramfs `a54d41ef981428464a3ea87f6c920f4252402c3f8edd11c25b4c43c6395d4265`。

初轮 `python3 -X utf8 -m unittest discover -s tests -v`：macOS 190 项、退出 0，跳过 36；Linux 容器 190 项、退出 0，跳过 4。macOS 的 32 项平台限制为 Linux Bash 3 项、Linux NFC 10 项、Linux rescue/PTY 19 项，均在 Linux 实跑；两边剩余 4 项缺 DT/schema 环境，不能计为通过。维护者要求补齐后，使用 CI 已有固定依赖版本、专用 venv 和实际 L2 DTB/schema 执行专项及全量，补验结果：第二次干净 L2 重建退出 0，镜像/DTB/initramfs 均与首次同哈希，无未来时间或 Clock skew；专用 DT 检查及四项正反例通过、零跳过；最终 Linux 全量 **191 项全部通过，零跳过**（新增一项 yamllint 依赖反例），退出 0。依赖沿用 CI 的 `python3-venv=3.12.3-0ubuntu2.1`、`python3-dev=3.12.3-0ubuntu2.1`、`swig=4.2.0-2ubuntu1` 与 `tools/requirements-dtschema.txt` 的 `dtschema==2025.8`；apt/pip 传递闭包仍归 TD-001。最终 PPS 定向 4/4、构建/DT 门禁定向 6/6 通过，含上述控制器反例。

补验首次被严格入口拦截：`dt_binding_check` 在缺 yamllint 时返回 0 却在 stderr 报跳过，不能计为通过。现已在 requirements/工具锁固定 [yamllint 1.38.0](https://pypi.org/project/yamllint/1.38.0/)（GPL-3.0-or-later，独立宿主工具，不链接到内核），入口预检版本和可执行文件；新增反例覆盖缺包、错版本、缺可执行文件。更新入口/requirements 的来源锁哈希；CI 已有安装入口自动消费，无 workflow 修改。最终 `check_repo.py` 和 `git diff --check` 退出 0。

补验命令（容器挂载仓库至 `/workspace`、既有架构 volume 至 `/build`，PATH 前置 `/build/pd-pps-dtschema/bin`）：

```sh
python tools/validate_kernel_dt.py --source /build/pd-pps-linux-source-f45ed76d --build /build/pd-pps-linux-build-f45ed76d --dtb /workspace/.work/boot-l2-arm64/umi-csot.dtb
export MISEED_WIRED_DTB=/workspace/.work/boot-l2-arm64/umi-csot.dtb
export MISEED_DT_SCHEMA=/build/pd-pps-linux-build-f45ed76d/Documentation/devicetree/bindings/processed-schema.json
python -X utf8 -m unittest discover -s tests -v
```

实际日志在忽略目录 `.work/pd-pps-clock-fix/`：`build.log`、`validation-linux.log`（补验首轮因缺 yamllint 退出 1）、`validation-linux-final.log`（最终退出 0）、`clock-after.json`、`clock-final.json`。独立 venv、保留的 source/build/schema 继续在上述 volume 路径，可直接重跑；无需重新提取或依赖临时 fallback 目录。

🚦 范围、许可归属、标准接口与本轮数据/供电边界自查 🟢；源码篇幅 🟡：`umi_pps.c` 506 行、`pps_harness.c` 599 行，TD-008 保留内聚性与偿还条件，新增掉线测试头低于 300 行。相对本地 origin/main 规模约 1418 新增行/54 文件（含未跟踪测试/记录，最终文案可能有少量行差），低于绿灯阈值；不是 GitHub PR 计数。合并验收 🔴：新行为 L3、开发者以外供电专项复核与精确 HEAD CI 未执行；保护注入、温控标度、物理诱因和更早异常归 RISK-004。既有 PD/PPS 各约 10 分钟稳定结论保留。
