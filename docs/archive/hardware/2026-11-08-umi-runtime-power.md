# 2026-11-08 Umi 受限充电运行移植与插拔测试

## 授权、输入与范围

| 项 | 本轮事实 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08；Codex 实施与读取，维护者操作线缆；Darwin 25.6.0 / arm64 / zsh |
| 授权 | 维护者要求完成移植并启动镜像，另明确选择一次短时充电测试、结束恢复 70/65；确认已完成电脑 USB → 充电器 → 拔出 → 电脑 USB |
| 设备 | 唯一设备 A，umi；启动前 Android 16，普通 ADB；电池服务报告 SOC 78%、30.2°C、4105 mV。面板/底层固件未重新鉴定 |
| 仓库 / 来源 | `codex/umi-upstream-power-port`，HEAD `2acf745` 加未提交改动；Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；具体输入哈希见来源锁 |
| 构建环境 | 既有 Ubuntu 24.04 arm64 Colima 容器、GCC 13.3.0、GNU make 4.3；容器断网 |
| 载荷 | boot.img 7,405,568 B；SHA-256 `10ec0d0ee1a8255e14db31e0cc0f112dfbe310541a16eabc8992be4bdd9ce7c4`；仅 RAM boot |
| 启动时来源锁 | SHA-256 `717877cdc7807e3d339b600c9b211131daedf4d0d910cde997c4fb250b1c67a6`；后续只更新状态和证据元数据 |
| 操作边界 / 回退 | 不刷写、格式化、访问身份/NV、加载认证或电池 profile SRAM；不使用 adb root。正常回退是串口 `bootloader` → 原生 Fastboot → `fastboot reboot`；失联时电源＋音量下恢复 Fastboot。回退路径已有[本机证据](2026-11-08-upstream-power-ramboot.md) |

## 移植内容与归属

1. 保留 colorgarden 固定提交 `5fa2e0048f89b82355d2a9de89c1d74ab9910959` 的 FG/SMB5 原始文件头、GPL-2.0-only、原贡献者；新增修改与详细归属见 `kernel/power/NOTICE.md`。
2. 运行节点及 IRQ/ADC 接线采用 misaka-link 方案依赖的 Armbian/build `8ddeef322b4706b5be19d0fb7869f107c117666d` 中 0027（map220v / Jiali Chen）、0029（Teguh Sobirin / Jiali Chen），没有采用 colorgarden 电池 DTS 的 IRQ 列表或近似参数。battery 设计容量 4780 mAh、描述值来自 misaka `cf83073887899f485a5c606f9842c5e691ea796c`。
3. PM8150B FV/FCC 编码、watchdog、Umi 正常温区/电压上限对照当前 Android 内核对应的 LineageOS `71b13e62f057a649b77fe4062feb73ee72ad609c`：`qpnp-smb5.c`、`smb5-reg.h`、`fg-reg.h`、`umi-sm8250.dtsi`。适配文件及参考文件 SHA-256 均在来源锁。没有导入原厂二进制。
4. Gen4 只读外围测量，不为了暴露读数初始化 IMA/DMA 或加载 profile；charger 不再使用旧的 PMI8998 FV 编码、启用充电/关闭 watchdog 的广泛初始化序列。修复过压 health 误报、IRQ 读取错误、SMB5 phase 枚举，采用标准 `charge_behaviour`。
5. 内建 FG/SMB5/ADC5。探测先停充；电流限制读回校验为 FCC 500 mA、输入最多 500 mA，来自上游 USB2 SDP 基线。FV 使用 PM8150B 的 3.6 V + N×10 mV 编码，不高于继承 FV 和 Umi 4.45 V 正常上限；本机实际继承上限为 **4.40 V**。
6. 仅在电池 present、health Good、温度 **>15°C 且 ≤45°C**、电压未达上限、SOC/读数有效、限值/watchdog/die-shutdown 配置读回正常时才允许充电。温区取自 Umi `chg-cool-th`/`chg-warm-th`；不编造温控或采样电阻参数。读取/限制异常停充；停充写失败还尝试暂停输入，不继续喂 watchdog。watchdog 使用厂商 bite-to-stop 语义，未关闭保护。
7. 用户显式准备后，测试等下一次断线/插入才开始；最长 60 秒或拔出立即结束。测试只临时绕过 SOC 上限，其他门控保留，结束恢复作者的 70/65 滞回语义。政策改写为内核有界 worker，未复制 Bash/systemd 服务；不依赖电脑串口持续连接。

## L0 / L1 / L2

源码后已输出 Audit。最终 Linux 66 项离线回归全部通过；macOS 66 项中 54 执行、12 平台跳过。ShellCheck error 级、仓库检查和 diff 空白检查通过。测试包含策略温度/电压/SOC/断线/电池缺失边界、非法枚举、I/O 错误、PM8150B 电压编码及继承低限保持、错误源码与符号链接拒绝、运行驱动必须内建，以及既有后台断线日志回归。

首次目标编译发现内核 `current` 宏命名冲突和 sysfs API 单组/多组兼容问题，均已修复。最终构建无 C/DTS 告警或错误，退出 0；输入仍经完整源码缓存校验。

最终完整构建命令：

