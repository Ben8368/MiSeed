# 2026-11-08 Android root 只读供电核查

## 范围、身份与授权

| 项 | 记录 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08 17:31 起（Asia/Shanghai）；Codex 执行，维护者授权 |
| 授权 | 用户要求同步远端、切换指定分支并核实截图五项问题；明确允许在 Android 中以 adb root 获取信息 |
| 宿主 | macOS Darwin 25.6.0 / arm64 / zsh；ADB 37.0.2（16765528） |
| 仓库 | `codex/umi-pd-pps-fastcharge`，HEAD/远端均 `c7b6b487e3147b0054c02084b828f05ea26c36c2`；包含 origin/main `109225e98b25c2f0b84a3ab191346272a1a29ba4` |
| 设备匿名代号 | 本机 umi；Mi 10 / kona；面板/触控未新增采集 |
| Android | 16，Lineage `23.2-20261101-NIGHTLY-umi`；kernel `4.19.325-cip131-st15-perf-g71b13e62f057` |
| 板级字段 | `ro.boot.hwversion=2.9.1`、`ro.product.board=kona`；hardware SKU 空；DT board-id `<0x2b 0>`、msm-id `<0x164 0x20001>`；不把软件字段当作物理板号 |
| 载荷 / 回退 | 没有加载 MiSeed 镜像、重启或写入手机；采集结束 `adb unroot`，恢复普通 adbd |
| 边界 | 无刷写、分区内容读取、I2C 扫描、寄存器转储、GPIO 请求、PD 请求或供电修改；Android 自身充电策略继续运行 |

## 五项问题逐项结论

| 截图问题 | 核实结果 | 尚缺 |
| --- | --- | --- |
| 实际充电泵型号与变体 | Android 绑定 `bq2597x-charger`，I2C `4-0066`，DT `ti,bq2597x-standalone`；仅确认驱动、地址与工作模式配置 | 当前 dmesg 和 kernel logcat 已没有 detect_device / 寄存器 0x13 ID 启动记录；名称无法区分 BQ25970/SC8551/NU2105/BQ25968。MiSeed 仍只接收 ID 0x10，不得放开 clone fallback |
| 采样电阻、电池 OCP、热敏网络 | 实际 Android DT 没有 sense-resistor、bat-ocp-threshold 或 bat-ocp-alarm-threshold；存在 BAT_OCP 及三类温度保护关闭项 | 实板电阻、网络及有效保护参数仍需可信匹配资料；现有阈值不等于校准。MiSeed 强制缺项失败、不填默认值的行为已存在 |
| watchdog 与保护寄存器 | MiSeed 已有保护读回、5 秒编码、实际写刷新、故障/退出停止；源码注释仍声明真实语义待验证 | 刷新/超时停泵、VOUT_OVP 完整语义及真实故障证据未闭合；Android 读回不能替代 MiSeed 验收 |
| PMIC、GPIO 与供电路径 | 分支已有 overlay 注入隔离、UART34/35 最小掩码、PD PHY L2A 引用与 VBUS 约束；本次实际 Android DT 确认 GPIO8 输入切断与 GPIO68 泵中断 | MiSeed 仍保留 GPIO8/68，Type-C/I2C15/泵/VBUS disabled；新镜像唯一 SPMI/PON、GPIO 与 USB 往返尚无 L3，RISK-010/TD-002 不关闭 |
| MiSeed 实际快充与稳定性 | 当前 HEAD CI/L1/L2 有证据；旧镜像短时 0.5 A L3 不覆盖本分支 PPS | 新镜像身份/保护读回、PD/PPS、有界充电、拔线/停止、60/600 秒到期、SOC、故障保护及独立专项复核未完成 |

**结论：五项没有全部修复完善；第四项已取得明确源码修复和额外 Android 接线证据，其余仍有运行准入阻断。优先补充匹配资料和既有启动身份记录，本轮证据不足以要求拆机，也不足以启泵。**

## Android 只读观测

- 泵节点：`/sys/firmware/devicetree/base/soc/i2c@884000/bq25970-standalone@66`。`reg=<0x66>`、`interrupts=<68 0x2002>`；`pinctrl-0=<0x5cb>` 匹配 `bq2597x_master_int_default`，其 mux/config 均为 gpio68、gpio function、drive-strength 2、pull-up/input-enable。名字含 master 不代表本机泵工作在 master 模式。
- SMB5 的 `vbus-disable-gpio=<0x65 8 0>`；同一 TLMM 引用，GPIO8 active-high。这是当前 DT 配置证据，不是实测电气动作或完整安全引脚范围。
- Android 泵 DT 的阈值：BAT_OVP 4550 / alarm 4525、BUS_OVP 12000 / alarm 11000、BUS_OCP 3750 / alarm 3500、AC_OVP 14；原属性单位和含义仍以固定驱动为准。
- `bat-ocp-disable`、`bat-ocp-alarm-disable`、`bat-therm-disable`、`bus-therm-disable`、`die-therm-disable` 均实际存在；热敏阈值字段为 0x15/0x15，die 为 145。关闭保护时这些字段的存在不能证明保护有效，更不能用于推断实际电阻网络。
- 缓存 `src_cap_id` 前后均为 2，PDO 只有一个 fixed 5000 mV / 3000 mA，支持 USB communications / DRP / data-role swap；无 APDO。仅代表当前 USB 连接电源的缓存，不代表其他充电器、不证明实际交付 15 W 或 PPS 不受支持。
- 状态快照：battery `Charging` / `Good`、SOC 48%、温度 340（0.1°C）；USB `USB_PD`、voltage_now 4597314 µV；`usb/current_now` 文件不存在。该电压读数不替代 PD 合约或外接表校准。日志中泵 charging_enabled 为 disable，仅为 Android 当时状态。
- dmesg 可读，过滤匹配到 369 条泵相关行后仅保留末尾 12 条；没有身份启动记录。kernel logcat 检查 10828 行也未找到 ID，不保存完整日志。不能把“没找到”解释为芯片不存在。

