# 2026-11-08 USB 断线充电日志 L1/L2/L3

关联 RQ-11 / WP9。结论：本机已验证电脑 USB 拔出后继续记录，短时接入/拔出充电器后接回电脑，可导出原有 RAM 时间线；没有实现或验收主动充电管理、温控、PPS 或电池侧净充入。

## 回滚和输入

维护者要求完全回到 `b97f1d9977caf211e5c219a0fb3431b922247e0c`。开始时 HEAD 为 `20be2ac0c6bae9c3b57255b685efb166e722bb03`，有上轮未提交充电修复；已把 tracked diff 和 untracked 文件备份到忽略的 `.work/before-b97f-rollback-20261108/`，然后 `git switch --discard-changes -c codex/charging-session-log b97f1d9977caf211e5c219a0fb3431b922247e0c`，删除经备份核对的上轮三个 untracked 文件。开始本轮实现前 `git diff --exit-code <目标 SHA>` 退出 0，工作区干净。

因此本次代码基于精确目标提交，仅新增日志功能，不包含后续 SMB5 控制、LOS Gen4、profile 或 rootfs 限充服务。本记录形成时旧分支提交仍可追溯，新增改动未提交、推送、发布或合并；后续覆盖 PR #2 的维护者授权见 CONTEXT。

