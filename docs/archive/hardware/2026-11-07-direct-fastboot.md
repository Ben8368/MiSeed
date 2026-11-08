# 2026-11-07 Linux RAM 镜像直接返回 Bootloader Fastboot

关联 RQ-02/06/08、WP2/WP9。本轮维护者明确恢复 Linux 直达 Fastboot 的任务；结果仅覆盖指定设备和诊断镜像。

| 项 | 本轮证据 |
| --- | --- |
| 日期 / 操作者 | 2026-11-07，Codex 执行，维护者完成第二版测试前的硬件按键操作 |
| 授权范围 | RAM 启动及 Linux 直接重启到 Fastboot；维护者另明确允许首次 `adb reboot bootloader`，包含 Android 标准一次性 BCB 路径。此权限仅使用一次；MiSeed 不挂载存储，不写 BCB/分区，不控制充电 |
| 设备 / 变体 | 同一匿名设备 A，ADB/Fastboot 均为 `umi` / Mi 10；CSOT 为既有维护者自述，本轮未重新测定面板 |
| 原系统 / 固件 | ADB：`sys.boot_completed=1`，kernel `4.19.325-cip131-st15-perf-g71b13e62f057`；产品 `lineage_umi`，fingerprint 为 Xiaomi global Android 13 / V14.0.5.0.TJBMIXM；不据此认定底层固件身份 |
| 仓库 / 输入身份 | 基于 `8d73348b3a29398b1804d0ceebe96fb6f4065fce` 的未提交工作分支 `codex/direct-fastboot`；原始 L3 输入哈希见下表；后续 PR #2 集成版单独记录于 [来源锁](../../../manifests/sources.lock.json) |
| 宿主 / 构建 | Darwin 25.6.0 arm64 / zsh；Colima VZ / Ubuntu 24.04 arm64；GCC 13.3.0、GNU make 4.3；Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，v6.18.60 |
| 最终 RAM 载荷 | `.work/direct-fastboot-no-tlmm/boot.img`，7,131,136 B，SHA-256 `ce196d57911a8bb5266c213f2afd6397196bffb376a49c228d18fe46e734a726` |
| 前置 / 回退 | 首次核实 `product=umi`、`unlocked=yes`、`is-userspace=no`。正常恢复点是 `fastboot reboot` 返回原系统；USB 失联时电源键＋音量下恢复 Fastboot。首版自动返回原 Android，维护者随后手动进入 Fastboot；最终停在 Fastboot，未再运行 Android 回退 |
| 结果 / 等级 | 最终镜像连续两轮 Fastboot→RAM Linux→Fastboot，均 L3 通过；每次串口发起后同一设备 `product=umi`、`is-userspace=no`，ADB 列表为空。两轮中未运行 ADB 重启 |
| 未覆盖 | GPIO/触控/原生显示、完整电源管理、充电、其他变体、长时间运行、远端同 HEAD CI 与合并前独立专项复核 |

## 定位与实施

固定主线 `sm8250.dtsi` 将 SPMI 的 `periph_irq` 接到 PDC；`spmi-pmic-arb.c` 在取得该 IRQ 前不会注册 SPMI 总线。旧配置关闭 PDC，运行时 SPMI/PON 未绑定，因而 `restart2("bootloader")` 无法写入模式标记。

本轮首版启用 `CONFIG_QCOM_PDC=y`，增加最多 20 秒 UDC 等待、PDC/PSCI/USB 日志，并在 PON 未绑定时拒绝重启。首版 L2 退出 0，镜像 SHA-256 `96a9b4103aacbdadad241833caa3b50ceb2e1d98e20f833ee71853f971089398`；首次授权 ADB 进入 Fastboot 后，RAM boot 的 Sending/Booting 均 OKAY，但 30 秒内无 ACM，随后自动回到 Android。维护者也确认“现在是安卓”；启动标识变化且 `sys.boot_completed=1`。没有发送串口 bootloader 命令；pstore 读取被拒，不能把这一轮归因于某条已证实的 panic。

