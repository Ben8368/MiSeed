# 2026-11-10 Linux 标准接口继续完善

## 范围与固定依据

用户要求新建长期承载接口完善与后续验收的分支；`codex/linux-standard-interfaces` 基于 main `d9a65dca4a1a9530a44047273f61cabbb5f8b572`。本轮仅 Linux 内核及离线验证，Debian 集成后续进行。实施阶段未提交/推送，未操作设备/真实供电；无线 RX/TX 不在本轮。

宿主 Windows 10.0.26200 / x64 / PowerShell 7.6.9；L1/L2 使用 WSL Ubuntu-24.04 / Linux x86_64 / bash 5.2.21。锁定 Linux 6.18.60 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，以本地 bare Git 的原始文件核查 binding 与 API，不引用移动分支。本机既有有线 PD/PPS 稳定 L3 保持其原镜像证据范围。

## 接口审查与修改

| 功能 | mainline 子系统 / 消费者 | 本地差异与处置 |
| --- | --- | --- |
| 有线 BQ IRQ | TLMM pinctrl / IRQ core | `qcom,tlmm-common.yaml` 明确 input-enable=false；`pinctrl-msm.c:msm_gpio_irq_reqres()` 调用 msm_gpio_direction_input。删除 DTS 中被拒绝的属性；保留 GPIO68、边沿、mux、偏置、驱动强度和 IRQ 申请。不能拿 schema 成功代替新 DT L3 |
| 有线固定 PD/SMB5 | thermal zone/cooling device；标准 sysfs、内核 governor，Debian 后续消费 | 独立 `wired-pd-thermal` 电池温度，deci-C→milli-C；`umi-wired-pd` 二态停充。45000 m°C 来自既有 UMI_CHARGE_WARM_DECIC=450，无新增电压/电流/温度/滞回参数。框架在暖端保守停充，worker 原冷端/传感器失败/限值读回门控独立保留 |
| 有线 PPS/BQ 芯片层 | regmap、power_supply 与 TCPM；标准供电状态消费者 | 身份/standalone 检查、停泵后配置/读回、故障快照、live status 与 ADC 收拢到 bq25970.c；板级精确阈值选择、PPS 状态机/锁/所有权仍在组合驱动。新增 ADC channel 越界拒绝。私有 UAPI、保护阈值与寄存器写入顺序不变 |
| 有线 DT 验证 | dt_binding_check / dtbs_check / dt-validate | SMB5 增补 thermal-sensor/cooling cells；有线 checker 纳入 TLMM 与 thermal-sensor schema，检查 fixed PD sensor/cooling phandle、暖端值与停充档位；反例恢复非法 input-enable 必须拒绝 |

固定 PD 使用电池温度停充，不复用 PPS 六传感器虚拟模型或 FCC 档位；完整板级温控模型仍需核实。无线 RX/TX 不由该温度接口或有线 L3 推导。产品 ABI、独立通用驱动、上游接受、编译与真机分别举证。

## 锁、所有权与失败路径

thermal 请求在 SMB5 的同一把锁下更新，stop 失败仍保存 cooling_state=1 并调度 worker 重试；state=0 只移除 thermal 请求，不清用户 inhibit、不直接启充。PPS 占有 SMB5 时 cooling 回调不写共享硬件、不释放互锁；安全释放后 worker 仍受 cooling 请求约束。温度读错不伪报 0°C，转换前拒绝端点及越界值。

BQ 标准属性和通知只读 live status，锁存故障只由保护 worker 消费；配置必须先停泵并确认停止，再按原顺序写入和校验。快照后续读错时也保留已取得的故障/锁存字段，错误仍触发原有安全退出，新增真实控制器反例覆盖。未知芯片/非 standalone 模式拒绝绑定，不添加 clone fallback。芯片后端仍私有链接于同一组合模块，没有新增跨驱动符号或宣称完整通用驱动完成。

## Audit 与验证

源码修改后、完整验证前已输出 🚦 Audit Report。范围、接口设计与授权为绿；既有 8 项源码黄灯继续由 TD-008 跟踪；新行为 L3、独立专项复核与精确 HEAD CI 未闭合。验证结果随后记录于本节，不把待运行标作通过。

已执行专项：WSL 原生 C harness + Python 的 `test_kernel_thermal.py`、`test_bq25970.py`、`test_charge_policy.py`、`test_pps.py`，8 项通过、无跳过、退出 0。覆盖温度单位/端点/读取失败、热停充失败保留与重试、用户 inhibit、PPS 所有权、拔插后 thermal 请求、芯片身份/模式、故障采样逐 I/O 失败、只读状态不消费锁存、配置前停泵及 ADC 越界。

最终验证均退出 0：

