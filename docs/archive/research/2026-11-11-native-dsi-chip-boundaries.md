# 2026-11-11 原生显示、运行时 DT 与有线芯片边界

## 实施前接口审查

- WP2 显示：固定 Linux 6.18.60 MSM/DPU、MIPI DSI、DRM panel/DSC；消费者为 KMS/fbdev、标准 backlight。固定 CSOT 数据置于驱动，不导入下游通用命令 DT ABI、任意寄存器 sysfs 或 Android 服务。
- 主线尚无已确认的 Umi CSOT binding，不能冒用近似芯片 compatible；采用固定 colorgarden 提交的 BSD 板级数据，保留来源。独立电源轨、物理尺寸单位、本机完整变体身份待核实。
- WP2 DT：保留符号隔离，补最终 FDT 拓扑审核和直接路径/数值目标反例。拒绝异常不等于阻止 bootloader 修改；不盲删 PMIC 或重排 phandle。
- WP9 有线：独立 BQ25970 I2C/IRQ/寄存器提供者，板级 PPS 通过 auxiliary bus 管理其充电功能。保留 power_supply/thermal、TCPM、SMB5 独占事务；无线 RX/TX 不在范围内。
- 固定源码依据：DRM panel/DSC、auxiliary_bus.rst、power_supply_class.rst；binding 与拆分需 DRM/power_supply 子系统评审，不声明 upstream 接受。

## 授权与验证边界

本轮为源码实施和 watchdog 评估，未执行危险充电、刷写或保护参数修改。
watchdog 与阈值保持原值；原生显示单独构建，不替换已验证救援镜像。
L1/L2 与 L3 分列，不继承参考项目硬件结论。

## 实现与审查结论

1. 显示：固定 1080×2340@60、四 lane、DSC 1.1（540×12，8bpc/8bpp），保留来源中的 59 条命令及等待；无参数短写剥离填充。设置 `prepare_prev_first`，确保上游 DSI 桥先准备再发面板命令；DRM panel/标准 backlight 支持亮度 0，命令失败或短写则复位并报错。启用 DSI0/7nm PHY，L9a 的父供电按来源接 S8c，GPIO 掩码只额外开放 GPIO12。默认仍为 simpledrm，`MISEED_NATIVE_DSI=1` 必须明确输出目录。面板独立电源轨/冷启动与具体 CSOT 变体未验，不声明原生显示已在本机点亮。
2. DT：既有符号隔离保留主线 phandle；`check_runtime_dt.py` 检查最终 FDT 中 live soc 直属拓扑、SPMI/TLMM 子树、隔离区、别名、phandle 唯一性及非零 RAM。rescue `dtb` 只读导出 `/sys/firmware/fdt`，先完整读取再输出。新增真实 fdtoverlay 正例及 target-path/数值 phandle 绕过反例。该审计只能检测异常，不阻止 ABL 合并，也不证明全机内存图安全；本机 DTBO、最终 FDT 和 SPMI 绑定仍须 L3，RISK-010 保持开放。
3. 有线：`bq25970_core` 独立编译，拥有 I2C/regmap、完整芯片 ID、初始停泵及 IRQ，提供 auxiliary `bq25970_core.charger`；`umi-bq25970` 只保留板级 PPS/TCPM/thermal 策略。当前仍仅匹配已审查的 Umi compatible，不宣称其他板型或兼容芯片通用支持。IRQ 不读取锁存故障，仍由策略唯一串行读取；无消费者时 IRQ 尝试停泵。清理先设置 removing/ready、排空工作并走原停机事务，最后退订回调；等待回调时不持策略锁。子设备先解绑，随后释放 IRQ，最后释放 provider，regmap 在此期间存活。
4. 安全互锁：`umi_smb5_claim/check/release` 与 `umi_pps_stop` 保留。顺序仍为停泵并确认 → 固定 PD 合约并确认 → 释放 SMB5；失败保留独占。没有把这个跨芯片事务改成用户态分步写入。电气阈值、watchdog、无线 RX/TX 均未因拆分改变；新生命周期需真机重验，既有有线稳定/旁路供电 L3 结论保留。

## Watchdog 评估：有线 BQ25970

核对固定 LOS `71b13e62f057a649b77fe4062feb73ee72ad609c` 的 `drivers/power/supply/ti/bq25970_reg.h` 与 `bq2597x_charger.c`：0x0b bit2 禁用、bit3 timeout 标志、bits1:0 编码 0.5/1/5/30 秒；参考初始化关闭 watchdog。这些源码证明寄存器用法，不能替代芯片到期行为规格或真机证据。公开资料检索未取得足以确认完整到期/恢复语义的资料，不以 BQ25980/BQ25968 等型号推断。

潜在收益是控制循环/CPU/I2C 失去进展后，由独立硬件超时进入受控状态；只有确认到期确实停泵且恢复安全后，这项收益才成立。它不能替代 OVP/OCP/OTP、板级温控、PD 合约检查或 SMB5 所有权。

风险与需要的证据：

- 调度延迟、I2C 重试、PD 协商或 suspend 超过周期会误触发；当前策略允许部分操作等待 10 秒，因此不能照搬 0.5/1/5 秒，更不能仅凭存在 30 秒档就认定安全。
- 必须确认实际喂狗动作、普通寄存器访问是否也会重置计时、读操作有无副作用；独立定时器盲目喂狗可能掩盖策略已失效。
- 必须确认到期停泵、保护寄存器是否复位、默认值、故障标志清除与 IRQ 行为；未核实前不能保证当前保护配置继续有效。
- 到期后不能只清标志重新启泵；必须重新验证保护、遥测与合约，保留原停止/固定 PD/SMB5 释放事务。挂起、解绑、重启及 I2C 故障都需独立反例。

当前 `umi_pps_hw.c` 配置并验证 WD_DISABLED；直接清 bit2 会被现有保护校验判为故障。因此本轮保持禁用。后续应先取得精确型号规格或受控实验依据，再设计受健康状态约束的喂狗与到期恢复，最后经独立复核和明确授权做失联/超时 L3；本轮未主动启用或测试到期。

## 离线验证与材料