固定参考 [colorgarden Umi DTS](https://github.com/colorgarden/umi-mainline/blob/5fa2e0048f89b82355d2a9de89c1d74ab9910959/dts/sm8250-xiaomi-umi-common.dtsi)（BSD-3-Clause）记录过 TLMM 部分 GPIO 寄存器外部中止。主线 `pinctrl-msm.c` 在 PDC 不就绪时延迟 GPIO 注册；PDC 就绪后 gpiolib 会读取有效 GPIO 的方向。因此第二版禁用本诊断不需要的 TLMM，不复制参考 GPIO 范围或供电配置。第二版恢复 USB 并通过两轮往返，支持该隔离方案；尚未取得首版崩溃调用栈，不将 TLMM 的具体故障引脚宣称为已定位。禁用取舍见 TD-002。

最终串口 `status` 两次确认：PDC 绑定 `qcom_pdc`，`c440000.spmi` 绑定 `spmi_pmic_arb`，PM8150 PON 绑定 `qcom-pon`，DT `mode-bootloader=0x2`，UDC 为 `a600000.usb`，内核 reboot mode 为 `cold`。本轮无需改暖重启或引入 vendor PSCI reset。PON 使用既有主线 reboot-mode notifier，未新增裸寄存器访问。

运行时还观察到 ABL/DTBO 添加的 `qcom,pm8150@0` 等节点与主线 `pmic@0` 等节点重复，产生 SPMI `-EEXIST` 警告；已绑定的 PM8150/PON 和 USB 往返仍工作。后续风险见 RISK-010；不将这些警告归为已修复。

## 实际命令与验证

主机工具为仓库忽略目录内 Platform-Tools 37.0.2，下文设备参数已脱敏。两版的构建命令相同，仅 `MISEED_OUTPUT_ROOT` 分别为 `/workspace/.work/direct-fastboot-pdc` 和 `/workspace/.work/direct-fastboot-no-tlmm`：

```sh
docker --context colima-miseed-boot-l2 run --rm --mount type=bind,source=/Users/ben/Project/Code/MiSeed,target=/workspace --mount type=volume,source=miseed-boot-l2-miseed-boot-l2,target=/build --workdir /workspace --env MISEED_WORK_ROOT=/build --env MISEED_OUTPUT_ROOT=/workspace/.work/direct-fastboot-no-tlmm --env MISEED_JOBS=4 miseed-boot-l2:local bash tools/build_kernel_l2.sh
```

两版均退出 0，152 个 ABL anchors / 241 个 DTBO targets 检查通过。源码修改后、完整验证前分别报告 Audit。离线检查：`python3 -X utf8 tools/check_repo.py`、`git diff --check`、`bash -n tools/build_kernel_l2.sh`、`shellcheck --severity=error tools/build_kernel_l2.sh`、`python3 -X utf8 -m unittest discover -s tests -v`（28 项）均通过。另在容器用替换 reboot syscall 的临时 C 驱动，仅调用 `request_bootloader()`，验证 PON 未绑定时拒绝并且系统调用次数为 0；未运行手机 init。

设备顺序：一次 `.work/host-tools/platform-tools/adb -s <设备A> reboot bootloader` → 首版 `fastboot -s <设备A> boot .work/direct-fastboot-pdc/boot.img` → 自动回 Android → 维护者手动进入 Fastboot → 第二版 `fastboot -s <设备A> boot .work/direct-fastboot-no-tlmm/boot.img` → 串口 `status` → 串口 `bootloader` → Fastboot 只读验收 → 同一镜像再启动一次 → 串口 `status` / `bootloader` → Fastboot 再验收。全部 fastboot boot 的 Sending/Booting 均 OKAY，工具退出 0。

ACM 使用 macOS `/dev/cu.usbmodem101`、115200；主机临时 Python 工具仅设置串口、发送固定命令和读取输出。第一轮重启时串口读取报 `ENXIO`（设备重枚举），随后 Fastboot 核验成功；主机工具随后把预期断连作为正常结束，第二轮退出 0。没有把串口断连本身当作 Fastboot 成功证据。

RISK-008 在本机、本镜像的直接 Fastboot 路径范围内关闭；板级完整性与 TLMM 恢复不在此关闭范围。所有改动保留本地未提交状态，未推送、发布或合并。

## 推送 PR #2 前的集成

维护者随后明确要求“推送到 pr2”。目标为既有 `codex/fastboot-charging-governance`，基线 `3999ab85aa901acd2b4c819d81a97d665be7a474`；保留该分支的异步 UDC 持续重试、串口挂断恢复、源码缓存完整性校验和 macOS 架构隔离。以持续重试取代本轮首版的有限 20 秒等待，避免引入重复等待；风险编号 RISK-009 已由历史设备标识占用，本轮 PMIC 风险调整为 RISK-010。

原始已测 `ce196d57…734a726` 镜像的项目输入如下；其余生成器输入继承 `8d73348b`，配置/内核/DTB/产物哈希以本节前述 L2 记录为准。集成版 initramfs 已变化，原始两轮 L3 不作为集成版 L3。

| 原始 L3 输入 | SHA-256 |
| --- | --- |
| `kernel/umi-csot.dts` | `0d1fa09475c857451423a1e0675f13d31e3b07186313ebf1f25ccac5a6e4167d` |
| `kernel/umi-l2.config` | `1cbd8807c4815ade6d5708b86a956103018a3e11d61672a9dd01f612451fe99b` |
| `kernel/rescue_init.c` | `b538359685d1743cd8597e533b678337458bf31804779be9699cd5a620e702f3` |
| `tools/build_kernel_l2.sh` | `babd8aec0a37b94a6e439bb287c388c3b5353d6db315341c9094c37ca54457a2` |

集成版使用同一 Docker 命令，仅输出目录改为 `/workspace/.work/pr2-direct-fastboot`，构建退出 0；缓存完整性核验通过，152/241 符号检查通过。镜像为 7,131,136 B，SHA-256 `3711704ca29a02c3cd7a8b13d9e84beec8829a685adf3955f3f8292121c304ec`；initramfs 与原 L3 镜像不同，当前输入和完整产物哈希见来源锁。

容器执行 `python3 -X utf8 -m unittest discover -s tests -v`：46 项全部通过，包含既有 USB 延迟绑定/两次挂断恢复、新增 PDC/PSCI 日志保留与 PON 未绑定时不触发 reboot syscall 的反例。仓库检查、diff 空白、两份构建脚本 Bash 语法和 ShellCheck error 级均通过。macOS 主机上的来源锁输入及集成镜像哈希核对一致。推送仅追加提交，不改写 PR2 历史；同 HEAD 远端 CI、集成版 L3 和合并前独立专项复核仍待完成。本轮推送任务不操作手机。
