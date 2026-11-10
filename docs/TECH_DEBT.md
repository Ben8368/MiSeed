# 技术债

只记有意保留的实现妥协；未知假设归 RISK_REGISTER。关闭后按 GOVERNANCE 归档。

## TD-001：构建环境闭包不完整（P2，2026-11-05）

取舍：首个 Boot L2 固定 Linux、DTS、配置、header 模板及直接工具版本；Ubuntu runner 和 apt 传递依赖尚未逐包哈希锁定。Boot L2 已增加来源/脚本/本地输入的一致性检查，但仍非声明式依赖解析器，完整闭包及自动消费锁文件继续延期。

影响：首个 Umi/CSOT boot L2 本机和 hosted Ubuntu 24.04 均通过，`boot.img` 哈希完全一致，证据见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。PR #1 首次 hosted Boot L2 run 因全 ARM64 `defconfig` 编译无关驱动、达到 30 分钟上限取消；脚本已改为 `allnoconfig` + 板级配置。直接 apt 包版本已钉住，传递包哈希和 runner 镜像 digest 未闭合；UEFI/rootfs 尚无构建 job，分支保护依维护者决定延期。

偿还：首个 L2 的远端结果与本机构建已独立比对一致；剩余闭合传递依赖与 runner digest，再分别接入 UEFI/rootfs 构建和产物校验。

## TD-002：TLMM 最小恢复尚缺本机验证（P1，2026-11-07）

取舍：原 GPIO-free RAM 诊断禁用 TLMM。PPS 测试 profile 按 LOS 接线只开放 UART12 GPIO34/35、I2C15 GPIO44/45、BQ IRQ GPIO68，其余仍保留；GPIO8 暂未接入。下游 TLMM overlay 继续隔离；来源见[续查](archive/research/2026-11-08-pps-topology-followup.md)。扩展掩码已通过 ARM64 L2，尚无新镜像 L3，不能继承旧镜像验证。本机两轮直接 Fastboot 证据及参考来源见[记录](archive/hardware/2026-11-07-direct-fastboot.md)。

影响：不能将此配置作为 GPIO、触控或完整板级电源支持；PDC 首版无 USB 且回 Android，无崩溃栈，具体故障引脚仍未知，不冒险批量读取 GPIO 寄存器。

偿还：从匹配本机的可信来源锁定有效/保留 GPIO 范围和实际消费者，独立审查后按需恢复 TLMM，以 L2 与授权 L3 重验 USB/PON 往返及受影响外设。

## TD-003：NFC Android 私有 ABI（P1，2026-11-10；WP10）

源码偿还：移除 NQ 字符设备/ioctl 与私有 UAPI 默认构建输入，替换为 SN100T Linux NFC/NCI transport、标准资源 binding 和只读 Netlink 消费者，旧来源与 L3 保留为历史。验证见[适配记录](archive/research/2026-11-10-sn100t-linux-nci.md)。剩余：新驱动 reset/init、RF/NDEF、错误/关闭恢复 L3、Debian 服务与独立复核；本项仍开放，未知固件/共享 VEN/eSE 能力归 RISK-011。

## TD-004：有线充电诊断与私有协调（P1，2026-11-10；WP9）

源码偿还：FG/SMB5/BQ 已采用 power_supply，修正 FG 电流符号/SMB5 ADC 换算，诊断独立可选；BQ 身份检查、停泵后配置/读回、故障快照与 ADC 下沉芯片后端，见[标准接口完善](archive/reviews/2026-11-10-linux-standard-interfaces.md)。umi_smb5_* 保留失败锁定互锁，不以用户空间分步写入替代；独立通用芯片驱动、板级策略拆分及所有权专项复核仍开放。新 ABI/保护恢复 L3 在本分支继续，Debian 消费后续进行；既有稳定运行结论不变。

## TD-005：有线温控模型与验收（P1，2026-11-10；WP9/WP6）

源码偿还：有线 PPS 六路 thermal zone/cooling device 保留；有线固定 PD 新增独立电池温度 zone 与二态停充 cooling，沿用 45°C 暖端，冷端/读错独立保护保留，见[标准接口完善](archive/reviews/2026-11-10-linux-standard-interfaces.md)。未把 PPS 多传感器/FCC 表移用于固定 PD，wireless_therm 名称不代表无线 RX/TX。剩余：板级标度、两条有线路径的超温/读错/恢复 L3、完整板级热模型与独立复核；未因标准节点存在关闭本债务。

## TD-006：诊断启动与用户空间临时路径（P2，2026-11-10；WP2–WP5）

偿还：继承 framebuffer 已改上游 simpledrm/KMS + fbdev 控制台；撤除独立 observer 与虚构 DT 节点，原始只读采样归 SMB5 可选诊断，见[续修](archive/reviews/2026-11-10-mainline-interface-followup.md)。剩余原生 DSI/面板、资源/PM 交接、Debian 服务；ignore_unused、强制诊断命令行、静态 rescue/CHARGE_LOG 与 TCPM debugfs 仍限 bring-up 用途。Android boot header/Fastboot 属 ABL 加载契约，UEFI 产品路径按 WP3/WP4 验收；DTBO/GPIO 风险另见 RISK-010/TD-002。

## TD-007：本地驱动 binding 与 DT 语义（P1，2026-11-10；WP2/WP9/WP10）

源码偿还：FG/SMB5/BQ25970 与 NFC 已补 YAML，电池/遥测/真实供电源引用已修；撤除 observer 虚构节点。有线 BQ IRQ 删除 TLMM 不支持的 input-enable，输入方向由 IRQ 资源申请设置；新增固定 PD thermal binding，并将 TLMM 加入有线检查范围，见[标准接口完善](archive/reviews/2026-11-10-linux-standard-interfaces.md)。剩余新 DT/标准消费者 L3、binding 上游评审、整机 schema/运行时 DTBO 与专项复核；定向检查不等于整机合规。

## TD-008：源码篇幅黄灯（P2，2026-11-10）

责任与保留原因：WP9 的 `kernel/power/` 下 `qcom_fg_core.c`（567）、`qcom_fg_io.c`（496）、`qcom_fg_measure.c`（351）、`qcom_pm8150b_hw.c`（330）、`umi_pps.c`（506）分别保留 FG 核心、SRAM 事务、代际读数、有线硬件访问、PPS 状态机完整性；同包 `tests/pps_harness.c`（599）保留故障注入桩。WP10 的 `kernel/nfc/sn100x.c`（325）保留传输/PM；WP0 的 `tests/test_boot_l2.py`（355）保留缓存/平台回归。括号为行数。

偿还：各工作包下次修改时复核内聚性，提取独立接口、代际后端或测试职责；所有相关文件≤300 行且适用回归通过后关闭。拆分与验证见[记录](archive/reviews/2026-11-10-source-responsibility-split.md)，本项不豁免红灯或供电安全要求。
