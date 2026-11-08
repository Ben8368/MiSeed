# 2026-11-08 PPS 超时加固、泵身份与温控续核

## 用户请求与本轮边界

用户要求先独立归档采集信息并提交 PR，再修复快充，完成后 RAM 启动、配合插拔充电器和回收日志。随后补充电源为“绿联45W屏显充电器”，并要求优先复用公开源码及他人真机验证；设备连接短暂消失后，用户明确回复“已接回，继续只读采集”。原有 adb root 信息读取授权继续用于定点核查，未扩为主动供电操作。

宿主 macOS Darwin 25.6.0 / arm64 / zsh，工作分支 `codex/umi-pd-pps-fastcharge`。采集归档提交为 `b88e81de47f609faf983245a67f1d1cb443e3f0f`，已推送 [PR #3](https://github.com/Ben8368/MiSeed/pull/3)。最初四份原始 JSON 另存忽略的 `local/archives/20261108-android-root-power/` 并附哈希；公开摘要和结构化快照见[初次核查](../hardware/2026-11-08-android-root-power-audit.md)。PR 更新后新增 2811 行，未合并。

本轮没有启动 MiSeed 镜像、刷写、块设备访问、GPIO 操作、I2C 扫描、寄存器转储或充电控制。只进行了定点身份/配置读取及 Android 温控文件/元数据读取；root 完成后恢复普通 adbd。Android 自身继续管理供电。

## 已确认的本机信息

公开、脱敏的[后续观测快照](../hardware/2026-11-08-pump-id-thermal-observations.json)保留原始字节、状态和文件哈希；五份本地采集另存 `local/archives/20261108-pump-id-thermal/` 并附 SHA-256 清单，不覆盖初次快照。vendor 配置正文/二进制不提交。

| 观测 | 结论与限制 |
| --- | --- |
| `i2cget -f -y 4 0x66 0x13` → `0x10`，退出 0 | 执行前以 getprop、I2C name 和 of_node 确认唯一 umi 的 `4-0066` 绑定为既有 bq2597x 节点。完整 ID 匹配当前 BQ25970 端口；芯片变体身份不再仅依赖名称/已滚动日志。`-f` 只越过已绑定检查，没有解绑或写配置 |
| `i2cget -f -y 4 0x66 0x2b` → `0xd0`，退出 0 | 固定 Lineage 头文件的 sense bit1=0 对应 2 mΩ 选择；这是当前 Android 配置，不是实物电阻或 ADC 精度测量，未将其写成已合格板级校准 |
| PM8150B VADC/ADC_TM `conn_therm@4f` | 实际 DT 为 channel 0x4f、pre-scaling `<1 1>`、ratiometric、settle 200；conn/quiet 热区 governor 为 `user_space`，并非 BQ TSBUS/TSBAT 引脚的接线证据 |
| `thermald-devices.conf` | battery 动作通过 cooling_name `battery`；本机 cooling_device15 的 type=battery、cur_state=0、max_state=16，均退出 0；说明可追踪原系统执行链，不证明 MiSeed 已具备该链 |
| 温度快照 | conn=37963、quiet=43935、cam=41706、xo=46469、battery=35500（Android 热区毫摄氏度）；只读单次观测，不作传感器精度/保护验收 |
| vendor 配置 | `thermal-engine.conf` 是默认空配置（84 B）；`thermal-chg-only.conf` 为可读文本；`thermal-normal.conf` 为非 UTF-8 数据（3104 B），随后已离线解码并完成逐字节重加密一致性核对，不把关机充电配置套为常规策略 |

四份 vendor 文件均 `cat` 退出 0。normal 原始字节 SHA-256 为 `436a49a83321a3f2bcca7c556031805801a548869fb91e1124d0baf448f3868e`；最初探索性读取使用替代解码，随后改为二进制读取并重新计算哈希，公开快照只采用后者。普通 shell 读取 DT 被拒后，用已授权 root 重读；权限错误不作空/零属性解释。缺失的 battery charge_control_limit/system_temp_level 等属性记 rc=1，不推断为未限流。

### 常规温控离线解析

继续公开资料检索找到 [mi-thermal-py-crypt 的格式实现](https://github.com/aufarakha/mi-thermal-py-crypt/blob/200dc81c8498a4013cba13fdbf6e01de27220f01/thermal-crypt.py)，固定 commit `200dc81c8498a4013cba13fdbf6e01de27220f01` / blob `a51c98ab2311c3bf87fad533bd7117ecde604e33`。只阅读其 AES-128-CBC/PKCS7 格式说明，没有下载执行该脚本或复制代码进入仓库；使用宿主 `/usr/bin/openssl enc` 对本地采集字节离线解码并反向编码，输出与原文件逐字节一致。明文为 3101 B、SHA-256 `f144b75723fea08d8a67184bae927fb09c97e35ca0b0dc2bb11bec10294523f5`，仅存忽略目录及归档副本，没有改写手机任何配置。

常规 VIRTUAL-SENSOR 使用 battery/cam_therm1/conn_therm/quiet_therm/wireless_therm/xo_therm，权重 223/150/440/−143/115/149，weight_sum=1000，compensation=4541。MONITOR-BAT 轮询 2000 ms，trig 为 41000/43000/45000/46000/47000/48000/49000/50000/63000，clr 为 38000/41000/43000/45000/46000/47000/48000/49000/51000，target 为 803/1106/1206/1206/1209/1410/1512/1514/1515。上述是原配置数字，不能把虚拟温度当作单个电池温度，也不能把动作编码当作毫安。

进一步读取固定 Lineage [smb5-lib.c](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/qcom/smb5-lib.c)（blob `6372dab5db184e24251122e77a9dd15930e40024`）：系统 thermal level 在 PPS 下通过工作队列更新 `thermal_fcc_pps_cp`，随后进入 FCC/ICL 投票。守护进程如何拆解上述 target、运行时是否存在覆盖配置/有效守护进程尚未确认；第二次 ADB 列表为空时停止了这些后续读取。不能将解析成功扩大为当前 Android 或 MiSeed 完整保护链已验收。

ADB 采集中途一次列表为空，身份检查失败即停止；用户接回后重新核实 umi 才继续。定点寄存器命令仅访问驱动用于识别的 0x13 和无读清除标志的配置 0x2b，未读取带清除语义的告警/故障寄存器。此方法需要人工核对绑定与目标，不加入通用自动采集器或默认命令。

## 公开实现如何复用

不能从“前几个参考缺项”推导“公开资料不存在”。本轮扩大查询并核实如下来源；没有将外部代码或 vendor 二进制导入构建。

| 来源 / 固定身份 | 得到的证据 / 本次采用 |
| --- | --- |
| [Lineage BQ 头文件](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/ti/bq25970_reg.h)，既有锁 `71b13e62…` | 完整 BQ25970 ID 为 0x10；sense 选择可核对。VOUT_OVP 字段名与 enable 编码仍需完整语义核对，不能仅凭宏名决定极性 |
| [Lineage Umi 设备文件清单](https://github.com/LineageOS/android_device_xiaomi_umi/blob/8e115b1ed63caa3ee5c3d52df0a7bce7c27512cd/proprietary-files.txt)，`8e115b1e…` | 明确列出多份 vendor thermal 配置；结合本机 cooling/ADC 观测，后续需复用完整传感器—策略—执行链，不能把 pump DT 的禁用保护解释为完整充电系统不含温控 |
| [surya BQ25970 补丁](https://github.com/woodyst/surya-pmos/blob/d7eadb03f230506140839f18a6c0ef3c43016d7e/kernel/0002-power-supply-add-TI-BQ25970-switched-capacitor-charg.patch)，`d7eadb03…`，blob `f1d76053…` | 开发者报告该 POCO 设备充电可用；补丁 hw_init 仍关闭 watchdog 和三类片内温控，README 说明高功率私有握手未实现。可用于寄存器布局交叉检查，不能用它关闭 Umi 的保护缺项或把另一板型参数直接采用 |
| [同项目提高电流补丁](https://github.com/woodyst/surya-pmos/blob/d7eadb03f230506140839f18a6c0ef3c43016d7e/kernel/0003-arm64-dts-qcom-sm7150-xiaomi-common-raise-max-charge.patch)，blob `7b37c61f…` | 提交正文明确承认主线热管理尚缺、提高电流减少余量；不采用该取舍 |
| [TI SLUSD72B](https://www.ti.com/lit/ds/symlink/bq25970.pdf) | 实际打开的是 13 页公开简版，没有完整 watchdog 寄存器/计时器解释。搜索遇到以 BQ25970 命名却展示 BQ25960/BQ25968 的资料，不用异型号正文替代本芯片证据 |
| [绿联屏显 45W 官方页面](https://www.ugreen.com/en-ae/products/ae-25422) | 相近型号声明 PPS；尚未确认与用户实物 SKU 一致，不指定其 APDO，更不把此前电脑 5 V/3 A 缓存当作这只充电器 |

下一步应以已确认 ID 0x10 为基础，核实实际采样校准、BQ/PMIC 硬件失效停止语义，以及原系统外部温控如何作用于 PPS。若匹配资料证明某片内热敏引脚未接，应审查实际替代保护拓扑，不能凭空补热敏参数，也不能把关闭检查直接等同于保护闭合。公开源码和他人真机经验可复用；仍需确认适用硬件、有效配置和完整依赖。

## 本地软件修复

发现旧 PPS worker 在 I/O 前读取时间，随后保护读取/PD 请求可阻塞；跨过 5 秒 SETTLE 或 60/600 秒期限后，仍可能继续主动操作。先加入真实 C 控制器延迟反例，在未修复源码上 `test_real_controller_success_faults_deadlines_and_cleanup` 失败，断言显示超过 SETTLE 期限仍启泵。

`umi_pps.c` 追加边界时间检查：保护准备后激活 PPS 前、激活后、电压/电流请求之间及返回后、watchdog 验证后/刷新前后、启泵前后检查当前会话/SETTLE 期限。超期走原停泵/固定合约清理，清理不受期限阻断；已经提交的挂死事务不能靠软件时钟取消，硬件保护仍不可替代。

合成反例覆盖慢初始化后不激活 PPS、读取跨 SETTLE 后不启泵/刷新、主会话过期后不发新电压/电流请求、单次电压请求跨期后不再发电流请求、保护验证跨期后不刷新。原逐 I/O 故障、部分停止失败和所有其他门控继续验证；不改板级参数，不启用任何禁用电源节点。

## 验证与当前准入

源码修改后、完整验证前已输出 Audit：软件跨期问题可修复；硬件校准/保护与新拓扑 L3 仍为红色阻断项。

| 环境 / 命令 | 结果 |
| --- | --- |
| macOS / `python3 -X utf8 -m unittest discover -s tests -v` | 0，95 项，79 通过 / 16 Linux 专属跳过，5.649 秒 |
| Linux 容器 / `docker --context colima-miseed-boot-l2 run --rm --mount type=bind,source=<repo>,target=/workspace --workdir /workspace miseed-boot-l2:local python3 -m unittest discover -s tests -v` | 0，95 项全过，5.495 秒 |
| ARM64 L2 / `bash tools/build_kernel_l2_macos.sh` | 0，固定 Linux `f45ed76d…`、Ubuntu GCC 13.3.0；含真实 libfdt 正反例，30 项输出/输入哈希与当前工作区匹配 |
| 文档/仓库 / `python3 -X utf8 tools/check_repo.py`、`git diff --check` | 适用检查最终重验，结果见本轮汇报 |
| 已推送归档 HEAD 的 [CI 42468282927](https://github.com/Ben8368/MiSeed/actions/runs/42468282927) | `b88e81de47f609faf983245a67f1d1cb443e3f0f` 六项 completed/success；追加软件修复的远端 CI 单独核对 |

最终 driver SHA-256：`399b192fc3d0fd8c373ef31a68578146ebfd7d83a270c8737965d5d84e35ce29`。
最终 boot.img SHA-256：`fa72db0f2e4c4e23ff06e0e6ef3a9b19f14bd18ca2d5a8c9fca503cef72beb71`（7,806,976 B）。
DTB SHA-256：`fa8de9f861f5d4cf5385f2667db9e50d019ec37cc16be973818cc37b814a7b13`。
镜像、配置和已核对哈希另存忽略的 `.work/pps-deadline-final-20261108/`；没有将构建产物提交。

本轮推进了实际芯片身份、现用 sense 配置和原温控路径的可见性，不能声称快充已实现可用。任务 2 尚未完成，任务 3 的启动/充电/回收日志尚未开始；不会将用户测试授权替代 RISK-004 的保护证据。独立专项复核和新 MiSeed L3 均未完成，保持草稿，不合并。
