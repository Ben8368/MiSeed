# 技术债

只记有意保留的实现妥协；未知假设归 RISK_REGISTER。关闭后按 GOVERNANCE 归档。

## TD-001：构建环境闭包不完整（P2，2026-11-05）

取舍：Boot L2 固定 Linux、DTS、配置、header 与直接工具版本，并校验来源/脚本/输入一致性；Ubuntu runner、apt 传递依赖哈希与声明式依赖解析尚未闭合。

影响：首个 Umi/CSOT boot L2 本机和 hosted Ubuntu 24.04 均通过，`boot.img` 哈希完全一致，证据见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。PR #1 首次 hosted Boot L2 run 因全 ARM64 `defconfig` 编译无关驱动、达到 30 分钟上限取消；脚本已改为 `allnoconfig` + 板级配置。直接 apt 包版本已钉住，传递包哈希和 runner 镜像 digest 未闭合；UEFI/rootfs 尚无构建 job，分支保护依维护者决定延期。

偿还：闭合传递依赖与 runner digest，接入 UEFI/rootfs 构建与产物校验。

## TD-002：TLMM 最小恢复尚缺本机验证（P1，2026-11-07）

取舍：原 GPIO-free RAM 诊断禁用 TLMM。PPS 测试 profile 按 LOS 接线只开放 UART12 GPIO34/35、I2C15 GPIO44/45、BQ IRQ GPIO68，其余仍保留；原生 DSI profile 另开放 GPIO12 复位，待 L3；GPIO8 暂未接入。下游 TLMM overlay 继续隔离；来源见[续查](archive/research/2026-11-08-pps-topology-followup.md)。本轮掩码 L2、新镜像 L3 待验；旧 Fastboot 证据见[记录](archive/hardware/2026-11-07-direct-fastboot.md)。

影响：不能将此配置作为 GPIO、触控或完整板级电源支持；PDC 首版无 USB 且回 Android，无崩溃栈，具体故障引脚仍未知，不冒险批量读取 GPIO 寄存器。

偿还：从匹配本机的可信来源锁定有效/保留 GPIO 范围和实际消费者，独立审查后按需恢复 TLMM，以 L2 与授权 L3 重验 USB/PON 往返及受影响外设。

## TD-003：NFC Android 私有 ABI（P1，2026-11-10；WP10）

源码偿还：移除 NQ 字符设备/ioctl 与私有 UAPI 默认构建输入，替换为 SN100T Linux NFC/NCI transport、标准资源 binding 和只读 Netlink 消费者，旧来源与 L3 保留为历史。验证见[适配记录](archive/research/2026-11-10-sn100t-linux-nci.md)。剩余：新驱动 reset/init、RF/NDEF、错误/关闭恢复 L3、Debian 服务与独立复核；本项仍开放，未知固件/共享 VEN/eSE 能力归 RISK-011。

## TD-004：有线充电诊断与私有协调（P1，2026-11-10；WP9）

取舍：有线PPS保留umi_smb5_*互锁、固定5V复充阶段及故障锁；新增TCPM标准input_voltage_limit扩展与固定PD有序切档，见[审查](archive/reviews/2026-11-13-wired-policy.md)。BQ持续I²C失败仍阻塞卸载/关机。

偿还：WP9闭合有界失败回退、watchdog到期/恢复L3、切档/热暂停/复充生命周期专项复核；TCPM提案需子系统评审，Debian及其他板型待验。

## TD-005：有线温控模型与验收（P1，2026-11-10；WP9/WP6）

源码偿还：有线PPS保留六路thermal/FCC硬限额，新增提前降额、低流PPS、停泵保留合约及冷却恢复；固定PD新增独立BMS温度趋势降流/回升，保留电池温度zone与二态cooling，45°C暖端/冷端/读错保护不变。SMB5 cooling停充/释放已有SDP L3；普通PPS降额/回升已有6→56%L3；热暂停再启泵、30W目标实际效果、固定9V及PD趋势反馈/热触发、温标、真实越限/读错及独立复核待验。PPS多传感器/FCC表未直接移用于固定PD；wireless_therm非无线RX/TX。

## TD-006：诊断启动、根属性交接与临时用户空间（P2，2026-11-10；WP2–WP5）

取舍：ABL 直启仍覆盖根节点 `/` 的 `compatible/model`；保留仅 CSOT profile 核对完整 Umi/SM8250 身份的 UBWC quirk，严格 FDT 审计继续 FAIL。第二阶段 UEFI 尚未实现；不改原厂 DTBO/ABL，不在 Linux 内伪造根。保留 ignore_unused、诊断命令行、静态 rescue/CHARGE_LOG、TCPM debugfs；见[证据](archive/research/2026-11-11-native-dsi-chip-boundaries.md)。

责任/退出：WP3/WP4 用 EFI 配置表交接主线 DT，正确保留 memory/reserved-memory/chosen/initrd，L2/L3 整体审计通过后撤除 quirk。WP2 验证新 MDSS reset 的 SMMU 接管；变体、面板独立供电/PM 与 Debian 标准消费者待验。Android boot header/Fastboot 只用于诊断；DTBO 未知项/GPIO 归 RISK-010/TD-002。

## TD-007：本地驱动 binding 与 DT 语义（P1，2026-11-10；WP2/WP9/WP10）

源码偿还：FG/SMB5/BQ/NFC 已补 YAML，供电引用已修、observer 虚构节点已撤除。有线 BQ IRQ 删除 TLMM 不支持的 input-enable，输入方向由 IRQ 资源申请设置；新增固定 PD thermal binding，并将 TLMM 加入有线检查范围，见[标准接口完善](archive/reviews/2026-11-10-linux-standard-interfaces.md)。新增 CSOT binding、显示链路定向 schema 与最终 FDT 审计；剩余新 DT/标准消费者 L3、binding 上游评审、整机 schema/实际 DTBO 与专项复核；定向检查不等于整机合规。

## TD-008：源码篇幅黄灯（P2，2026-11-10）

责任与保留原因：WP9 的 `kernel/power/` 下 `qcom_fg_core.c`（567）、`qcom_fg_io.c`（496）、`qcom_fg_measure.c`（351）、`qcom_pm8150b_hw.c`（330）、`umi_pps.c`（480）分别保留 FG 核心、SRAM 事务、代际读数、有线硬件访问、PPS状态机完整性及故障定位；同包`qcom_pm8150b_policy.c`（340）、`umi_pps_policy.h`（325）保留有线互锁和共享纯策略，TCPM协商已独立为142行单元；同包 `tests/pps_harness.c`（487）保留传输/生命周期桩，保护反例已提取。WP10 的 `kernel/nfc/sn100x.c`（325）保留传输/PM；WP0 的 `tests/test_boot_l2.py`（355）保留缓存/平台回归。括号为行数。

偿还：各工作包下次修改时复核内聚性，提取独立接口、代际后端或测试职责；所有相关文件≤300 行且适用回归通过后关闭。拆分与验证见[记录](archive/reviews/2026-11-10-source-responsibility-split.md)，本项不豁免红灯或供电安全要求。