| 检查 | 命令 / 结果 |
| --- | --- |
| Windows 全量 | PATH 加入 `.work/tools/w64devkit-2.11.0/w64devkit/bin` 后 `python -X utf8 -m unittest discover -s tests -v`：184 项，124 项通过、60 项因平台/DT 工具条件跳过；补齐部分故障证据保留后重跑 `test_pps.py`，4 项通过 |
| WSL 全量 | `python3 -X utf8 -m unittest discover -s tests -v`，设置下方 DTB/schema/PATH：最终 184 项全部通过、无跳过；含实际生产 C worker、芯片后端、thermal 回调与 DTB 反例 |
| ARM64 L2 | 下方环境执行 `bash tools/build_kernel_l2.sh`，完成 Image/DTB/modules/救援入口/initramfs/boot.img，新增 thermal 对象/符号与旧接口排除门禁通过；快照字段修正后增量完整重建退出 0 |
| binding | `make -C <source> O=<build> ARCH=arm64 dt_binding_check DT_SCHEMA_FILES=<scope>`：所列 schema/示例无诊断 |
| 板级 DT | `make -C <source> O=<build> ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CHECK_DTBS=y qcom/umi-csot.dtb DT_SCHEMA_FILES=<scope>`：所列范围无诊断 |
| 最终打包 DTB | `dt-validate -s <schema> -l <scope> .work/linux-standard-l2/umi-csot.dtb`：退出 0、输出为空；全量回归中的 `test_wired_dt.py` 四项实测通过，不单凭 validator/make 退出码判断 |
| 固定源码缓存 | `python3 tools/verify_kernel_source.py --repository /tmp/miseed-boot-l2/linux-stable.git --revision f45ed76d6791cf72ff1265ee93511175bbbc2f18 --source <source>`：通过，最终本地覆盖与固定原始树一致性核验完成 |
| Shell / 文本 / 来源锁 | `shellcheck --severity=error tools/build_kernel_l2.sh`、`python -X utf8 tools/check_repo.py`、`git diff --check`：通过；8 项既有源码黄灯保持，未新增超限文件 |

```sh
MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 \
MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/linux-standard-source-20261110 \
MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/linux-standard-build-20261110 \
MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/linux-standard-l2 \
bash tools/build_kernel_l2.sh
```

`<source>`、`<build>` 对应上方 EXTRACTED_SOURCE、BUILD_ROOT；`<schema>` 为 `<build>/Documentation/devicetree/bindings/processed-schema.json`。WSL 回归设置 `MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/linux-standard-l2/umi-csot.dtb`、`MISEED_DT_SCHEMA=<schema>`，并将 `/tmp/miseed-dtschema/bin` 加入 PATH。

`<scope>` 为 `power/supply/qcom,pm8150b-fg.yaml:power/supply/qcom,pm8150b-charger.yaml:power/supply/xiaomi,umi-bq25970.yaml:power/supply/battery.yaml:thermal/thermal-zones.yaml:thermal/thermal-sensor.yaml:pinctrl/qcom,sm8250-pinctrl.yaml:net/nfc/nxp,sn100t.yaml:display/simple-framebuffer.yaml`。本轮没有全量整机 schema 合规声明。

工具链为 aarch64-linux-gnu-gcc 13.3.0 / GNU make 4.3。首次 L2 有 vmlinux.a/modules.order 时间领先约 5 秒和 Clock skew 诊断；最终增量重建仍有 lib/math/modules.order 领先 5.6 秒与 Clock skew。显式 syncconfig、对象/链接符号、打包输入哈希检查均通过；不声称零警告或可复现环境闭包完成，仍归 TD-001。

| 最终产物 | SHA-256 |
| --- | --- |
| Image | `eac0e723bf7243a4085dcd8aabf440d775a984c8c6907333d3aac819dcd52d21` |
| DTB | `8c7f8455cf3b6310648971daf38861a495ad354b359dc0572f58f700f03cb8de` |
| resolved.config | `08b70762778823dac5ffdbd0c72f7516c9d69c0dc0f18c4d4d7b93f4a3634030` |
| initramfs | `a54d41ef981428464a3ea87f6c920f4252402c3f8edd11c25b4c43c6395d4265` |
| boot.img | `9f4713dbcb3b502d3f1db8a06d9ee61b6f6154935be8bfff2162baed69e32014` |

原始长日志/产物仅存忽略目录 `.work/linux-standard-*`，不提交。最初来源锁刷新脚本字段名错误，未写入文件；更正后锁校验通过。首次 schema 启动因 WSL PATH 含空格而未执行 make，改用明确引用的工具路径后重跑成功；这两次前置失败不计为通过。CONTEXT 初稿超预算，已压缩并由最终 L0 复核。

## 后续与边界

本分支继续新 ABI/DT/thermal/simpledrm 的适用 L3、保护注入/传感器失败/恢复、标度与独立复核；Debian/UEFI 集成另按后续工作包推进。独立通用 BQ 驱动、完整板级热模型、资源/PM 交接、整机 schema/运行时 DTBO 仍按 TD-004～007 与 RISK-004/010 跟踪，不以本轮局部修复关闭。

## 治理规范与提交授权

维护者随后要求明确驱动优先采用主线标准子系统/驱动模型/ABI/设备描述，原则上不引入厂商专有内核框架；标准未覆盖特性优先通用化扩展并满足上游设计/维护要求。正文集中在 ARCHITECTURE，实施前依据与评审路径归 AI_RULES，本地审查证据归 QUALITY；不把符合设计要求写成已合入上游。三份文档经仓库预算/链接及 diff 空白检查通过，未改内核输入，既有 L1/L2/schema 证据范围不变。

维护者明确要求“提交 pr”，授权本分支提交、推送并创建面向 main 的 PR。提交前远端 main 仍为上述基线，核对最终 L2 日志的 49 个本地输入哈希与当前文件全部一致。未完成的独立复核、新行为 L3 与精确 HEAD CI 在 PR 分列；该授权不包含合并、发布或设备操作。
