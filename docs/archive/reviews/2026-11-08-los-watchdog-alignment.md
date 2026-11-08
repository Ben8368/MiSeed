# 2026-11-08 BQ watchdog 对齐 LOS 与诊断镜像首次真机核对

## 依据与改动

用户明确要求按本机长期使用的 LOS 实现关闭 BQ watchdog。固定来源为 [bq2597x_charger.c](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/ti/bq2597x_charger.c)，commit `71b13e62f057a649b77fe4062feb73ee72ad609c`；`bq2597x_init_device()` 调用 `bq2597x_enable_wdt(bq, false)`。源文件及寄存器头的 SHA-256 已在来源锁，不重新导入外部代码或固件。

此前 MiSeed 强制启用 5 秒 watchdog 并新增定时写入，属于本项目附加行为。把该新增机制的完整语义当成复用 LOS 的必要前提不准确，本轮纠正：

- 仅设置 0x0b 的 WATCHDOG_DIS bit2，保留频率/超时选择等其他位，与 LOS 的 update_bits 行为一致。
- 移除周期刷新写入；周期末改为只读保护复核和期限检查，不增加独立保活线程。
- 关闭位被意外清除则停止；已关闭状态下不把历史 timeout flag 判作当前 watchdog 到期。
- 不再要求验证未启用的泵 watchdog 的刷新/到期行为。关闭模式下没有该硬件超时停止保障，不能声称软件期限能中止挂死 I/O。

本轮只按明确请求改变 watchdog。未改其余电流/温控参数，未放开电源节点，未向手机写 BQ 寄存器。来源的 Umi DTS 确有 TSBAT/TSBUS=0x15、die alarm=145°C，且关闭这些片内热保护；电池 OCP 数值是注释而非有效校准，sense 的读取代码也被注释。不能再笼统说所有参数在源码里不存在，应逐项区分有效配置、关闭项、注释与本机实读。外部温控移植另见[前序记录](../research/2026-11-08-los-thermal-port.md)。

## 软件验证

宿主 Darwin 25.6.0 / arm64 / zsh；分支 `codex/umi-pd-pps-fastcharge`。源码修改后、完整回归前已输出 Audit Report。相关合成测试核对只设置 bit2、不改其他位、运行/退出不刷新、历史 timeout flag 和关闭位意外变化，以及既有逐 I/O 失败、期限和停止清理反例。

| 命令 / 环境 | 结果 |
| --- | --- |
| `python3 -m unittest discover -s tests -p test_pps.py -v` | 0，4/4 |
| macOS / `python3 -X utf8 -m unittest discover -s tests -v` | 0，95 项，79 通过 / 16 Linux 专属跳过，7.110 秒 |
| Linux 构建容器 / `python3 -m unittest discover -s tests -v` | 0，95/95，4.598 秒；首次自动审批超时，按工具提示重试一次后成功，不将超时解释为不安全 |
| `bash tools/build_kernel_l2_macos.sh` | 0，Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` / Ubuntu GCC 13.3.0；30 项工作区输入/输出哈希匹配 |

最终 boot.img SHA-256 `97a5b9f93c1038bfd95a075a9760ab74a33420f359f1535357c74ee2efe26498`。产物、配置和完整日志保留在忽略的 `.work/los-watchdog-off-final-20261108/` 并附清单；此镜像尚未上机，不与下节已启动镜像混淆。L0 检查在提交前执行。

## 已完成的 RAM 启动（watchdog 修改前镜像）

此前用户已明确要求启动镜像并配合断 USB、接充电器、重新接 USB 取日志；本次再次明确要求启动。使用标准一次 `adb reboot bootloader`，包含 Android 可能经过的 BCB/misc 路径；随后仅执行 `fastboot boot`，不刷写、格式化、挂载存储或访问身份分区。正常回退为串口 bootloader → fastboot reboot 原 Android；失联时用电源键＋音量下回 Fastboot。

| 项目 | 本机事实与范围 |
| --- | --- |
| 前置 | 唯一 umi，Android 启动完成，原内核 `4.19.325-cip131-st15-perf-g71b13e62f057`，普通 adbd；Fastboot `product=umi`、`unlocked=yes`、`is-userspace=no` |
| 已启动载荷 | `.work/los-thermal-final-20261108/boot.img`，7,806,976 B；SHA-256 `6789b4e7a58f028566c08957d6af07da330aefabab89e81adb14cd42e8c9a882`，输入对应提交 `563b8cf9ce92af0946184cdd77d38e16f16a9494` |
| 启动 | Sending / Booting 均 OKAY，Fastboot 退出 0；macOS ACM 枚举成功；串口 version 确认 Linux 6.18.60 / MiSeed / GCC 13.3.0 |
| 设备与挂载 | PDC、SPMI arb、主线 PMIC 和 PON 绑定，mode-bootloader=2；只见 rootfs、devtmpfs、proc、sysfs、只读 debugfs、configfs，没有块设备文件系统挂载；未测试 UART 电气或新镜像回退往返 |
| 遥测 | 当次 SOC=69，电池温度 33°C，电压约 4.07V；USB 输入约 5.03V；SMB5 enabled=0 / inhibit-charge，未发送启充命令 |
| PPS | 串口 pps-status 明确报告 controller unavailable；节点禁用，未进行 PD/PPS 合约或快充测试 |
| 日志 | 首份 268 条、末份 734 条，BEGIN/END 和序号连续，覆盖计数 0；忽略目录 `.work/los-thermal-ramboot-20261108/` 附 SHA-256 清单 |

早期独立打开串口时，重连提示符导致数次只读命令输出错位；没有将其作为检查成功证据。随后按命令回显复读 version/status/mounts，并以严格起止标记导出日志。没有发送任何充电控制命令。

截至本记录，手机保留在上述旧 RAM 镜像且充电抑制，尚未执行用户插拔充电器测试，也未验证新 watchdog 行为。L3 只覆盖所列镜像的启动、串口、挂载和遥测，不能扩大为 PD/PPS 可用、全部 PMIC/引脚或保护失效测试通过。