环境：Windows x64 / PowerShell 7.6.9 + WSL Linux x86_64，GCC 13.3.0、GNU make 4.3；固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`（6.18.60）。工作分支 `codex/native-dsi-dt-power`，起点 `b73f19db3c7ce52209ea26a058f58f747f31157b`，本轮未提交/推送/合并。

- L1：`python -X utf8 -m unittest discover -s tests -v`，传入 `MISEED_WIRED_DTB`/`MISEED_DT_SCHEMA` 和 dtschema venv PATH；204 项通过、0 跳过。涵盖 59 个面板命令失败、DSC 失败/短写/零亮度、BQ IRQ 独占/退订，以及既有 PPS/互锁/温控/回退。第一次全量执行发现新增 GPL 导出宏影响用户态测试桩，修复桩后全量通过；未改芯片逻辑以适配桩。
- Schema：dtschema 2025.8、yamllint 1.38.0，`tools/validate_kernel_dt.py` 对 staged source、板级与最终 DTB 执行检查；涵盖 FG/SMB5/BQ、NFC、thermal、电池、TLMM、simple-framebuffer、新 CSOT panel、SM8250 MDSS、DSI controller、7nm PHY、RPMh regulator。4 项 DT 回归无跳过；无诊断。不是全机 schema 验收。
- 原生 L2：`MISEED_NATIVE_DSI=1 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/native-dsi-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/native-dsi-source-20261111 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/native-dsi-build-20261111 MISEED_JOBS=6 bash tools/build_kernel_l2.sh`。首次链接发现 rescue 新文件遗漏，已补入构建清单并重建通过。
- 原生输出 `.work/native-dsi-20261111/`；默认回归输出 `.work/simpledrm-split-20261111/`；保留 `resolved.config`、`native-dsi-profile`、`build-source-lock.json` 与来源 hashes。日志仅存忽略的 `.work/`，不提交构建物或长原始日志。

## L3 执行计划与停止条件

第一阶段只验证显示/DT/新驱动绑定，使用电脑 USB 数据口；不接 PPS 充电器、不做无线 RX/TX。先核对唯一设备为 umi、CSOT 变体与镜像哈希，取得 RAM 启动及本次显示资源操作授权。使用 `fastboot boot`，不刷写分区。观察原生画面/方向、USB、PON，导出 `status`、`dtb` 和 `charge-log`；检查 msm/DSI/panel 绑定及 SPMI 注册错误，不能把 simpledrm 旧画面当作原生 DSI 成功。

本机已准备 `.work/capture-native-fdt.py`（pyserial 3.5，复用已验证的 MiSeed VID/PID 与 console 身份检查），仅发送只读 `dtb`，输出不得覆盖；`--help` 已离线检查，未打开真机串口。

主机以 `python -X utf8 tools/check_runtime_dt.py --reference .work/native-dsi-20261111/umi-csot.dtb --serial-export <本次导出>` 比对最终树。失败保留材料并停止；不靠删除未知 PMIC 节点使测试通过。亮度/熄屏恢复/挂起和独立面板供电需要后续标准 KMS/backlight 消费者及专项步骤，不能据首屏判全部完成。

无显示、失去 USB、持续 probe/DSI/SPMI 错误或异常发热即停止；有 USB 时保存 RAM 日志后用既有 `bootloader` 命令回退，无 USB 时使用已知硬件按键回 Fastboot，再正常启动原 Android。回退不写分区、不锁 BL。拆分后的有线 PPS/固定 PD/停止/拔插与故障恢复另行授权，先完成显示/DT 阶段，不把两种测试混为一项。

🚦 实施/离线验证按以上范围；面板供电/变体与新生命周期 L3 为黄灯，RISK-010/TD-004/006/007 保持开放。高风险合并仍缺开发者以外专项复核、适用 L3 与精确 HEAD CI，不能仅凭本地构建批准合并。

## 最终产物核对

| Profile | boot.img SHA-256 | DTB SHA-256 |
| --- | --- | --- |
| native-dsi-20261111 | `cc7687f9a3cc1e1214dbb09664cd95606217f8abbf0c8b0fcda1c3c8c8dada9e` | `e883d6c5048f3025bf3a45bf484103c222c75379daf0db643b14e9ef9bcd9f2c` |
| simpledrm-split-20261111 | `d9f8de1b51a3c45dfe2220dac9bafd7a5f2ea18533b61aa8c954ed3982a9682f` | `8c7f8455cf3b6310648971daf38861a495ad354b359dc0572f58f700f03cb8de` |

两份 `build-source-lock.json` 的所有输入哈希均与当前仓库一致；均启用 BQ core/auxiliary/PPS，只有原生 profile 启用 CSOT panel。默认构建检测到旧 source cache 的 Makefile 不符合当前审查规则，保留旧树并使用全新源码/构建树，最终成功；未放宽缓存验证。

最终 native build、default build、schema 和 204 项全量测试均退出 0（0 跳过）；Shell `bash -n`、ShellCheck error 级、`git diff --check` 与仓库 L0 通过。8 项已有源码黄灯继续归 TD-008；无新增源码篇幅黄灯。没有远端 CI、独立专项复核或本轮 L3 结果。

## 首次 L3 后续

上述离线结论之后，原生镜像已获授权完成首次 RAM 启动和采集；SPMI/TLMM 隔离及 BQ 拆分 probe 取得 L3，但发现 root/DPU/MMC 仍被 ABL 覆盖，显示采集缺项、点屏未确认。事实与后续门槛见[真机记录](../hardware/2026-11-11-umi-native-dsi-first-l3.md)，不回写为原生显示通过。

## 黑屏反馈后的续查与补救

### 当前事实与外部证据

用户已确认首次 RAM 启动时黑屏、随后回到 Android，补记在[首次 L3](../hardware/2026-11-11-umi-native-dsi-first-l3.md)。续接工作在 Windows 10.0.26200 x64 / PowerShell 7.6.9 上进行，离线目标编译使用 Ubuntu 24.04 WSL2 x86_64、Bash 和 GCC 13.3.0；保留既有未提交改动。本次不操作手机。

重新通过 HTTPS 核对 colorgarden/umi-mainline 固定 commit `5fa2e0048f89b82355d2a9de89c1d74ab9910959` 的下列文件，网络字节与本地副本相同，SHA-256 补入 sources.lock.json 的 reviewed_paths；不从移动分支自动消费代码：

- [TESTLOG §10](https://github.com/colorgarden/umi-mainline/blob/5fa2e0048f89b82355d2a9de89c1d74ab9910959/docs/TESTLOG.md)：来源记录 Linux 6.12.94、1080×2340@60、4-lane command mode / DSC、59 包发送成功、msmdrmfb/fbcon、目视点屏及熄屏再亮。400/2047 黑屏，2047 可见，启动脚本选择 1500。它报告的真实三段复位时序新版本未上机，不能把该版本也写为已验。
- `docs/DISPLAY.md`、`initramfs/init`：驱动保留初值 1；用户空间先写 0，再写 1500。没有外部 PWM/WLED，软件 brightness/actual_brightness 不证明 DCS 到达面板。
- `patches/panel-xiaomi-umi.c`：仅审查机制，未复制其任意寄存器、kick/repower 等私有 sysfs。来源亮度回调同样使用普通 `mipi_dsi_dcs_set_display_brightness()`；固定 6.18.60 的普通/large 版本分别以低/高字节优先，当前未擅自改字节序。

判断：同款固定 CSOT 有原生 MSM/DPU→DSI→panel 的来源报告，技术路径可行；MiSeed 的低初值和缺少用户空间亮度设置是明确遗漏，尚不能证明它是本机黑屏的全部原因。供电仍继承固件、具体 CSOT 变体和恢复时序仍需本机验证。

### 本地实现与接口审查

| 功能 | 固定 Linux 6.18.60 接口 | 标准消费者与本地差异 |
| --- | --- | --- |
| 显示与背光 | DRM panel/KMS、backlight_class；Documentation/ABI/stable/sysfs-class-backlight | Debian compositor/KMS 与标准 brightness；救援 `display-on/off` 只是 sysfs bring-up 包装，不是产品 ABI |
| 点屏诊断 | DRM connector sysfs、DRM debugfs atomic state、fbdev 身份 | 读取 DRM、backlight、framebuffer、MDSS/DPU/DSI/PHY 与 panel 绑定；debugfs 限诊断，归 TD-006 |
| 内核日志 | syslog READ_ALL / SIZE_BUFFER（3/10） | `dmesg` 同类只读快照；不清空 ring、不筛选消息。原生 profile 将 ring 配为 1MiB；已覆盖的早期消息仍无法恢复 |
| DT 交接 | libfdt 符号重定向、OF disabled 层级和原主线 phandle | 不改主线 binding，不引入 Android 服务；整个 soc 子树逐属性比较，根 model/compatible 均严格比较 |

新增 `kernel/rescue_display.c`：`display` 采集只读状态；`display-on` 只接受唯一 `xiaomi,umi-csot` 且 max=2047 的 backlight，先写 0 再写 1500、检查写入与软件读回；`display-off` 写 0。失败不尝试其他 backlight，不自动提高亮度，不把读回当作点屏。面板驱动记录 prepare 完成与亮度传输返回码，初值仍为 1。将既有有线固定 PD thermal helper 原样按职责移入 rescue_thermal.c，保持 init 单元≤300 行；无线 RX/TX 不涉及。

新增 `tools/collect_display_evidence.py`（Windows）：先验证参考 DTB，再验证 MiSeed VID/PID 与 greeting；默认只读采集显示状态、未筛选 kmsg 和最终 FDT，`--light-panel` 才发送 display-on 并追加状态。分帧/字节数/连续 DTB 校验失败保留 partial，成功文件留哈希；runtime DT 审计失败也先保全所有采集结果并退出 1。

### Overlay 防护与未解边界

`mdss_mdp`、`sdhc_2` 的导出符号新增重定向，保留原真实 phandle 和内部连接。各自的 overlay 目标置于独立 disabled 外层内，允许下游将目标本身设为 okay，但不得启用外层或真实 soc；root 及现有 soc/SPMI/TLMM 审计不放宽。`target-path` / 数字 phandle / 深层子节点旁路均须被全 soc 比对拒绝。

首次真实 FDT 的根仅 compatible/model 两属性不同，没有指向 `/` 的导出 symbol 可重定向。因此这两项无法由本次 symbol 扩展修复；仍需本机实际 DTBO/选中条目及 ABL 路径证据定位，不能臆定来自某个 fragment。保持 RISK-010 开放，不能通过白名单掩盖 root 差异，也未在内核里伪造运行树。严格整体审计尚不能通过，下次真机安排前需解决该阻断。

### 验证与后续材料

本轮目标为 L0/L1/L2 和可审查的采集材料；不继承上次授权执行新的显示资源、供电或设备操作。下一轮若获授权，使用本次构建的新载荷与来源锁，电脑数据口、临时 RAM 启动，先身份/DT/显示只读采集；约定的亮度测试才使用 `--light-panel`，不接 PPS 充电器，不测无线 RX/TX。保存证据后经 bootloader 回 Fastboot，再正常重启 Android；若采集缺项、审计失败或需重建，保全现场后结束本次操作。root 未解前不通知接入。

### 收尾与审查待办

当前 HEAD `b73f19db3c7ce52209ea26a058f58f747f31157b` 加未提交工作树，相对本地 main `d9a65dca4a1a9530a44047273f61cabbb5f8b572` 估算 94 文件、约 3.8k 新增行；正式 PR 规模仍须以 GitHub additions/changed_files 为准。文件规模已超过 90 的收尾红线、未超过 100 硬上限，因此停止功能扩展，本轮只完成修复和适用验证。未获提交、推送、发布或合并授权，当前没有创建/更新 PR；后续获授权建/更新 PR 时须带入本段收尾清单并复核远端精确值。

- 完成本轮 L0、全部反例、两 profile L2、定向 schema 和输入/产物哈希核对，保留原失败与复跑结果。
- 开发者以外专项复核：隔离层级与旁路、显示/reset/DSC 时序、日志/采集失败路径、既有有线 provider/policy 生命周期及互锁；不以本轮自查冒充独立复核。
- 先定位 root 交接并完成修复/评审，再准备明确授权的新 L3；本次不宣称 DPU/MMC 的新隔离已上机、原生点屏或睡眠恢复成功。
- Debian 标准显示消费者、UEFI 产品路径和无线 RX/TX 保持原工作包边界。必要门禁与 RISK-010 未关闭前不合并，后续功能另开工作分支/PR。

### 续接轮 L2 结果与载荷

以下命令由 Windows 经 `wsl -d Ubuntu-24.04 --cd /mnt/c/MiSeed --` 执行；均退出 0。编译器 GCC 13.3.0，来源仍为 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`。本轮未修改内核/固件来源版本；新旧原生配置逐项比对只多出 `CONFIG_LOG_BUF_SHIFT=17→20`。

