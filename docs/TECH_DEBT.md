# 技术债

只记实现妥协；未知归 RISK_REGISTER，关闭后按 GOVERNANCE 归档。

## TD-001：构建环境闭包不完整（P2，2026-11-05）

取舍：Boot L2 固定 Linux、DTS、配置、header 与直接工具版本，并校验来源/脚本/输入一致性；Ubuntu runner、apt 传递依赖哈希与声明式依赖解析尚未闭合。

影响：首个 Umi/CSOT boot L2 本机和 hosted Ubuntu 24.04 均通过，`boot.img` 哈希完全一致，证据见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。PR #1 首次 hosted Boot L2 run 因全 ARM64 `defconfig` 编译无关驱动、达到 30 分钟上限取消；脚本已改为 `allnoconfig` + 板级配置。直接 apt 包版本已钉住，传递包哈希和 runner 镜像 digest 未闭合；UEFI/rootfs 尚无构建 job，分支保护依维护者决定延期。

偿还：闭合传递依赖与 runner digest，接入 UEFI/rootfs 构建与产物校验。

## TD-002：TLMM 最小恢复尚缺本机验证（P1，2026-11-07）

取舍：原 GPIO-free RAM 诊断禁用 TLMM。有线PPS按LOS接线开放UART12 GPIO34/35、I2C15 GPIO44/45、BQ IRQ GPIO68；NCI另开专用脚。原生DSI另开GPIO12复位；[90Hz移植](archive/research/2026-11-16-display-90hz-port.md)再开GPIO50选择，新增脚L3待验。GPIO8未接入，下游TLMM overlay仍隔离。旧来源见[续查](archive/research/2026-11-08-pps-topology-followup.md)。

影响：不能将此配置作为 GPIO、触控或完整板级电源支持；PDC 首版无 USB 且回 Android，无崩溃栈，具体故障引脚仍未知，不冒险批量读取 GPIO 寄存器。

偿还：从匹配本机的可信来源锁定有效/保留 GPIO 范围和实际消费者，独立审查后按需恢复 TLMM，以 L2 与授权 L3 重验 USB/PON 往返及受影响外设。

## TD-003：NFC Android 私有 ABI（P1，2026-11-10；WP10）

源码偿还：移除 NQ 字符设备/ioctl 与私有 UAPI 默认构建输入，替换为 SN100T Linux NFC/NCI transport、标准资源 binding 和只读 Netlink 消费者，旧来源与 L3 保留为历史。验证见[适配记录](archive/research/2026-11-10-sn100t-linux-nci.md)。剩余：新驱动 reset/init、RF/NDEF、错误/关闭恢复 L3、Debian 服务与独立复核；本项仍开放，未知固件/共享 VEN/eSE 能力归 RISK-011。

## TD-004：有线充电诊断与私有协调（P1，2026-11-10；WP9）

源码偿还：撤charge_test及跨模块私有导出，互锁内聚同模块，外部power_supply/thermal。TCPM限压仍为本地扩展；BQ持续I²C失败仍阻塞退出。见[审计](archive/reviews/2026-11-16-linux-interface-closure.md)。

偿还：WP9闭合有界失败回退、watchdog到期/恢复L3、切档/热暂停/复充生命周期专项复核；TCPM提案需子系统评审，Debian及其他板型待验。

## TD-005：有线温控模型与验收（P1，2026-11-10；WP9/WP6）

源码偿还：有线PPS保留六路thermal/FCC硬限额，新增提前降额、低流PPS、停泵保留合约及冷却恢复；固定PD新增独立BMS温度趋势降流/回升，保留电池温度zone与二态cooling，45°C暖端/冷端/读错保护不变。SMB5 cooling停充/释放已有SDP L3；普通PPS降额/回升已有6→56%L3；热暂停再启泵、30W目标实际效果、固定9V及PD趋势反馈/热触发、温标、真实越限/读错及独立复核待验。PPS多传感器/FCC表未直接移用于固定PD；wireless_therm非无线RX/TX。

## TD-006：诊断启动、根属性交接与临时用户空间（P2，2026-11-10；WP2–WP5）

取舍：ABL 直启仍覆盖根节点 `/` 的 `compatible/model`；保留仅 CSOT profile 核对完整 Umi/SM8250 身份的 UBWC quirk，严格 FDT 审计继续 FAIL。第二阶段 UEFI 尚未实现；不改原厂 DTBO/ABL，不在 Linux 内伪造根。保留 ignore_unused、诊断命令行、静态 rescue/CHARGE_LOG、只读fbdev缩略图、TCPM debugfs；见[证据](archive/research/2026-11-11-native-dsi-chip-boundaries.md)。

责任/退出：WP3/WP4 用 EFI 配置表交接主线 DT，正确保留 memory/reserved-memory/chosen/initrd，L2/L3 整体审计通过后撤除 quirk。WP2 验证新 MDSS reset 的 SMMU 接管；变体、面板独立供电/PM 与 Debian 标准消费者待验。Android boot header/Fastboot 只用于诊断；DTBO 未知项/GPIO 归 RISK-010/TD-002。

## TD-007：本地驱动 binding 与 DT 语义（P1，2026-11-10；WP2/WP9/WP10）

源码偿还：FG/SMB5/BQ/NFC 已补 YAML，供电引用已修、observer 虚构节点已撤除。有线BQ IRQ删除不支持的input-enable，新增固定PD thermal binding及TLMM检查，见[标准接口完善](archive/reviews/2026-11-10-linux-standard-interfaces.md)。CSOT已有binding/显示链路schema/FDT审计；新增必需disprate-gpios描述GPIO50高=90Hz，WP2按[移植审查](archive/research/2026-11-16-display-90hz-port.md)提交DRM/DT评审。剩余新DT/标准消费者L3、binding上游评审、整机schema/实际DTBO与专项复核；定向检查不等于整机合规。

## TD-008：源码篇幅黄灯（P2，2026-11-10）

责任/原因：WP9 `kernel/power/qcom_fg_core.c`（567）、`qcom_fg_io.c`（496）、`qcom_fg_measure.c`（351）、`qcom_pm8150b_hw.c`（360）、`umi_pps.c`（480）、`umi_pps_policy.h`（325）承载电量计、SMB5/PPS 与互锁；`tests/pps_harness.c`（487）为传输/生命周期桩。WP10 `kernel/nfc/sn100x.c`（381）保留传输/PM及分阶段诊断重试；WP0 `tests/test_boot_l2.py`（355）保留缓存回归；`tools/build_kernel_l2.sh`（301）保留构建/打包顺序，下次按配置/打包拆分。WP2 `kernel/umi-csot.dts`（318）集中板级节点与 ABL 锚点审计，下次扩展拆为可编译功能组。

偿还：各WP下次修改时拆分接口、代际后端或测试职责；全部≤300行且回归通过后关闭。见[记录](archive/reviews/2026-11-10-source-responsibility-split.md)，不豁免红灯和供电安全。
