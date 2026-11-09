# 2026-11-08 LOS 常规温控执行链核对与移植

## 请求、来源与边界

对应 RQ-12 / WP9。用户要求复用长期在本机使用的 LOS 内核及已知信息，继续实现并启用快充；设备接回后继续使用既有 adb root 只读授权。宿主 Darwin 25.6.0 / arm64 / zsh，分支 `codex/umi-pd-pps-fastcharge`，沿用[草稿 PR #3](https://github.com/Ben8368/MiSeed/pull/3)。

本轮补齐此前未知的动作编码及软件执行链，移植到主线 PPS 控制器；没有启动 MiSeed、刷写、块设备访问、改手机温控配置、修改供电或启泵。采集结束恢复普通 adbd。原始元数据、vendor 二进制和离线分析另存忽略的 `local/archives/20261108-los-thermal-runtime/`，七份文件附 SHA-256 清单；Git 只收[脱敏事实快照](../hardware/2026-11-08-los-thermal-runtime-observations.json)及本记录。旧证据不改写，前序见[泵身份与温控续核](../reviews/2026-11-08-pps-deadline-and-protection-review.md)。

来源仍固定 [LineageOS android_kernel_xiaomi_sm8250](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/tree/71b13e62f057a649b77fe4062feb73ee72ad609c)，commit `71b13e62f057a649b77fe4062feb73ee72ad609c`。补充审读 `drivers/power/supply/qcom/smb5-lib.c`，blob `6372dab5db184e24251122e77a9dd15930e40024`，SHA-256 `8f542b9679a95da4c5a1bdf1d622ea4e06ab31345b158c9a68bb7d7ff9edeb68`，GPL-2.0-only / The Linux Foundation 2018–2021。逐文件记录进入来源锁；原贡献者归属保留在 [NOTICE](../../../kernel/power/NOTICE.md)。

选择本机匹配 LOS 而非其他手机的保护关闭配置：已对应实际运行内核、Umi DTS、传感器和服务，可以核查软件依赖。Android 私有 power_supply、投票器和闭源守护进程不直接搬入主线；温控公式/数表作为观测事实，重新实现主线接口和失败停止。vendor 二进制只在宿主离线解析，没有执行、复制其代码进入产品或再分发。

## 本机只读与离线证据

Android `umi`，内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。普通权限首次读取 override/daemon 元数据失败，后用已授权 root 核实，不把权限错误解释为文件不存在。

| 项目 | 结果与证据范围 |
| --- | --- |
| 服务与 profile | `mi_thermald`、`android.hardware.thermal-service.qti` 运行，`init.svc.mi_thermald=running`，thermal sconfig=0；root 列出 `/data/vendor/thermal/config` 无配置文件。这些与常规 vendor profile 对应，但本次没有持续记录每次动作执行 |
| 守护进程 | `/vendor/bin/mi_thermald`，101688 B，SHA-256 `5c942849b198416cf40d096de9faed0c6ca4dba552173c61274659d365743e3b`；设备与宿主哈希相同 |
| 离线方法 | Python lzma 解析 `.gnu_debugdata`，现有 Linux 容器内 GNU readelf/objdump 读取 AArch64 符号/反汇编；不在手机附加进程，不执行 vendor 程序 |
| `generic_set_level` | 虚拟地址 `0xe7d8`，512 B；`0xe820` 起识别 `battery` 等名称；`0xe8a4`–`0xe8c4` 的乘法/移位/余数和条件选择还原百进制双动作 |
| 屏幕输入 | `screen_monitor` 地址 `0xe9d8`，读取 `/sys/class/thermal/thermal_message/screen_state` 并 atoi 到全局 `0x1896c`；本次输入 0。不擅自把零/非零解释为某种物理亮灭状态 |
| 虚拟温度 | `virtual_read_temp` 地址 `0xc11c`，至 `0xcdc4` 实现加权和/除数/配置补偿及附加 board compensation |
| 附加补偿 | `board_sensor_temp_comp_thread` 地址 `0x7cd8` 读取 `board_sensor_temp_comp`；本机节点不存在，对应 BSS 默认 0；本端口只接受该补偿值为 0，不外推其他设备 |
| 日志限制 | 当次 `logcat -d -s mi_thermald` 返回 0 行，不声称捕获了实际降额事件 |

对于 target < 100，直接作为档位；target ≥ 100 时，screen_state 非零取 `target / 100`，为零取 `target % 100`。例如 803 对应 8/3，1106 对应 11/6；不是 803 mA 或百分比。

常规配置原始/明文哈希及往返校验在前序记录。本端口沿用其六路顺序：battery、cam_therm1、conn_therm、quiet_therm、wireless_therm、xo_therm；权重 223、150、440、−143、115、149，除数 1000、补偿 4541 毫摄氏度。九档 trigger/clear/target 保留。固定 Umi DTS 的 PPS 电池 FCC 表（mA）为 6000、5700、5475、5250、5025、4800、4575、4350、4100、3500、3000、2400、2000、1500、1000、500。

固定 `smb5-lib.c` 将系统 thermal level 经 PPS thermal work 更新 `thermal_fcc_pps_cp` 并参与 FCC 投票；源实现有约 3 秒延迟和逐步变化。此确认动作到电流表的源码链，不证明 MiSeed 主线驱动或实际电池电流已按该表工作。

## 本地实现与差异

`umi_pps_policy.h` 实现六路加权虚拟温度、九档滞回、双动作解码和 16 档 FCC 上限，使用 64 位加权和。MiSeed 当前没有已审查的屏幕状态提供器，采用两种动作中较严格的档位。补偿只接受已观测的 0。

`umi-pps.dtsi` 补齐 PMIC ADC provider/channel 和 PPS 命名 IIO 引用：

| 输入 | 主线 provider / channel | 配置 |
| --- | --- | --- |
| cam_therm1 | PM8150L / 0x4d | ADC5 AMUX_THM1 100K pull-up |
| conn_therm | PM8150B / 0x4f | ADC5 AMUX_THM3 100K pull-up |
| quiet_therm | PM8150 / 0x4d | ADC5 AMUX_THM1 100K pull-up |
| wireless_therm | PM8150B / 0x4d | ADC5 AMUX_THM1 100K pull-up |
| xo_therm | PM8150L / 0x4e | ADC5 AMUX_THM2 100K pull-up |

接线与 `pre-scaling=<1 1>`、ratiometric、settle 200 来自实际 Android DT 和固定内核支持，不把 PMIC 热敏输入等同于 BQ TSBAT/TSBUS 引脚。固定主线 ADC5 的 `IIO_TEMP` processed 单位为毫摄氏度；整组数据在主线上的实际标度仍需 L3。

`umi_pps.c` probe 要求五路 IIO 输入均为温度类型；每次循环取得 BMS 电池温度和五路 PMIC 温度，整组读取超过 200 ms、读取错误或触及 ADC 温度端点立即拒绝继续。降低温控上限后先限制 PD 电流；本会话不再抬升该电流，取消源实现的延迟降额。BMS 或泵实测电流已超新限值则立即停止；温控限制低于现有泵工作范围也停止，不自行定义新的低功率工作模式。

增加 0x2b bit3 原生值不变量：本机读到 `0xd0`，此位为 0；配置掩码不写 bit3，读回翻转则退出。没有根据矛盾宏名猜测 VOUT_OVP 极性，没有声称该不变量证明全部硬件语义。Type-C、I2C15、泵、VBUS 仍 disabled，GPIO 保留范围不变，采样/热敏/OCP 校准仍不填默认值。

## 验证

源码修改后、完整验证前已输出 🚦 Audit Report：温控软件路径可移植；硬件保护/校准和新拓扑 L3 仍是红色阻断项。未进行主动充电。最终源码校验与 L2 日志中的全部 30 项工作区输入/输出哈希匹配。

| 环境 / 命令 | 结果 |
| --- | --- |
| macOS / `python3 -m unittest discover -s tests -p test_pps.py -v` | 0，4 项全部通过，编译并运行真实控制器 C harness |
| macOS / `python3 -X utf8 -m unittest discover -s tests -v` | 0，95 项，79 通过 / 16 Linux 专属跳过，7.212 秒 |
| Linux 容器 / `python3 -m unittest discover -s tests -v` | 0，95 项全部通过，4.993 秒；容器 `miseed-boot-l2:local`、仓库绑定到 `/workspace` |
| ARM64 L2 / `bash tools/build_kernel_l2_macos.sh` | 0，Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、Ubuntu GCC 13.3.0；真实 libfdt 正反例通过 |
| 最终 DTB / `fdtget` | 五组 provider phandle、channel、名称和 ADC 地址逐项吻合；这只是编译后连接检查 |
| L0 / `python3 -X utf8 tools/check_repo.py`、`git diff --check` | 均为 0；最终文档、链接、预算和来源锁检查通过 |

新增合成反例包括：803 的两种状态和未知状态、非法动作、虚拟温度/滞回、无效补偿、ADC 端点、逐 IIO 失败、整组读取超时、运行时热降额、冷却后不抬升、过低温控限值退出、0x2b bit3 翻转不继续刷新 watchdog。原逐 I/O 故障及超期/清理失败继续覆盖。没有用合成电阻/温控参数填实机 DTS。

最终产物只存 `.work/los-thermal-final-20261108/`，附输入/输出和测试日志及 SHA-256 清单：

- boot.img：`6789b4e7a58f028566c08957d6af07da330aefabab89e81adb14cd42e8c9a882`。
- umi-csot.dtb：`0d798ffa54184fdfef3953f21c58694b439b0b8002692a632f1c57df85cd8fff`。
- umi_pps.c：`32be04ee97f93ffe8a6551b58a13ad40d0de2f105899f92e8bcdc3cb699b086e`。
- umi_pps_policy.h：`8ed5d0eb619d55633012da9e4e1413f6a3e87ef6849a9c7d1c698f1a63cf338e`。

## 准入与剩余条件

动作编码、常规配置和对应 FCC 表的源码执行链不再列为未知。温控软件已经 L1/L2，尚未有主线 ADC 标度/温控执行的 MiSeed L3；这不能宣布快充可用。

RISK-004 仍包括实板采样/热敏和电池 OCP 参数、watchdog 刷新/到期/挂死后的停止效果、VOUT_OVP 完整语义及故障动作。2026-11-08 复核 [TI 产品页](https://www.ti.com/product/BQ25970)仍将完整资料列为申请获取；公开 [SLUAA33A 应用说明](https://www.ti.com/lit/pdf/SLUAA33)解释 PPS 闭环和功率级，没有给出完整 watchdog 寄存器语义。不能用搜索中误标成 BQ25970 的 BQ25960/68 正文代替。源 LOS 自身关闭部分片内保护，不能直接把关闭项移植为保护已经闭合。

还需开发者以外专项复核，RISK-010 / TD-002 的新 PMIC/TLMM/USB 往返和温控标度 L3，再按已有授权进行保护与有界 PPS 测试。当前不启动快充镜像、不要求用户拔线插充电器；任务 2 的真实快充可用和任务 3 的充电日志回收仍未完成。