```sh
# 原生：既有固定源码/构建目录通过来源验证后重用
MISEED_NATIVE_DSI=1 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/native-dsi-resume-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/native-dsi-source-20261111 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/native-dsi-build-20261111 MISEED_JOBS=6 bash tools/build_kernel_l2.sh
# 默认：旧 source cache 不满足当前审查时自动使用隔离的新源码/构建树，未放宽校验
MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/simpledrm-resume-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/source-split-source-20261110 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/source-split-build-20261110 MISEED_JOBS=6 bash tools/build_kernel_l2.sh
/tmp/miseed-dtschema/bin/python tools/validate_kernel_dt.py --source /tmp/miseed-boot-l2/native-dsi-source-20261111 --build /tmp/miseed-boot-l2/native-dsi-build-20261111 --dtb /mnt/c/MiSeed/.work/native-dsi-resume-20261111/umi-csot.dtb
/tmp/miseed-dtschema/bin/python tools/validate_kernel_dt.py --source /tmp/miseed-boot-l2/native-dsi-source-20261111 --build /tmp/miseed-boot-l2/native-dsi-build-20261111 --dtb /mnt/c/MiSeed/.work/simpledrm-resume-20261111/umi-csot.dtb
```

两个 schema 命令各有 4 项定向反例、0 跳过，标准诊断为空；第二次复用相同的已审查 binding schema，并核对默认 profile 的最终打包 DTB，不声称重建了已经清理的默认临时源码树。`bash -n`、`shellcheck --severity=error` 退出 0。thermal helper 搬移除去公开函数的 static 修饰后，与 HEAD 原实现逐字相同；完整有线控制反例随全量 suite 验证。

| profile / 文件 | bytes | SHA-256 |
| --- | ---: | --- |
| native-dsi-resume-20261111/boot.img | 14135296 | `65b4e6fca5759a4944c4cc697b1a879ff887619b812c31e833596c7e7788d050` |
| native-dsi-resume-20261111/umi-csot.dtb | 179928 | `b254490cbaf3f46f4892e5754e752d2da4030a9c6e449b0187a4c416f482c6e8` |
| native-dsi-resume-20261111/resolved.config | 98822 | `064d6dd908d67c83d2ded659cd6a2e0623748ecd3047a8bbc26369232f693978` |
| native-dsi-resume-20261111/build-source-lock.json | 43990 | `654c2ab28d0479a39caa98781f27990ba3953c62e4c73aed095bc5947d4dabae` |
| simpledrm-resume-20261111/boot.img | 9654272 | `78481573eb0b900cd4ad54d8f4fbce17b238562f90d6f97054e07cbdbd4e89e5` |
| simpledrm-resume-20261111/umi-csot.dtb | 179062 | `5cbd07260ec34a7d10bc98bc687f4ee01b05a5754ae31776ae0131eb10ccb184` |
| simpledrm-resume-20261111/resolved.config | 92693 | `74ac03448769553361448fbf99554d6f62f5a06e426bb1600accf64d610d24a0` |
| simpledrm-resume-20261111/build-source-lock.json | 43990 | `654c2ab28d0479a39caa98781f27990ba3953c62e4c73aed095bc5947d4dabae` |

两份 build-source-lock.json 与当前 manifests/sources.lock.json 完全相同，每个 input_sha256 均已逐项比对当前仓库字节，无差异。镜像、schema、长原始日志及完整清单仅留在忽略的 `.work/`，不入库。原生 DTB 的 `mdss_mdp` / `sdhc_2` 已指向禁用父层的 `/target`，本机是否实际遵循该映射仍待新 L3。

首次显示桩编译发现一个未使用局部变量并已修复；显示的身份/歧义/写失败/读回错误、日志权限/尺寸/丢字节反例通过。全量普通环境中 4 项 schema 测试因 PATH 不含 dt-validate 跳过；随后并行生成 schema 与运行回归造成 4 次 JSONDecodeError，属本轮验证调度错误，保留失败日志。最终使用完成后的 processed-schema.json 固定快照和显式工具 PATH 串行消费，不把中间失败或跳过写成通过。另修正新采集工具的预检：打包 DTB 的 memory/reg 原本由 ABL 填充，只做格式解析，不能把它当作已启动设备的 RAM 样本做自审；实际导出树的 RAM 检查继续严格执行。

最终全量命令（schema 快照已完成后执行）：

```sh
PATH=/tmp/miseed-dtschema/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/native-dsi-resume-20261111/umi-csot.dtb MISEED_DT_SCHEMA=/mnt/c/MiSeed/.work/dsi-resume-processed-schema.json python -m unittest discover -s tests -v
```

退出 0，213 项、0 失败、0 跳过，耗时 46.730 秒；日志 `.work/dsi-resume-verified-tests.log`。另有原生与默认各一次定向 schema PASS、4 项反例、0 跳过；成功与失败日志均保留在 `.work/dsi-resume-*`。本轮新路径仍无真实 USB 串口/面板 L3，不把合成日志或 sysfs 桩当作设备读回。

🚦 收尾 Audit：L0 文本/链接/锁/预算与 diff 检查、上述 L1/L2 为绿；旧 8 项源码黄灯归 TD-008。本地 94 文件规模红灯已启动收尾，100 文件硬上限未超；未创建/更新 PR，远端正式规模和精确 HEAD CI 未核验。RISK-010 root 交接、新镜像 L3、独立专项复核仍为合并阻断。不提交、不推送、不发布、不合并，未刷写、改供电参数或启动新充电测试。

## 根元数据与 UBWC 续修：实施前接口审查

固定 ABL 参考 `prabhatkrmishra/android_bootable_bootloader_edk2@7435641439c0021884a56f7d2ecc483377aa8b91` 的 BootLinux.c → ufdt_apply_multi_overlay → ufdt_overlay_apply → ufdt_overlay_root_node，会在 fragment 之前覆盖主树已有同名根属性。研究副本保留原版权/许可头，仅在忽略的 .work 编译；未导入产品。使用原始 LibUfdt/POSIX sysdeps、本机 libfdt 和一个仅定义链表布局的 host shim 编译成功；Umi MIUI 参考条目 8 重放精确复现 compatible/model 两处改写，live soc 全子树不变。普通 fdtoverlay 对照不复制根元数据，解释了既有 L1 盲点。29 条目均复现根复制，27 条目 live soc 不变，另外两个非 Umi 条目会改 PCIe 节点，未将它们写成通过。

本机首次 FDT 的 dtbo_idx=11，参考包 Umi 索引为 8；当前没有 ADB 枚举。本机实际 DTBO 未采得，不能推断固件相同。新只读采集器和表解析器用于闭合这部分证据。

进一步全量扫描锁定 Linux C/header 中的精确根兼容字符串，发现 cpufreq-dt-platdev、qcom_pd_mapper 和 UBWC 数据库三处匹配表；前两项在当前配置未启用。UBWC_CONFIG=y，msm_mdss_init 与 DPU 初始化均调用 qcom_ubwc_config_get_data()，根缺失 qcom,sm8250 会返回 -EINVAL，使 MDSS/DPU 初始化终止。这是确定的源码/输入关联，不声称首次筛选日志已观察到该报错，也不把亮度排除出尚待验证项。

