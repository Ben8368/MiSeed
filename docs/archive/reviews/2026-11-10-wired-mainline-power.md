# 2026-11-10 有线 power_supply / thermal / DT 迁移

## 范围与基线

用户要求依据此前对话偿还有线充电技术债，覆盖电源、电池和电量计；无线 RX/TX 不在本轮。宿主 Windows x64、PowerShell 7.6.9；L1/L2 使用 WSL Ubuntu 24.04 x86_64。分支 codex/wired-mainline-power，起点 cf68a091af0a1ca47666fc9bbbe4f8b1b4b1fa00；实施时未操作手机，提交/推送由维护者后续授权交付 PR #4。

固定 Linux 6.18.60：f45ed76d6791cf72ff1265ee93511175bbbc2f18。核对该树的 Documentation/ABI/testing/sysfs-class-power、include/linux/power_supply.h、include/linux/thermal.h、drivers/usb/typec/tcpm/tcpm.c、power-supply.yaml 和 thermal-zones.yaml。硬件保护/阈值继续来自已锁定 LOS 71b13e62f057a649b77fe4062feb73ee72ad609c；未新增猜测电压、电流、采样电阻或温度阈值。

## 实施前接口审查与落地

| 有线功能 | mainline 接口 / Debian 消费者 | 本轮变更与边界 |
| --- | --- | --- |
| FG、电池电量/电压/净电流/温度 | power_supply BATTERY；sysfs、uevent、UPower | 保留 qcom-battery 和现有 µV/µA/0.1°C/百分比；FG 净充电负号保持，未改变校准或 profile SRAM。补 external_power_changed，两个有线充电器以 supplied_to 通知电池，无反向供电 phandle/探测环 |
| SMB5 固定 PD 普通主充 | power_supply；charge_behaviour、input_current_limit | 配置 ICL 通过 input_current_limit 读写；current_max 只读 AICL。禁止请求取消诊断窗口，失败保留 inhibit；PPS 独占/移除时返回 EBUSY，普通 auto 不跳过保护 |
| USB-C PD/PPS 协商 | Type-C/TCPM power_supply ONLINE=0/1/2、VOLTAGE_NOW/CURRENT_NOW | 保留标准接口与原保护、合约校验、退回固定 PD 顺序，无 Android HAL/Binder/voter 依赖 |
| BQ25970 泵状态与禁止 | 独立 USB power_supply、标准 uevent / charge_behaviour | 注册 bq25970-charger；ADC 转 µV/µA；未配置 ADC 返回 ENODATA，尚未 ready 返回 EAGAIN，移除返回 ENODEV；不以属性读取清除故障锁存；无 APDO 的预期退出不伪报硬件故障 |
| 自动/禁止请求 | pump 和 SMB5 各自的 charge_behaviour | 泵 inhibit 跨拔插保留；auto 不清除未完成停机或故障；停泵会允许固定 PD 回退，整条路径禁止必须泵→SMB5 两步；工具报告每个失败，不能宣称跨设备原子事务 |
| 有线 PPS 温控 | thermal zone + cooling device + DT trips/maps | wired-pps-thermal 输出 m°C，umi-wired-pps 为 0–15 档；使用既有六路模型/九组滞回/16 档表，不猜新参数。最终限流是独立保护与 cooling 请求的较严者；不因 sysfs state=0 或 thermal 读取失败取消独立保护 |
| 板级拓扑 | power-supply.yaml / 本地新增 YAML | FG/SMB5 monitored-battery 指向 simple-battery；遥测使用 qcom,fuel-gauge/xiaomi,fuel-gauge；PPS power-supplies 指向 TCPM，xiaomi,switch-charger 表示并联互锁。BQ compatible 改为精确板级 xiaomi,umi-bq25970，未冒用近似芯片 |
| 用户空间 | 标准 sysfs 与 uevent | tools/wired_power.py 默认只读，明确缺失/错误；有授权才用显式 --action。rescue pps-status/pps-stop/charge-stop 也消费标准 ABI；删除 fastcharge_test。charge_test 由默认关闭的 CONFIG_MISEED_CHARGE_DIAGNOSTICS 隔离 |

这些是本地主线子系统形式的适配，不是向 kernel.org 投稿或合入的证明。新增 YAML 为本地 binding 提案；UPower 只列为标准消费者，未在 Debian 真机运行。温控模型只覆盖本次 PPS，不能推定固定 PD 的全部热策略完成，更不能推定无线 RX/TX 已支持。

## 所有权与错误路径复核

- 保留内核内部 umi_smb5_claim/check/release：先停止并隔离 SMB5 输入，泵与 TCPM 回退均确认后才释放；任何 stop/release 错误继续持有所有权。标准 power_supply 属性没有“跨两芯片原子独占并在错误后保留所有权”语义，未用非原子用户进程替代。该接口不是 Android ABI，但硬件驱动/板级策略拆分仍属 TD-004 后续。
- 用户请求与安全状态分别表示：inhibit 是持久意图；STATUS 读实际 switching；HEALTH 使用 worker 故障结果且不再次读清除型寄存器。setter 返回实际停止错误，报告成功不等于只更新一个软件标志。
- PPS 接口注册期间 ready=false，IRQ 不启动 worker；清理 action 最后注册，先阻止新请求并 drain worker，再由 devres 释放 thermal 和 power_supply。SMB5 添加对 FG 的设备寿命链接。
- 保留此前固定 LOS 保护配置及硬件风险，包括 BQ watchdog/部分比较器的既有禁用状态。本轮没有证明内核失效时的软件保护仍能执行，也没有据此开展新的主动充电。