```sh
docker --context colima-miseed-boot-l2 run --rm --network none --mount type=bind,source=/Users/ben/Project/Code/MiSeed,target=/workspace --mount type=volume,source=miseed-boot-l2-miseed-boot-l2-arm64,target=/build --workdir /workspace --env MISEED_WORK_ROOT=/build --env MISEED_EXTRACTED_SOURCE=/build/linux-source.mQ9SPb --env MISEED_BUILD_ROOT=/build/build-clean.AuyWKi --env MISEED_OUTPUT_ROOT=/workspace/.work/umi-runtime-power-l2-20261108 --env MISEED_JOBS=4 miseed-boot-l2:local bash tools/build_kernel_l2.sh
```

`MISEED_EXTRACTED_SOURCE` 只选择缓存位置，仍逐文件校验固定 Linux 树、再覆盖本地锁定输入；不绕过源码校验。离线回归为同容器中 `python3 -X utf8 -m unittest discover -s tests -v`；宿主另执行同命令、`shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh`、`python3 -X utf8 tools/check_repo.py` 和 `git diff --check`。

| 产物 | SHA-256 |
| --- | --- |
| Image（6,953,472 B） | `0c7054d01d4b0176325412cdbf357e6a276788dae3135e91a3097151bcfc6c62` |
| resolved config | `42681b2b42daad5c964e2e6eb4f6899877d6d43aab50e307eb3a8a8977868d78` |
| initramfs | `4b45ab8104c7d53cc5e57ffff095033b5e8989d7fd472f92392e0e6a3d9cf603` |
| DTB | `f108a562c88b9f7d6ac8393b9657c821b2212abab3bfa60ce08271ed9765bf39` |

## 实际真机命令与观测

ADB/Fastboot 使用忽略的 `.work/host-tools/platform-tools/` 下工具，对预先锁定的同一 `<设备A>` 操作；占位符代替真实序列号。一次 `adb -s <设备A> reboot bootloader` 后检查 `product=umi`、`unlocked=yes`、`is-userspace=no`，再 `fastboot -s <设备A> boot <上述boot.img绝对路径>`，Sending 7232 KB / Booting 均 OKAY、退出 0。

USB ACM `/dev/cu.usbmodem101` 出现。串口 `status` 核实 PDC/SPMI/PON 已绑定；FG 与 charger 供电对象出现。多次 `charge-log` 导出初始/准备/测试后/重放快照，所有导出成功。初始仅停充；一次启动瞬态原始电流读数触发 `rc=-75`，保持 `enabled=0`，随后连续样本 `rc=0`，才执行串口 `charge-test`。准备后核实 `armed=1 active=0 enabled=0 rc=0`。

维护者确认完成插拔。重连后两次运行：

```sh
python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/umi-runtime-power-device-20261108/after-charger.log --timeout 30
python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/umi-runtime-power-device-20261108/after-replay.log --timeout 30
```

两份各 **1004 条**，seq 0..1003 连续、覆盖 0、同一 boot session，记录逐字相同；准备前 654 条也完整保留。257 条 controller 样本中，23 条处于测试且 `enabled=1`，均 `rc=0`。

| 日志 boottime ms | 观测 |
| ---: | --- |
| 169404 | 单次测试已准备，当前电脑输入，停充 |
| 195880 | 电脑拔出，input=0，继续停充、等待下一次插入 |
| 199976 | 充电器接入，test_active=1 / enabled=1；开始最长 60 秒窗口 |
| 224515 | 充电器拔出，input=0 / enabled=0，test_armed=0 / test_active=0 |
| 231601 | 重连电脑，保持停充，恢复 70/65 策略 |
| 272070 | 最近样本 input=1 / enabled=0 / rc=0，SOC 77%、32°C，测试已结束 |

上述约 24.5 秒为采样中观测到的输入/控制区间，不是精密插拔计时。第一次启用行中的电流在写 enable 前已读取，不能当作启用后的稳定电流。随后 **22/22** 样本原始电池电流为负，范围 **−102,539 至 −70,800 µA**、中位 **−92,773 µA**；电池电压 **4.1269–4.1355 V**，温度 **31–32°C**，SOC 维持 77%。拔出时可见约 +0.40 A 放电读数，重连电脑并停充后约 0 A。方向与驱动/本次供电阶段一致，支持本次短时存在电池净充入；不是外接电流表或容量标定。0.5 A 是编程限值，不是实测电池净充入值。

## 结论与剩余边界

本机本哈希完成 FG/SMB5/ADC5 运行接入、停充启动、授权单次充电器插入启充、拔出停充、70/65 策略恢复、断线日志保留与重复导出 L3。设备当前仍在本次 RAM Linux，电脑 USB 在线、测试已结束并停充；未重启或刷写。

没有实测 60 秒超时路径、SOC 跨越 70/65、极端温度、真实过压/过流、watchdog 到期、I/O 故障恢复、长期满充/循环、其他电池变体、PD/BQ 快充或独立专项复核。不把本次受限单例称为全部充电保护或长期使用已验证。RISK-004 的发布/长期供电边界与 RISK-010 的重复 PMIC 问题仍开放。完整原始日志仅保留在忽略的 `.work/umi-runtime-power-device-20261108/`。