功能→接口：SM8250 UBWC 数据选择→现有 qcom_ubwc_cfg_data/标准 DRM 驱动→既有 DT root compatible 与 qcom ID→标准 KMS/fbdev/Debian compositor，无新增 sysfs/ioctl/binding、Android 服务或电气参数。复用候选：主线根匹配目前不识别 ABL 的旧 Kona 元数据；首选完整方案为 WP3/WP4 在受控第二阶段 UEFI 向 Linux 交接主线 DT，而不是改厂商分区、伪造运行树或删除根 compatible/model。为收尾本次 ABL 直启显示诊断，计划只在 CSOT panel 配置启用时，按完整 Umi/Kona 旧 compatible、精确 model、board-id=0x2b/0 和 msm-id=QCOM_ID_SM8250（356）核对后复用同一个 sm8250_data；不扩展到其他机型、不改变配置数值。未知或矛盾身份继续 -EINVAL。

这是仅 bring-up 的本地身份兼容 quirk，实施前归入 TD-006，责任 WP2/WP4；退出条件是 UEFI 标准根交接 L2/L3 后撤除，不把厂商旧 compatible 写入主线板级 DTS。上游评审方向仍是修正固件交接，而非推广板级 UBWC 硬编码或私有 ABI。根严格审计仍报差异，不能因复用配置就声称根已还原或整机 DTBO 已闭合。

### 根复制依据与实际修复结果