宿主 Darwin 25.6.0 arm64 / zsh；Colima/VZ、Ubuntu 24.04 arm64，GCC 13.3.0、GNU make 4.3；镜像 `sha256:883d1dc17e202acfaa976019b153fa2a1c2fe91f4d7b488982ebd2bfc2018500`。固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`（v6.18.60）。

## 实现和边界

- [硬件观察器](https://github.com/Ben8368/MiSeed/blob/a081ab302a3099eb1825ed76fe8cb011e1cec79c/kernel/power_observer.c)在现有主线 PM8150B `pmic@2` 下绑定，每秒只读 `0x1310`（USB RT）、`0x1006`（CHGR status1）、`0x1007`（status2）、`0x1042`（enable）。记录 USB present、SMB5 硬件阶段、使能位和原始状态字；任何读取失败输出地址/错误码，不沿用旧值。
- 不申请/确认 IRQ，不写 PMIC，不初始化 SMB5/FG，不加载电池 profile、不读 SRAM 或身份/NV、不调整充电电流/电压/使能。`CONFIG_POWER_SUPPLY=y` 的类存在，但 resolved config 没有启用 `CHARGER_*` / `BATTERY_*` 控制驱动。
- 状态定义只读对照既有固定 qcom_smbx 补丁（SMB5：Robin Snyders；基础 qcom_smb2：The Linux Foundation / Casey Connolly），没有重新导入或应用其充电控制代码。来源完整提交、补丁路径/哈希、生成器及本地输入哈希见[来源锁](../../../manifests/sources.lock.json)。本地观察器为 GPL-2.0-only，自有用户态/工具为 AGPL-3.0-only；未新增外部固件或下载执行外部代码。
- [RAM 记录器](../../../kernel/charge_log.h)由 PID 1 持续运行，和 USB 子进程独立。memfd 最多保留 8192 条、每条 768 字节，包含启动标识、递增序号、boottime 毫秒；满后覆盖最旧记录，导出头报告覆盖数。它每秒读取相关内核事件、每两秒读取已有 power_supply 属性和 UDC state；缺失温度/SOC/V/A 明示 unavailable。
- `/dev/kmsg` 读取不清除全局 ring buffer。内核 overrun 记 LOSS，长事件标 truncated。每轮最多读取 128 个内核事件，避免洪泛阻止采样。PMIC 日志另以非阻塞方式尝试输出到 `/dev/tty0`；本次没有单独验收屏幕显示。
- 导出复制一致快照后即释放进程锁，再写 USB；USB 输出阻塞/掉线不会持有采集锁。`charge-log` / `logs` 导出全部仍保留的历史，`charge` 导出最近 32 条。重复读取不消费历史，USB EOF/HUP 走既有重连路径。
- 日志只在本次运行的 RAM 中，重启/断电丢失；并非跨重启持久化。8192 是记录条数，不承诺固定时长。阶段/使能位不能证明电池正在净充电，原始 status2 本轮不作故障位或温度解码；UDC configured 也不能独立证明仍接电脑。

RAM 日志和序号协议所依赖的接口已对照 [Linux power_supply 单位契约](https://docs.kernel.org/power/power_supply_class.html)及 [POSIX 进程文件锁](https://man7.org/linux/man-pages/man2/fcntl_locking.2.html)复核。

## 离线验证

源码修改后、完整验证前已输出 🚦 Audit Report：仅读硬件、RAM 有界、供电控制保持缺失；日志 L3 不解除 RISK-004/010。高风险合并前的独立专项复核仍未完成。

`python3 -X utf8 -m unittest discover -s tests -v`：Linux 共 59 项全部通过；macOS 共 59 项，47 通过、12 项 Linux 专属跳过，退出码均为 0。相关测试覆盖：

1. 实际观察器 C sample 的八个阶段、USB/enable 位、四个寄存器分别失败时不输出伪状态；静态拒绝引入 regmap 写入/IRQ 控制。
2. 实际用户态 C 代码通过 memfd/POSIX 锁合成验证 RAM 环形覆盖计数、重复导出、缺失/截断数据明确失败；stdout 管道阻塞期间仍可写新记录。
3. Linux PTY 两次挂断恢复；另以独立记录进程在终端断开期间记录模拟 charger-inserted/removed，重连后命令导出仍有两条事件。不运行手机 PID 1，不访问硬件。
4. 电脑导出器验证起止标记、完整序号、丢号/重复/错误结尾/中断拒绝、禁止覆盖旧导出；构建缓存仅放行确定的 MFD Makefile 接线，其他编辑仍拒绝。

`python3 -X utf8 tools/check_repo.py`、`git diff --check`、`bash -n tools/build_kernel_l2.sh`、`shellcheck --severity=error tools/build_kernel_l2.sh` 最终均退出 0。没有修改 workflow、运行远端 CI 或完成独立审查。

## L2 构建

实际命令：

```sh
docker --context colima-miseed-boot-l2 run --rm --network none --mount type=bind,source=/Users/ben/Project/Code/MiSeed,target=/workspace --mount type=volume,source=miseed-boot-l2-miseed-boot-l2-arm64,target=/build --workdir /workspace --env MISEED_WORK_ROOT=/build --env MISEED_OUTPUT_ROOT=/workspace/.work/charging-session-l2-20261108 --env MISEED_JOBS=4 miseed-boot-l2:local bash tools/build_kernel_l2.sh
```

退出 0；原缓存不匹配固定上游，构建器保留缓存后从锁定 bare repo 新展开源码；新 build 为 `/build/build-clean.T8jJu8`。观察器对象确实编入，152 个 ABL 锚点、241 个 DTBO target 符号检查通过。`CONFIG_MEMFD_CREATE=y`、`CONFIG_FILE_LOCKING=y`；没有引入 Type-C/充电控制启用配置。最终输入和产物锁已更新。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| ARM64 Image | 6,687,232 | `2b8506c27abea53aaa35efc9b9bb6a04c2f3c103fa0ba340b82d94200bdff50f` |
| boot.img | 7,131,136 | `0bc3ef160b6494aa968c95bc9c08875829a18b3206c82f2fecced55e6a4d72f4` |
| resolved config | 61,600 | `e349dd6d4cf342e8607ecb5f06e0c7b31ff4a59b60b3a33c21a1c95e3030d35c` |
| DTB | 171,876 | `b4ea967d51e731b0b1628ae53e4e4e0d2be8ab5d77b561895115bdcaf5141616` |
| power observer 对象 | 7,096 | `e093707d4b87b6929a5485d8de23e479214c2fc3eda956e7ea51002740116e98` |

构建、测试输出及启动镜像只在忽略目录 `.work/charging-session-l2-20261108/`。原始长日志不进入版本库。

## L3 授权、操作与回退

| 项 | 本轮事实 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08，Codex 操作软件，维护者完成充电器与电脑线缆切换 |
| 授权 | 维护者本轮明确允许一次 `adb reboot bootloader`（含 Android 标准 BCB 路径）、`fastboot boot` RAM 启动、约 10 秒短时充电器接入/移除、重连读取并返回 Fastboot；不启停/调整充电、不刷写分区 |
| 设备 | 当前唯一已授权设备 A；Android `ro.product.device=umi`；Fastboot `product=umi`、`unlocked=yes`、`is-userspace=no`。未输出序列号或访问身份/NV |
| 镜像 | 只启动上述 SHA-256 的 `boot.img`；发送 6964 KB、Sending/Booting 均 OKAY，工具退出 0 |
| 回退 / 停止 | 导出完成后串口 `bootloader` 返回原生 Fastboot；失联时电源＋音量下恢复；异常发热立即拔线停止。未使用刷写或重锁命令 |
| 前置检查 | `/dev/cu.usbmodem101` 可用；首次 `charge-log` 导出 102 条，覆盖数 0；内核可见只读观察器绑定 PMIC@2 |
| 人工切换 | 维护者确认完成电脑拔出→接充电器→拔充电器→接回电脑；没有重启手机 |
| 断线后的导出 | `after-charger.log` 共 250 条（seq 0..249），起止标记有效、序号连续、覆盖数 0；与首次导出 boot_id 相同，无观察器 read_error |
| 重复导出 | `replay.log` 共 354 条，前 250 条与上一份逐字相同，覆盖数 0；证明导出不清历史 |
| 结束状态 | 串口 `bootloader` 后核实唯一 Fastboot 设备，`product=umi`、`is-userspace=no`，已恢复指定回退点；未继续重启 Android |
| 未覆盖 | 重启后持久化、缓冲长时饱和的真机结果、温度/SOC/电池 V/A 精度、净充入、充电协议/功率、异常保护及其他设备 |

实际内核状态变化（时间为记录器 boottime，约 1 秒采样，不能作为精确插拔边沿）：

| boot_ms / seq | USB present | hardware phase | 原始值 |
| --- | ---: | --- | --- |
| 49,768 / 169 | 0 | disabled | usb_rt=0x26，chgr1=0x47，chgr2=0x08 |
| 55,844 / 181 | 1 | fullon | usb_rt=0x10，chgr1=0x03，chgr2=0x08 |
| 71,204 / 210 | 0 | disabled | usb_rt=0x26，chgr1=0x47，chgr2=0x08 |
| 78,375 / 226 | 1 | disabled | usb_rt=0x10，chgr1=0x47，chgr2=0x08 |
| 79,399 / 227 | 1 | fullon | usb_rt=0x10，chgr1=0x03，chgr2=0x28 |

依据维护者确认的动作顺序，前后两段接入对应充电器和电脑；采样显示中间输入持续约 15 秒，不把用户的“约 10 秒”操作指引当作精确计时。本次 UDC state 始终显示 configured，**不能用它判定物理 USB 是否仍接电脑**；PMIC 的接入/断开变化与人工动作及导出连续性构成日志证据。`chg_enable` 始终为 1，是继承的硬件状态，不是本程序开启的，也不证明电池净充入。

## 后续使用

1. 启动该日志镜像并确认串口可用；保持同一次启动，执行电脑→充电器→电脑的切换，异常时拔线停止。
2. 重连串口执行 `charge-log` 或 `logs`；`charge` 仅看最近 32 条。保持运行才能保留 RAM 历史。
3. macOS/Linux 可关闭其他占用串口的终端，用[导出器](../../../tools/collect_charge_log.py)保存：

```sh
python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output local/charging-session.log
```

Linux 使用实际 `/dev/ttyACM…`，Windows 直接在串口终端输入 `charge-log`。不要照抄不存在的端口。工具拒绝覆盖现有文件；只有起止/序号完整才生成最终 `.log`，失败留下 `.log.partial` 并报告错误，可保持手机运行后换文件名重试。

本轮命令只用允许的 ADB/Fastboot 和串口命令；具体调用和原始结果保存在忽略目录。日志 L3 不解除 RISK-004、RISK-010、TD-002；后续主动充电仍需匹配校准、保护闭环与独立专项复核。