## 命令、退出码与证据保留

ADB 路径为 `.work/host-tools/platform-tools/adb`；下列 `<serial>` 为已确认的单一 umi，公开记录脱敏。实际采集的 JSON 仅保存在被忽略的 `local/android-root-*-20261108.json`，不提交设备标识、原厂分区、镜像或长原始日志。

用户随后明确要求单独归档并提交 PR：四份本地 JSON 已另存到忽略的 `local/archives/20261108-android-root-power/`，附 SHA-256 清单；脱敏、可复核的[结构化观测快照](2026-11-08-android-root-power-observations.json)随本记录进入 PR。该快照保留状态、原始 DT 属性字节和缓存 PDO，排除分区列表、固件指纹、标识及日志正文；未取得的 ID/校准仍为未取得。

| 命令 / 查询 | 实际结果 |
| --- | --- |
| `git fetch origin --prune`；`git switch --track origin/codex/umi-pd-pps-fastcharge` | 均 0；工作区初始干净，切换后精确跟踪远端；沙箱首次 switch 拒绝创建 index.lock，授权范围内重试成功 |
| `adb devices -l`；`adb -s <serial> shell getprop ro.product.device`；`adb -s <serial> root`；`adb -s <serial> shell id` | 均 0；单一 umi，root 后 uid=0；首次沙箱内启动 ADB listener 被拒，提权执行宿主命令后成功 |
| `PATH="$PWD/.work/host-tools/platform-tools:$PATH" python3 -X utf8 tools/collect_device.py --serial <serial> --power --output local/android-root-power-20261108.json` | 1 / PARTIAL；缺失 bootloader/slot/locked/SKU、泵 ID，不将其标记为完整 |
| `adb -s <serial> shell dmesg`；`adb -s <serial> shell logcat -b kernel -d` | 均 0；仅在主机内存过滤泵/身份记录，不保存完整原始输出 |
| `adb ... shell ls /sys/class/power_supply`；`find /sys/firmware/devicetree/base -iname '*bq*' -o -iname '*2597*' -o -iname '*sc8551*' -o -iname '*nu2105*'` | 均 0；发现绑定设备与实际 DT 节点 |
| `adb ... shell 'for d in /sys/bus/i2c/devices/*-0066; do echo "$d"; cat "$d/name"; readlink "$d/driver"; readlink -f "$d/of_node"; done'` | 修正 shell 参数引用后 0；首次嵌套 `sh -c` 未引用完整脚本导致语法错误 1，无设备写入 |
| `ls -1 <pump-DT-node>`，逐属性 `cat <pump-DT-node>/<property>`；`cat <DT-base>/qcom,board-id`、`qcom,msm-id`；`find <pinctrl-state> -maxdepth 3 -type f` 后读取 mux/config 属性 | 均 0；首次把 pin 属性当成 state 直属属性读取为 1，定位 mux/config 后成功；未把缺失属性读成零 |
| `cat /sys/class/power_supply/{battery/status,battery/health,battery/capacity,battery/temp,usb/real_type,usb/voltage_now,usb/current_now}`（实际逐项执行） | 前六项 0；current_now 缺失 1，组合查询为部分结果 |
| `adb -s <serial> unroot`；`adb -s <serial> shell id`；`adb -s <serial> shell getprop sys.boot_completed` | 均 0；uid=2000(shell)、boot_completed=1，仍在 Android |

固定 Lineage 源码经 GitHub connector 重读 [bq2597x_charger.c](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/ti/bq2597x_charger.c)：detect_device 读取 0x13 后以 `pr_err` 输出十进制 ID，MODEL_NAME 仅表示工作模式；registers sysfs/debugfs 会遍历含读清除位的寄存器，故本轮未使用。来源仍为既有锁，无外部代码导入。Android 版本与 uname 匹配来源只是身份线索，不证明运行二进制逐字节等于源码。

## 验证

- 核查前已输出 Audit Report：RISK-004 仍阻断快充，RISK-010/TD-002 待新镜像 L3；没有源码修改。
- `python3 -X utf8 tools/check_repo.py`：0；`python3 -X utf8 -m unittest discover -s tests -v`：0，95 项 / 79 通过 / 16 项 Linux Bash/GCC/伪终端相关跳过，耗时 10.023 秒。真实 C 控制器/SMB5 合成回归和 5 项 libfdt overlay 正反例均通过；不模拟为硬件验收。
- 文档更新后首次 L0 检查发现 platforms 超出 3500 字符预算，压缩重复描述后重验；最终 `tools/check_repo.py` 与 `git diff --check` 均 0。
- [CI run 42467014947](https://github.com/Ben8368/MiSeed/actions/runs/42467014947) 对应精确 HEAD `c7b6b487e3147b0054c02084b828f05ea26c36c2`：completed/success；Boot L2、PR commits、Ubuntu Python 3.11/3.14、Windows Python 3.14、macOS Python 3.14 六项全部 success。PR #3 仍 draft/open，base 为上述 main。未在本轮另做本地 L2。
- 本次 L3 仅为 Android 只读身份/设备树/缓存/状态观测；没有 MiSeed 新镜像、PPS、超时或保护故障 L3。没有提交、推送、发布或合并。