来源链接：[固定 ABL 调用](https://github.com/prabhatkrmishra/android_bootable_bootloader_edk2/blob/7435641439c0021884a56f7d2ecc483377aa8b91/QcomModulePkg/Library/BootLib/BootLinux.c#L350)、[固定 LibUfdt 根复制](https://github.com/prabhatkrmishra/android_bootable_bootloader_edk2/blob/7435641439c0021884a56f7d2ecc483377aa8b91/EmbeddedPkg/Library/LibUfdt/ufdt_overlay.c#L485)、[本项目固定 UBWC 数据选择](https://github.com/gregkh/linux/blob/f45ed76d6791cf72ff1265ee93511175bbbc2f18/drivers/soc/qcom/ubwc_config.c)。ABI/接口与妥协评审沿用上一节；这是参考 ABL 机制与保存的本机 FDT 输入的交叉复现，不声称 ABL 二进制本体已比对。

`kernel/display/umi_abl_ubwc.h` 已实现身份 gate；`apply_display_port.py` 只接受 UBWC 原始 SHA-256 `54351cbd9cad8293c312ef519fb227aeaa6d8f5abb90b0bbc694e7e8ea3752c0` 或精确、可逆的本地补丁。缓存验证器同步接受唯一该补丁，拒绝其他修改；源版权头保留。CSOT 配置外 quirk 不生效，标准根仍优先走原表，不在板级 DTS 引入旧 compatible，不写运行时 FDT。后续仅受控 UEFI 交接恢复 canonical 根后，才能退出 TD-006；不能把这个兼容处理写成根字节已恢复。

新增的 10 种错误身份反例、启用/关闭 profile、幂等与未知源拒绝均已通过。另从固定源码提取实际 qcom_ubwc_config_get_data() 原函数及补丁后函数，用 OF stub 单独编译/执行：保存的 Umi/Kona 根输入在原版确定返回 -EINVAL，native 修复版确定返回同一 sm8250_data 对象；关闭 native 后仍拒绝旧根。标准根、未知型号、缺根及 of_node_put 次数分别验证通过。结果位于 `.work/root-dt-review-20261111/ubwc-function-results.json`；这属于 L1，不是硬件显示证据。

审计工具改为同时给出 root_identity、live_hardware、ram 分组；根报错不能短路隐藏深层硬件变化。对首次 L3 原始载荷重审仍退出 1，并同时报告 compatible、model 和 live soc 改写，未放宽原判定。只读 DTBO 采集器的表边界、重叠、错索引、错设备、会话选择变化均有反例；Windows 5 项通过。一次实际启动采集器只到 ADB 枚举即停止：没有唯一已授权 Android 设备，未发 su、未读分区、未建成功输出目录；用户连接问题仍待回复。

本轮首轮 221 项通过（0 跳过），添加 UBWC 反例后一次 223 项回归中 1 项仓库快照测试遇到并行 Kconfig merge 的短命 sed 临时文件而失败，其余项通过；保留失败，不将其记为最终成功。最终回归安排在全部构建结束后串行执行。所有构建、schema 和原始长日志均留在 `.work/root-dt-review-20261111/`。

最终 L2 使用 GCC 13.3.0 / Linux f45ed76d6791cf72ff1265ee93511175bbbc2f18，两 profile 构建退出 0：

```sh
MISEED_NATIVE_DSI=1 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/native-dsi-root-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/native-dsi-source-20261111 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/native-dsi-build-20261111 MISEED_JOBS=6 bash tools/build_kernel_l2.sh
MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/simpledrm-root-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/root-default-source-20261111 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/root-default-build-20261111 MISEED_JOBS=6 bash tools/build_kernel_l2.sh
```

| profile / 文件 | bytes | SHA-256 |
| --- | ---: | --- |
| native-dsi-root-20261111/boot.img | 14135296 | `d7f01af45e66822790808203df0e5c46391c642547fc6f4c272cc920d33849c1` |
| native-dsi-root-20261111/umi-csot.dtb | 179928 | `b254490cbaf3f46f4892e5754e752d2da4030a9c6e449b0187a4c416f482c6e8` |
| native-dsi-root-20261111/resolved.config | 98822 | `064d6dd908d67c83d2ded659cd6a2e0623748ecd3047a8bbc26369232f693978` |
| native-dsi-root-20261111/build-source-lock.json | 44653 | `a3737b6118103c79f59742541823c41080c10b4bb5b4bf12bbced61f98cf6be9` |
| simpledrm-root-20261111/boot.img | 9654272 | `78481573eb0b900cd4ad54d8f4fbce17b238562f90d6f97054e07cbdbd4e89e5` |
| simpledrm-root-20261111/umi-csot.dtb | 179062 | `5cbd07260ec34a7d10bc98bc687f4ee01b05a5754ae31776ae0131eb10ccb184` |
| simpledrm-root-20261111/resolved.config | 92693 | `74ac03448769553361448fbf99554d6f62f5a06e426bb1600accf64d610d24a0` |
| simpledrm-root-20261111/build-source-lock.json | 44653 | `a3737b6118103c79f59742541823c41080c10b4bb5b4bf12bbced61f98cf6be9` |

两份最终 build-source-lock.json 与当前清单完全相同，每项 input_sha256 均匹配当前字节。原生 config 与上轮配置相同；原生 UBWC 对象已重编，镜像散列改变。默认 boot.img 与上轮逐字节相同（78481573…d4e89e5），不把 guard 外代码存在说成默认行为改变。未产生新 L3；本机 DTBO 和显示反馈仍需设备接入。

当前本地 main 比较为 98 文件、约 4.4k 新增行，仍处规模收尾红灯，未越 100 硬上限；上述均为现有显示阻断和采集盲点的收尾修复，停止功能扩展。合并前独立专项复核与精确 HEAD CI 未取得，未获提交/推送/合并授权，均未执行。

### 本轮最终验证与交付边界

全部构建结束后串行执行（使用已经完成的 schema 快照）：

```sh
PATH=/tmp/miseed-dtschema/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/native-dsi-root-20261111/umi-csot.dtb MISEED_DT_SCHEMA=/mnt/c/MiSeed/.work/dsi-resume-processed-schema.json python -m unittest discover -s tests -v
/tmp/miseed-dtschema/bin/python tools/validate_kernel_dt.py --source /tmp/miseed-boot-l2/native-dsi-source-20261111 --build /tmp/miseed-boot-l2/native-dsi-build-20261111 --dtb /mnt/c/MiSeed/.work/native-dsi-root-20261111/umi-csot.dtb
/tmp/miseed-dtschema/bin/python tools/validate_kernel_dt.py --source /tmp/miseed-boot-l2/root-default-source-20261111 --build /tmp/miseed-boot-l2/root-default-build-20261111 --dtb /mnt/c/MiSeed/.work/simpledrm-root-20261111/umi-csot.dtb
```

首条退出 0，223 项、0 失败、0 跳过，48.563 秒；后两条各退出 0、4 项定向反例、0 跳过。日志分别是 `.work/root-dt-review-20261111/verified-tests.log`、`native-schema.log`、`default-schema.log`。Windows 单独 DTBO suite 5 项通过。完整新 C helper 与原 UBWC 取数函数的基线/修复对照已通过，ARM64 的 ubwc_config.o 重编成功。两份构建锁及所有输入哈希再次逐项一致。没有把并发快照失败当作成功，也没有把 L1/L2 当作本机点屏。

🚦 最终 Audit：本轮 L0/L1/L2 通过；旧 8 项源码黄灯仍归 TD-008，新增/修改单元均≤300 行。98 文件规模红灯保持收尾，未越 100 文件硬上限；无功能扩展、提交、推送、发布或合并。根元数据复制机制与 UBWC 失配已闭合到源码/重放层，TD-006 的诊断 quirk 不恢复 root 字节、不豁免严格根审计。RISK-010 的本机 DTBO 哈希、选中条目、新隔离/UBWC/显示 L3，及独立专项复核仍开放。第二阶段 UEFI 交接 canonical 根是产品路径关闭条件。

下一步设备准备：DTBO 只读采集工具已就绪，等待唯一已授权 Android ADB 设备；只读 `dtbo` 和公开根属性，不访问身份/NV，不调用 adb root、重启、remount 或写设备文件。失败/索引变化即停止并保留主机 partial。后续原生 RAM 点屏属于另一次有明确显示资源操作授权与回退的 L3；若采用已登记的 legacy-root 诊断 profile，应单独记录根审计差异与 quirk 命中日志，live_hardware/ram 必须通过，不能把整体 FAIL 写成标准交接 PASS。未知身份、额外硬件树变化或采集缺项均停止并回退，不在现场改阈值或重建。

## 本机 DTBO 证据闭合后的采集器收尾

用户随后确认 Android 已连接；实际设备读取结果与授权、回退详情统一归[硬件记录](../hardware/2026-11-11-umi-native-dsi-first-l3.md#本机-dtbo-后续只读采集与重放2026-11-11)。当前 12 条目镜像选中 11，与 MIUI 参考 29 条目的 Umi 索引 8 不混用；实际条目已逐属性重现首次 L3 的根与完整 live soc，新隔离的同条目重放通过。这关闭了实际 DTBO 未获取与覆盖来源未知两个证据缺口，没有新增新镜像 RAM 启动或点屏结果。

采集器现支持既有 su、已为 uid=0 的 adbd，或显式 --temporary-adb-root；默认不提升权限。临时分支在发送 root 前标记清理责任，成功、拒绝、传输超时、采集错误均执行 unroot 并验证恢复初始 uid；恢复未确认不发布成功镜像或 evidence.json。固定只读命令、唯一 Umi 身份、同一 boot ID/选择索引、表/属性边界均保留。Windows/WSL 各 8 项采集正反例通过；真实临时分支也完成采集与两次普通 uid=2000 检查。root 属性字符串/cell 和 boot ID 均检查格式，不能把 adbd exec-out 返回 0 的 Permission denied 文本当数据。

显示采集器新增完成失败回执与传输失败的区分：若 display-on 已返回完整 rc!=0 帧，保留 partial，并继续只读 display/dmesg/dtb，以免再次丢失黑屏诊断；若连接断开、超时或帧不完整则停止，不反复发送命令。新增两项正反例通过；这只改善主机采集，不改变驱动或亮度参数。

为使本轮主机工具哈希与后续镜像来源锁一致，已分别重跑原生/默认 L2（退出 0）。DTB、config、boot.img 均与前轮已经通过定向 schema 的对应产物逐字节相同；只更新构建锁快照，不伪造新硬件验收：

```sh
MISEED_NATIVE_DSI=1 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/native-dsi-device-dtbo-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/native-dsi-source-20261111 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/native-dsi-build-20261111 MISEED_JOBS=6 bash tools/build_kernel_l2.sh
MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/simpledrm-device-dtbo-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/root-default-source-20261111 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/root-default-build-20261111 MISEED_JOBS=6 bash tools/build_kernel_l2.sh
```

| 当前材料 | bytes | SHA-256 |
| --- | ---: | --- |
| native-dsi-device-dtbo-20261111/boot.img | 14135296 | `d7f01af45e66822790808203df0e5c46391c642547fc6f4c272cc920d33849c1` |
| native-dsi-device-dtbo-20261111/build-source-lock.json | 44653 | `70f6cb3f21bb5b483cf32f5ced87c81acd876e29e2f86a85d24ccd26f2d76284` |
| simpledrm-device-dtbo-20261111/boot.img | 9654272 | `78481573eb0b900cd4ad54d8f4fbce17b238562f90d6f97054e07cbdbd4e89e5` |
| simpledrm-device-dtbo-20261111/build-source-lock.json | 44653 | `70f6cb3f21bb5b483cf32f5ced87c81acd876e29e2f86a85d24ccd26f2d76284` |

原始分区、选中条目、重放产物及长日志仅留 .work/root-dt-device-20261111/；不提交固件或构建物。新原生 RAM 测试材料包含镜像/配置/当前输入锁、本机条目隔离重放、带恢复分支的采集器及 bootloader→Android 回退。测试应仅接电脑数据口，不接 PD/PPS 充电器、不测无线 RX/TX，不发 charge-test、thermal 或寄存器控制。实际启动前仍核对唯一 umi/Fastboot 解锁态及镜像哈希；本轮只读接入没有授权自动重启或显示资源操作。若获下一步明确授权，先采集只读状态和严格 DT 分组，再按约定写 1500 背光并采集完整证据，最后回 Android。已解释的根差异保留 FAIL，不混同 live 硬件树/实际显示结论；额外树变化、未知身份或采集缺项停止并回退。

本轮最终全量命令（所有构建结束后串行）：`PATH=/tmp/miseed-dtschema/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/native-dsi-device-dtbo-20261111/umi-csot.dtb MISEED_DT_SCHEMA=/mnt/c/MiSeed/.work/dsi-resume-processed-schema.json python -m unittest discover -s tests -v`，退出 0，228 项、0 跳过、55.063 秒。日志 .work/root-dt-device-20261111/verified-tests.log。两份当前构建锁再次逐项与仓库输入哈希一致；boot.img/config/DTB 均与前轮逐字节相同，原生及默认 L2 均重新退出 0。本轮没有新 C/DTS 改动；不把主机采集器更新说成新点屏 L3。

材料补核：Windows pyserial 3.5 与既有 Fastboot 工具可用；新原生镜像为 .work/native-dsi-device-dtbo-20261111/boot.img（d7f01af45e66822790808203df0e5c46391c642547fc6f4c272cc920d33849c1）。只读状态/分组 FDT 与完整日志采集、显式 1500 亮度控制、失败回执保全和传输失败停止路径齐备；运行前按唯一设备重新核对身份。当前手机保持 Android/普通 ADB，未进行新 RAM 启动，等待下一轮明确授权。

🚦 收尾结论：本机 DTBO 只读采集/权限恢复 L3、实际条目重放 L1、当前镜像 L2 与 228 项回归已闭合；新镜像隔离/UBWC/显示 L3、UEFI 标准根交接、独立专项复核和精确 HEAD CI 仍未关闭。PR 本地估算仍为 98 文件，红灯收尾且未越 100 硬上限；旧 8 项源码黄灯仍归 TD-008。没有提交、推送、发布或合并。原始 DTBO/镜像/长日志不入库。


## 第二次黑屏后的静态兼容审查

本轮用户建议核对 [colorgarden/umi-mainline](https://github.com/colorgarden/umi-mainline)，先检查 Linux 6.12 与固定 6.18.60 的 API、binding、reset 与供电，保留 simpledrm。已授权 d7f01af4…d33849c1 的 RAM 验证及成功返回 Android 统一归[第二次 L3](../hardware/2026-11-11-umi-native-dsi-first-l3.md#修复隔离ubwc-后的第二次原生-ram-l32026-11-11)；用户黑屏反馈已记录。以下为回退后的离线工作，不沿用旧镜像 L3 给新镜像背书。

### 固定来源与接口审查结果

对照沿用已锁的 colorgarden commit 5fa2e0048f89b82355d2a9de89c1d74ab9910959：[panel](https://github.com/colorgarden/umi-mainline/blob/5fa2e0048f89b82355d2a9de89c1d74ab9910959/patches/panel-xiaomi-umi.c)、[面板 DTS](https://github.com/colorgarden/umi-mainline/blob/5fa2e0048f89b82355d2a9de89c1d74ab9910959/dts/xiaomi-umi-panels.dtsi)、[config](https://github.com/colorgarden/umi-mainline/blob/5fa2e0048f89b82355d2a9de89c1d74ab9910959/config/umi.config)、[TESTLOG](https://github.com/colorgarden/umi-mainline/blob/5fa2e0048f89b82355d2a9de89c1d74ab9910959/docs/TESTLOG.md)。这是第三方作者报告，不能替代 MiSeed 点屏 L3；此前已选择性移植固定数据，现有实现不是尚未开始。

另下载 gregkh/linux v6.12.94 的 dsi_manager.c、dsi_host.c、msm_fbdev.c，与固定 f45ed76d6791cf72ff1265ee93511175bbbc2f18（6.18.60）逐段比较。旧文件 SHA-256 分别 e37aad9caa001a994d04b09d296c47f79c54ee291f0633468ff66e43c9cf690b、e7f67a974e13cbdd5e2bbeaff2fa12ffadd70112f37baac57247373d1e6cdf74、83ff56634028ec64a648cb141fdb5751b7b03a73bccca5a58f4f2a0b39086502。它们仅为审查输入，未换内核、未导入驱动；原文和 diff 保存在 .work/dsi-kernel-api-review-20261111/。

| 功能 → 主线接口 / 消费者 | 核对与本地差异 |
| --- | --- |
| 面板生命周期 → drm_panel / drm_bridge / 标准 DRM/KMS 与 fbcon，后续 Debian compositor | 参考兼容 devm_drm_panel_alloc 新旧写法；MiSeed 固定 6.18.60 的 devm_kzalloc + drm_panel_init 可编译。prepare_prev_first 在 panel bridge 转换为 pre_enable_prev_first，两边一致；主机先上电再执行 prepare |
| DSC/DSI → 标准 mipi_dsi / drm_dsc helpers，既有 DSI graph binding | 同为 4 lane、RGB888、command mode、LPM + noncontinuous clock；1080x2340@60、总时序 1112x3532、235655kHz、DSC 1.1/540x12/8bpc/8bpp。compression-mode → PPS → 59 包顺序一致，本次第一步已超时，不能归咎后面的厂商表 |
| DCS 成功/失败 → mipi_dsi_dcs_write_buffer 返回值 | 发现 MiSeed ret!=payload_len 的确定兼容缺陷。6.12.94/6.18.60 的 MSM 从 dsi_host_transfer → manager → host_cmd_tx → cmds2buf_tx 原样返回包含包头/4 字节对齐的 DMA 长度。例如 payload=6 返回 12；参考驱动只拒绝负错误。MiSeed 改为 ret<payload_len 时失败，保留短写/负错误闭锁，允许主线实际成功计数。这是潜在第二道阻塞，不是此次首个 type=0x07 超时根因 |
| 亮度 → 标准 backlight class / sysfs | 同用 mipi_dsi_dcs_set_display_brightness、0–2047；prepare=false 不发 DCS，新增明确 deferred 日志，不再让 1500 软件读回被误作送达证据。未未经证明改用 large helper 或调整电气参数 |
| reset/电源 → gpiod、regulator、既有 binding | 参考 reset/rails 可选，默认继承 ABL；MiSeed reset 为必需 GPIO12、ACTIVE_LOW，物理 high1/low1/high10ms，与所选固定表一致；控制器 L9a/PHY L5a 沿用锁定来源，面板 rails 仍由 ABL 继承。参考文档只证明当时低/高 reset 实测；完整三段最后版本的 L3 与本项目独立冷启动/PM 都未闭合 |

6.12→6.18 的 DSI manager 主要是 bridge attach/分配接口更新；命令 DMA prepare/trigger/wait 逻辑未发现能单独解释此次超时的顺序变化，GEM 地址空间迁移为 drm_gpuvm 的实际影响仍须新证据。SM8250 DSI host 未使用 panel hs_rate 字段，不能把赋值 682MHz 当实际时钟读回；下一轮采集实际 clk_summary/host rates。

### 故障解释与本轮处理

SMMU 的 SID 0x820/0xc20 匹配 MDSS 的 stream-mask；故障 IOVA 位于 /chosen/framebuffer@9c000000 所描述的 0x2300000 区间。当前记录未给 DSI TX IOVA、细时间线，无法确认旧扫描接管与命令超时是否有因果。preserved 0 boot mappings 是 arm_smmu_rmr_install_bypass_smr 的 ACPI IORT RMR 统计，不能据此宣称 DT 交接必然缺映射。参考配置 arm-smmu.disable_bypass=0 与本项目不同，但当前 fault 是已分配 cb=4 的翻译错误；没有证据支持用全局 bypass 修好它，本轮未关闭 IOMMU 或更改映射/硬件策略。

固定源码明确 dsi_err_worker status=0xc 组合为 FIFO 错误/MDP FIFO 下溢，并触发既有软件复位。为下一轮区分故障，在原始 SHA-256 9afe6c379338c04f05df7d53cdcc7de1c3e6be9bc327fc0fb31954752439eba8 的 dsi_host.c 上增加两处仅 CSOT native 配置生效的日志：FIFO 状态在原有清除前输出；DMA completion 等待超时输出 TX IOVA/len、CTRL/STATUS0/INTR、power/enabled 与 pclk/byte/esc rates。仅只读既有寄存器，不新增清除、复位、重试或输送。apply_display_port 和缓存验证器只接受原始或精确、可逆补丁，未知/多锚点/符号链接输入拒绝。保持上游 GPL/作者声明；接口仍为标准 DRM、backlight、debugfs、kmsg，无新增产品私有 ABI。

采集器 PHY compatible 改为本机及固定主线 DTS 中实际 qcom,dsi-phy-7nm，并导出 /proc/interrupts、clk_summary；native config 开启 PRINTK_TIME，1MiB 日志环保留。没有修补 sys_fillrect 警告：锁定源码告警后继续绘制，不是当前首命令超时的证据。默认 simpledrm 配置及既有可启动旧镜像保留；新默认镜像仅 rescue 取证程序随源码更新。

### 本轮离线验证

先输出两次适用 🚦 Audit（第二次纳入 DCS 成功计数修正），然后按既有 GCC 13.3.0/固定源码构建。确切命令与前节一致，将 native 输出改为 .work/native-dsi-dma-diag-20261111、默认输出改为 .work/simpledrm-dma-diag-20261111；native MISEED_NATIVE_DSI=1，两个独立 source/build cache 保持不变。两构建退出 0；构建锁均为 SHA-256 42da9082e3237cf9902b7651bcbaa3fadf91cd7f60c772f28ce9672a7aeb9593。

| 新材料 | bytes / SHA-256 |
| --- | --- |
| 原生 boot.img（未上机） | 14,135,296 / 9438724f260fbafaf00cc92d06896fbcb9e33224e0c1b2bce2004f10b9d4b380 |
| 原生 resolved.config | dc7ced5f2222b690a3c314c12734f1ca9be9af512f9bf7174d8e427baeda9c63 |
| 原生 DTB（与本次 L3 字节相同） | b254490cbaf3f46f4892e5754e752d2da4030a9c6e449b0187a4c416f482c6e8 |
| 默认 simpledrm boot.img（新 rescue 取证程序） | 9,654,272 / 68e3cc7b9041f530d3cc637955f85c24941145b6e20b3f27b3989dd8d5d82b3c |

所有构建结束后串行执行 PATH/schema/DTB 环境同前节的 python -m unittest discover -s tests -v（DTB 路径改为新 native 输出），退出 0，230 项、0 跳过、50.834s。实际 panel 生命周期 C harness 新增 MSM 对齐成功计数完整 59 命令，原有逐命令负错误、短写、DSC/PPS 失败和未准备亮度不发送仍通过；新增 DSI 补丁幂等/未知源/锚点/符号链接反例。日志 .work/native-dsi-dma-diag-tests-20261111.log，构建日志 .work/*-dma-diag-build-20261111.log；构建物/原始长日志不入库。

新载荷只达到 L2；首命令 DMA 根因、点屏、真实亮度送达、独立面板供电/PM、标准 UEFI 根交接、开发者外专项复核及精确 HEAD CI 仍开放。当前真实 DT 隔离/UBWC 与 Android 回退 L3 已完成，不回写为待验；根审计仍 FAIL，不称为 canonical DT 已恢复。PR 98 文件仍为红灯收尾，旧 8 项源码黄灯 TD-008；没有提交、推送、发布或合并。


## 新诊断镜像的后续真机结果

用户随后请求「再次上机」。同一 9438724f260fbafaf00cc92d06896fbcb9e33224e0c1b2bce2004f10b9d4b380 镜像的原生 DSC/59 条 DCS、1080×2340@60、1500 亮度命令和用户目视点屏已取得 L3，并确认普通权限 Android 返回；完整输入、命令、时间线与散列统一归[第三次 RAM 记录](../hardware/2026-11-11-umi-native-dsi-first-l3.md#第三次原生-ram-l3dsc亮度命令与目视点屏通过2026-11-11)。前节“新镜像未上机”保留为当时状态，不作为当前能力结论。

本次没有首命令超时或 FIFO 下溢，证明本次 59 条命令成功通过修正后的返回值检查；仍存在早期旧 framebuffer 地址范围 SMMU fault。不能因此宣称前次首个 DMA 超时的因果已经闭合。根 compatible/model 仍覆写，严格 FDT FAIL 与 UEFI 标准根交接开放项保留。权威阶段/能力/风险已同步更新，未增加源码或新构建，未再次启动额外一轮。


## PR #6 提交前收尾（2026-11-11）

用户授权「先提交到 PR6」。核对 PR #6 的 head=codex/linux-standard-interfaces / b73f19db3c7ce52209ea26a058f58f747f31157b，base=main / d9a65dca4a1a9530a44047273f61cabbb5f8b572，与本轮工作分支基点一致；本次追加提交，禁止 force push，不合并或发布。PR 描述同步收尾状态、验证及待办，后续功能另开 PR；提交前估算 98 文件，未越 100 文件硬上限。

暂存区 diff 检查发现 rescue_thermal.c 的 EOF 多余空行，提交尚未执行即清理；仅删空行并更新来源锁。原生/default 两套 L2 再次退出 0，输出分别 .work/pr6-submit-native-20261111 与 .work/pr6-submit-simpledrm-20261111；两套 boot.img、rescue-init、initramfs、DTB 和 config 均与前轮已验证版本逐字节相同。原生 boot.img 仍为已点亮的 9438724f…b9d4b380；新 build-source-lock.json SHA-256 1eabaa4fe652bf6b373c5c23ded38bf8e21f5aee5595f382c5a753ac9a44d198。未再次操作手机。

构建全部结束后，同前述 PATH/schema 环境、DTB 改为新原生输出，全量 unittest 再次退出 0：230 项、0 跳过、50.968s；日志 .work/pr6-submit-tests-20261111.log。ShellCheck error 级、仓库/来源锁与暂存区 diff 检查通过；产物与长日志仍不入库。提交使用维护者既有 Git 身份、Conventional Commit 中文说明与 Codex trailer；推送前检查 PR base..HEAD 的新增提交署名，精确提交及远端 CI 结果以 PR #6 当前 HEAD/Checks 为准。开发者外专项复核、标准根/SMMU、保护等既有验收开放项继续保留，不以本次授权提交当作合并授权。

## 启动接管续修：MDSS CORE reset（2026-11-11）

本轮基于 `9d1c881` 工作树，保留已有 QUALITY、DTBO 采集器、诊断 harness 及来源锁修改。宿主 Windows 10.0.26200 / x64 / PowerShell Core 7.6.9，构建在 WSL Ubuntu-24.04 / Linux x86_64 / bash，交叉 GCC 13.3.0。没有连接、重启或写入手机，没有提交、推送或合并。

### 原因、接口与修复边界

- 固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的 `qcom_smmu_client_of_match` 已包含 `qcom,sm8250-mdss`，不能把故障归因于缺少该项，也不需要全局 bypass。DPU 随后在 `_dpu_kms_mmu_init()` 经标准 MSM IOMMU 接口附着独立 paging domain。
- 同版 `msm_mdss_reset()` 已实现 optional reset，持有 20ms 后释放；但 SM8250 DTS 缺少 `resets` 引用，原生板级 DTS 也未补充，因此实际跳过 reset。旧 L3 的 0x9c000000 framebuffer 区间访问与旧扫描在 domain 切换后继续发 DMA 的机制吻合；这是具体源码缺口与待验因果，尚非新镜像真机结论。
- 在可选 `umi-native-dsi.dtsi` 的 `&mdss` 补 `resets = <&dispcc DISP_CC_MDSS_CORE_BCR>`，沿用主线 reset controller、已有等待时间和 DRM probe 次序。原生接管会终止 ABL 扫描，再由原生 KMS 重新点屏；默认 simpledrm DTS 不增加该引用。无新增 binding、UAPI、Android 依赖或电气数值，Debian 仍消费 DRM/KMS。
- 对照 Luca Weiss 的上游 [8b4faf419d601cead68d7821618369e2c0338b86](https://github.com/torvalds/linux/commit/8b4faf419d601cead68d7821618369e2c0338b86)，同一路径 `arch/arm64/boot/dts/qcom/sm8250.dtsi`（BSD-3-Clause）。上游 patch SHA-256 `0993ece58f5b468a6876a520b460b89151ccd8fabbed6b22651c3fb60c740d1e`，下载原件只保存在 `.work/display-handoff-mdss-upstream.patch`。上游 Tested-by 针对 RB5，不继承为 Umi L3；归属见 display/NOTICE 和 sources.lock。
- 另外修正既有 MDSS helper 忽略 assert/deassert 返回值的问题：失败打印错误、释放已取得的 handle 并阻止 probe 继续；获取失败仍保留原来的 defer/错误。补丁固定原文件 SHA-256 `645e876bdb2aec5553245692936887a0d3ab8273145245fee31a0f4feb81fb01`，保持 GPL-2.0 与原版权头。缓存验证器只接受 pristine 或该唯一精确补丁；不扩展 SMMU 映射。错误传播只覆盖 reset API 已上报的错误，不声称能检出 provider 隐藏的底层 MMIO 失败。
- `isolate_abl_targets.verify(..., native=True)` 增加最终 DTB 核对：MDSS/dispcc compatible、provider 的 `#reset-cells=1`、resets phandle 和 ID=0 均须匹配。拒绝 RSCC_BCR=1、错误 provider、多项 reset 和缺失 reset，避免仅凭 DTS 文本判断接管已接通。

### 根身份复核

真实 `selected-11.dtbo` 与新原生 DTB，经既有固定 ABL LibUfdt host 重放，完整 live soc 子树逐字节不变，MDSS reset 引用保留；根仍只有 compatible/model 两项改变。重放结果 `.work/native-dsi-handoff-20261111/abl-replay.dtb` SHA-256 `2d0c5b2d54df158ad2ac7b550ec9bbc47bbccf89d3bcc62bd75d70d58a9cfabd`。

`ufdt_overlay_root_node()` 在 fragment 之前直接合并根的同名属性，不经 symbols 或 target phandle，因而当前隔离无从截获。严格审计仍报根两项 FAIL；纯 host overlay 未执行 ABL 填 RAM，还另报 RAM 零区间，未将此模拟写成一次运行时 FDT 通过。不删除根属性、不加入白名单、不在 Linux 早期伪造根，不修改设备 DTBO/ABL 分区。

完整根修复仍要求 WP3/WP4 在受控第二阶段固件向 EFI Stub 提供主线 DT：核对 Umi/SM8250 身份，保持经过验证的 memory/reserved-memory/chosen/initrd 交接，向 EFI 配置表交付 canonical 根和受保护 live soc；再按最终 FDT 整体审计及 L2/L3 撤除 UBWC quirk。当前仓库没有该阶段实现；本轮不把字符串恢复包装为完整 UEFI 功能，TD-006/RISK-010 保持开放。

### 本轮离线验证

先输出 Audit，再执行验证。原生构建命令为 `MISEED_NATIVE_DSI=1 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/native-dsi-handoff-20261111 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/native-dsi-source-20261111 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/native-dsi-build-20261111 MISEED_JOBS=6 bash tools/build_kernel_l2.sh`，退出 0；日志 `.work/display-handoff-native-build-20261111.log`。

| 原生产物 | SHA-256 |
| --- | --- |
| Image | 1c8c81dd48686cd389155eb95a71b9eb6dab269c5038c2b2324b35b3175d890f |
| DTB | ad85268b167c187832e269aa3877836aa609fb552023c5574feac6e7ba3daead |
| boot.img | f87f2f424a43aebfb7ab664b544d7f8c8bdba27099cefcabf0d4ff70be18f440 |

`/tmp/miseed-dtschema/bin/python tools/validate_kernel_dt.py --source /tmp/miseed-boot-l2/native-dsi-source-20261111 --build /tmp/miseed-boot-l2/native-dsi-build-20261111 --dtb .work/native-dsi-handoff-20261111/umi-csot.dtb`，在既有完整 PATH 下退出 0：定向 dt_binding_check、CHECK_DTBS、最终 DTB 与四项 DT 回归均通过，日志 `.work/display-handoff-native-schema-20261111.log`。

全量 unittest 使用同一 schema 环境、`MISEED_WIRED_DTB` 指向新原生 DTB、`MISEED_DT_SCHEMA` 指向对应 processed-schema，退出 0：234 项、0 跳过、73.006s。新增四项涵盖 C reset 顺序/失败释放、精确补丁的幂等/拒绝未知修改及符号链接、锚点异常、DT provider/ID 正反例。日志 `.work/display-handoff-all-tests-20261111.log`；ShellCheck error 级退出 0。

默认构建将同一命令的 `MISEED_NATIVE_DSI=0`、输出改为 `.work/simpledrm-handoff-20261111`，初始 source/build 指向 `/tmp/miseed-boot-l2/linux-standard-{source,build}-20261110`，退出 0；旧缓存的 power Makefile 不匹配当前精确补丁而被拒绝，按既有逻辑保留旧目录、另解锁定源码并全量构建，临时 source/build 正常清理。日志 `.work/display-handoff-default-build-20261111.log`。默认 boot.img SHA-256 `68e3cc7b9041f530d3cc637955f85c24941145b6e20b3f27b3989dd8d5d82b3c`；boot.img、DTB、resolved.config、rescue-init、initramfs 均与 `.work/pr6-submit-simpledrm-20261111` 逐字节相同。复用本轮 processed-schema 对默认最终 DTB 运行同范围 dt-validate，退出 0、无诊断；同时确认默认 MDSS 没有 resets、CONFIG_DRM_MSM 未启用。产物散列/重放审计统一保存 `.work/display-handoff-artifacts-20261111.json`。

复核确认 C 测试 fixture 与固定原版 `msm_mdss_reset()` 只差注释/空白；新 MDSS 源码已通过原生 L2。`verify_kernel_source.py` 对完整原生缓存再次核对固定 Git tree 与允许补丁，退出 0，日志 `.work/display-handoff-source-verify-20261111.log`。最终仓库/来源锁/预算检查与 `git diff --check` 通过，既有 8 项源码黄灯归 TD-008，本轮新增/修改自维护源码均未新增黄灯。相对既定 main 基线的本地预估为 99 文件（含新测试）、约 5240 新增行，规模维度红灯收尾、文件硬上限未越；这不是远端 PR 统计，也未扩展其他功能或触发外部评论。

本轮无 L3；不得声称 SMMU fault 或前次 DMA/FIFO 超时已消除。后续 RAM 复验需核对本次完整 boot/DTB 哈希，采集最早至完整 KMS/亮度命令后的未过滤 kmsg 与最终 FDT，并验证 Android 返回。新引入故障、持续 fault 或失联即停止，沿用已点屏的旧 `9438724f…b9d4b380` RAM 镜像/重启返回 Android；不写 boot/DTBO/ABL 分区。原生 reset 的点屏和错误恢复 L3、根标准交接与开发者以外专项复核继续阻断相应验收。

## PR6 CodeRabbit 核实与修复（2026-11-11）

用户授权将当前工作区修复提交并推送到 PR6；无需等待 GitHub CI，不授权合并。远端 PR head=`codex/linux-standard-interfaces`、SHA=`9d1c8819c5db8e102a159852b0a6c811e4608f4c`，base/main=`d9a65dca4a1a9530a44047273f61cabbb5f8b572`，与本地工作分支 HEAD 一致；保留并一并核对既有工作区修改。

已读取[完成的审核](https://github.com/Ben8368/MiSeed/pull/6#pullrequestreview-5855731914)，run ID `2f73176c-d9a8-4c76-9440-615fd45d2336`，提交于 2026-11-11 08:14:36 UTC，列出的范围是 base→上述 HEAD、98 个文件。另一次[full review 命令](https://github.com/Ben8368/MiSeed/pull/6#issuecomment-6790381869)得到[限流回复](https://github.com/Ben8368/MiSeed/pull/6#issuecomment-6790795755)，不能将其记为新完整审核已触发或完成。机器人自身文档覆盖分析仅处理 50 文件/128 函数，48 文件跳过（34 不支持、14 超过分析文件限制）；选择 98 文件不证明每一文件都被完整审查。自动审核只提供线索，下表以当前源码及回归为准。

| 意见 | 实际核实 | 处理与证据 |
| --- | --- | --- |
| [panel unprepare 状态](https://github.com/Ben8368/MiSeed/pull/6#discussion_r4535485529) | 固定 Linux `drm_panel_unprepare()` 在回调负返回时保留 panel->prepared；本地回调却已 assert reset、清 c->prepared，下一次 core prepare 因而跳过 | DCS 关闭失败仍记录错误，但完成复位后返回 0；C harness 分别注入 display-off、sleep、两者同时失败，检查 core/private flags、背光延后及再次发送完整 59 条初始化 |
| [panel 分配器](https://github.com/Ben8368/MiSeed/pull/6#discussion_r4535485537) | 固定 `drm_panel_init()` 不初始化 refcount/container，`__devm_drm_panel_alloc()` 才建立 kref、容器和 devres put；原本 devm_kzalloc 不满足该生命周期 | 改用 devm_drm_panel_alloc，处理 ERR_PTR 并移除重复 init；实际固定内核原生 ARM64 L2 验证 probe/接口可编译 |
| [有线 PPS 清理](https://github.com/Ben8368/MiSeed/pull/6#discussion_r4535485542) | 旧 cleanup 取消 worker 后忽略 umi_pps_stop 返回值，仍退订和释放状态；单次故障确会使 stop_pending 丢失 | 采用同步保留资源，不采用异步移交：标 removing/非 ready、排空 worker，按既有 200ms 周期重试完整停机事务，成功后才在无 policy lock 下退订；实际 stop/cleanup 与 IRQ/退订代码一起进入 pthread harness，停泵/PD/SMB5 三种持续故障各验证三次阻塞重试后解除，及正常/重复清理 |
| [SMB5 名称](https://github.com/Ben8368/MiSeed/pull/6#discussion_r4535485555) | 匹配表 data=pm8150b，probe 用 `%s-charger` 注册为 pm8150b-charger；用户工具及夹具误用下划线 | 工具/夹具同步改为连字符；真实名称目录上的发现、在线判断、两种写入动作及旧名忽略均有合成回归 |

有线 PPS 的同步清理在 remove/shutdown 回调返回前保持供应者、regmap、策略状态及模块代码存活，避免返回后调用悬空回调；不在 policy lock 下等待 IRQ，不扩大芯片 provider 的板级职责。取舍是持续 I/O/回退故障会阻塞卸载/关机清理；有界且安全的硬件回退仍归 TD-004/WP9。不得为限制等待时间而无条件释放资源。当前回归证明失败保持与恢复执行，不证明物理总线永久挂死可由软件恢复。

机器人建议的 80% docstring 指标不是仓库约定门禁，且其统计不完整；不为达到该统计批量填充注释。已按实际复杂生命周期补清理说明。根属性覆盖明确写入 TD-006：准确属性、ABL 原因、受限 quirk、WP3/WP4 责任、EFI 主线 DT 与动态内存/chosen 交接及 L2/L3 退出条件；未知硬件部分继续归 RISK-010。未改原厂分区或执行真机供电操作，无线 RX/TX 未修改。

### 最终本地验证与推送范围

环境沿用本轮 Windows/WSL、固定 Linux/GCC/schema 版本。先输出 Audit，再执行定向及完整验证：

- 定向 C/宿主回归：`test_bq_lifecycle.py` 1 项、`test_native_display.py` 6 项、`test_wired_power.py` 6 项通过，均无跳过；实际内核源码/标准消费者与测试夹具一并核对，不用机器人结论代替测试。
- 两套完整 L2：`.work/pr6-review-build-20261111.sh` 顺序调用 `tools/build_kernel_l2.sh`，原生使用 native-dsi-source/build，默认使用 root-default-source/build，`MISEED_NATIVE_DSI=1/0`、`MISEED_JOBS=6`，产物分别 `.work/pr6-review-native-20261111` 与 `.work/pr6-review-simpledrm-20261111`，均退出 0；日志 `.work/pr6-review-{native,simpledrm}-build-20261111.log`。
- 对各自 source/build/最终 DTB 运行 `tools/validate_kernel_dt.py`，两套定向 binding、CHECK_DTBS、最终 DTB/schema 与四项 DT 反例均退出 0、无跳过；日志 `.work/pr6-review-{native,simpledrm}-schema-20261111.log`。
- 首次全量回归误与生成 schema/构建并发，退出 1：4 项读到被截断的 processed-schema，1 项复制到刚消失的 sed 临时文件；保留 `.work/pr6-review-all-tests-20261111.log`，未将其写成产品代码失败或通过。待全部构建/schema 完成后复制固定 schema 到 `.work/pr6-review-processed-schema-20261111.json`，以新原生 DTB 和该快照设置 `MISEED_WIRED_DTB/MISEED_DT_SCHEMA`，顺序重跑 `python -X utf8 -m unittest discover -s tests -v`，退出 0：**235 项、0 跳过、49.767s**；日志 `.work/pr6-review-final-tests-20261111.log`。
- `verify_kernel_source.py` 核验完整 native cache 与固定 Git tree/精确补丁，退出 0；ShellCheck error 级、仓库/输入哈希/文档预算及 diff 检查通过。日志 `.work/pr6-review-source-verify-20261111.log`。既有 8 项源码黄灯归 TD-008，修改后的 cleanup/harness 未新增黄灯。

| 本轮载荷 | boot.img SHA-256 | DTB SHA-256 |
| --- | --- | --- |
| 原生 | f7789fffe124197d991ecb7aa5e686c01b0bcb214d43ef7473d0326eed477000 | ad85268b167c187832e269aa3877836aa609fb552023c5574feac6e7ba3daead |
| simpledrm | 7bd5ada203ec7166ee1ffcb0e12eec00fbe6e7cacbbeb4db6b889059e67e58d6 | 5cbd07260ec34a7d10bc98bc687f4ee01b05a5754ae31776ae0131eb10ccb184 |

本轮没有新 L3：新 reset/SMMU、panel 生命周期与有线 PPS 拆除恢复的真机结论仍待验；此前点屏和有线 PD/PPS/旁路 L3 继续保留。既有工作区的 QUALITY 收尾例外规则、DTBO 输出路径 resolve、诊断 harness 的 stdlib include 与对应来源锁一并验证、提交。构建物、原始 DTBO/运行树及长日志不入库。推送仅更新原有 PR6 工作分支，精确 HEAD、GitHub 规模和查询当时的异步 CI/机器人状态以 PR 描述和本轮汇报为准，不等待完成，不合并。