## 验证

最终结果：L0/L1/L2 与定向 schema 全部通过；未做 L3、远端 CI 或开发者以外专项复核。

| 验证 | 实际命令 / 结果 |
| --- | --- |
| Windows L0/L1 | `python -X utf8 -m unittest discover -s tests -v`：154 项，87 项执行通过、67 项因 POSIX/C 编译器/本地 DT 工具条件跳过，退出 0 |
| WSL L1 | `python3 -m unittest discover -s tests -v`：154 项全通过，无跳过，退出 0；PATH 含 `/tmp/miseed-dtschema/bin`，MISEED_WIRED_DTB 指向本轮打包 DTB，MISEED_DT_SCHEMA 指向本轮 processed-schema.json |
| ARM64 L2 | `MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/wired-final-source-20261110 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/wired-final-build-20261110 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/wired-mainline-l2 bash tools/build_kernel_l2.sh`：退出 0，驱动/内核/DTB/rescue/boot.img 构建和既有校验通过，无编译 warning/error |
| binding | `make -C <source> O=/tmp/miseed-wired-schema-build ARCH=arm64 dt_binding_check DT_SCHEMA_FILES='power/supply/qcom,pm8150b-fg.yaml:power/supply/qcom,pm8150b-charger.yaml:power/supply/xiaomi,umi-bq25970.yaml'`：退出 0，schema、dtc 与 yamllint 无诊断 |
| 板级 schema | `make -C <source> O=<build> ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CHECK_DTBS=y qcom/umi-csot.dtb DT_SCHEMA_FILES='<上述三份>:power/supply/battery.yaml:thermal/thermal-zones.yaml:thermal/thermal-sensor.yaml'`：退出 0、无诊断；这是内核支持的单 DTB dtbs_check 模式 |
| 打包后 DTB | `tools/check_wired_dt.py --dtb .work/wired-mainline-l2/umi-csot.dtb --schema /tmp/miseed-wired-schema-build/Documentation/devicetree/bindings/processed-schema.json`：通过；核对实际 simple-battery/FG/SMB5/TCPM/泵/thermal phandle，并拒绝任何 schema 输出诊断，不单凭 make 返回码 |
| DT 反例 | tests/test_wired_dt.py 对编译 DTB 注入错误电池目标、旧 FG-as-source、4000 mA 非允许泵保护值、旧泵 monitored-battery，四种全部拒绝；删除必需 schema 也拒绝，避免未加载规则时假通过 |
| 仓库 | `python -X utf8 tools/check_repo.py`、`git diff --check`：退出 0；最终 C/DTS/config/rescue 内容哈希逐个匹配 L2 构建日志 |

`<source>`、`<build>` 分别是上表 L2 的 EXTRACTED_SOURCE 与 BUILD_ROOT。定向检查只覆盖所列有线/电池/thermal schema，未声称整机所有 DT、NFC/observer schema 通过。最初检查发现 thermal zone 名称不符，已修为 wired-pps-thermal；yamllint 的多余引号也已修正并重验。YAML 格式修正不改变 C/DTS 或生成二进制；最终 schema 内容另由锁清单和完整定向检查核对。

工具：aarch64-linux-gnu-gcc 13.3.0（Ubuntu 13.3.0-6ubuntu2~24.04.1）；dtschema 2025.8、pylibfdt 1.7.2.post2、yamllint 1.35.1。schema 工具安装在独立临时 venv；仅在 /tmp 解包 Python 开发头文件用于构建依赖，未变更系统包。传递环境闭包仍按 TD-001 管理。

本轮关键合成覆盖：实际 PPS worker/标准 get-set/thermal 回调、SMB5 标准 setter/内部互锁；I/O 停机失败保留所有权并重试、auto 拒绝未恢复故障、禁止跨拔插保持、属性读写权限/单位/通知、缺失或不支持属性、传感器失败/端点、cooling 降额、设备清理/挂起、控制台与用户空间消费者的失败路径。合成传输不能证明硬件保护或真实电气参数。

最终产物（仅本地 `.work/wired-mainline-l2`，未提交/发布/启动）：

| 文件 | SHA-256 |
| --- | --- |
| boot.img | `0e9a7fa6f6eaae20b0c0f32a44a17e13ff00ee61a663fc0b1246aacdbb90bfcf` |
| umi-csot.dtb | `5f809b376e8459872cfbe3007cac11f6ed36d781db7282d0fa6925c33c667d19` |
| 内核 Image | `8e2316dab3fc661638b6650125397be5397a0c1f75e6717774ad1dba46fc70b2` |
| rescue-initramfs.cpio.gz | `2918b962554778bf14cb73e41ba37d2c9d39610636de2ec4574abbe35c94cd68` |

本地原始输出保留在 `.work/wired-l2.log`、`.work/wired-linux-tests.log`、`.work/wired-windows-tests.log`、`.work/wired-binding-final.log`、`.work/wired-dtbs-final.log`；不把长日志提交到仓库。

## 未关闭条件

TD-004/005/007 本轮为源码/本地验证偿还，仍开放：新镜像的 PD/PPS 与标准 ABI L3、保护注入/超温/传感器失败/恢复、真实温度标度、Debian 消费、开发者以外专项复核及上游评审。固定 PD 热模型、FG profile 初始化/校准/休眠与电量精度专项不能由本轮推定完成。NFC/observer binding 例外不在此次有线范围。

既有镜像 44854093…e98a89f94c 的有线 PD/PPS 分别约 10 分钟稳定 L3 结论保持；该结论不被本轮未测项目否定，也不直接移用到本轮新镜像。
