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

源码偿还：FG/SMB5/BQ 已采用 power_supply；新增修正 FG 充电正号与有线消费者、SMB5 ADC 输入电流换算，抽出 BQ 寄存器层，诊断独立可选，见[续修](archive/reviews/2026-11-10-mainline-interface-followup.md)。umi_smb5_* 保留失败锁定互锁，不能以非原子的用户空间写入替代；完整芯片驱动/板级策略拆分和所有权专项复核仍待完成。剩余新 ABI 的有线 PD/PPS、保护恢复 L3、Debian 消费与独立复核；既有稳定运行结论不变。

## TD-005：有线温控策略未接入 thermal（P1，2026-11-10；WP9/WP6）

本轮源码偿还：有线 PPS 的既有六路模型接入 thermal zone/cooling device，DT 显式绑定既有 trip/滞回/FCC 档位，独立采样与保护继续生效；wireless_therm 仅是被复用的 ADC 名称。剩余：核实板级温度标度、超温/传感器失败/恢复 L3、固定 PD 热策略及独立复核；未将无线 RX/TX 纳入该 zone。验证范围见[记录](archive/reviews/2026-11-10-wired-mainline-power.md)，未因增加节点关闭本债务。

## TD-006：诊断启动与用户空间临时路径（P2，2026-11-10；WP2–WP5）

偿还：继承 framebuffer 已改上游 simpledrm/KMS + fbdev 控制台；撤除独立 observer 与虚构 DT 节点，原始只读采样归 SMB5 可选诊断，见[续修](archive/reviews/2026-11-10-mainline-interface-followup.md)。剩余原生 DSI/面板、资源/PM 交接、Debian 服务；ignore_unused、强制诊断命令行、静态 rescue/CHARGE_LOG 与 TCPM debugfs 仍限 bring-up 用途。Android boot header/Fastboot 属 ABL 加载契约，UEFI 产品路径按 WP3/WP4 验收；DTBO/GPIO 风险另见 RISK-010/TD-002。

## TD-007：本地驱动 binding 与 DT 语义（P1，2026-11-10；WP2/WP9/WP10）

本轮偿还有线部分：新增 FG/SMB5/BQ25970 YAML；同时修正驱动查找和 DTS，monitored-battery 只指向 simple-battery，电量计用独立遥测 phandle，TCPM 作为真实 power-supplies；定向 schema 与 L2 见[记录](archive/reviews/2026-11-10-wired-mainline-power.md)。剩余：新 DT/标准消费者 L3、binding 上游评审，整机 schema 与运行时 DTBO 拓扑；observer 虚构 DT 节点已撤除。NFC 新增专用 YAML 与标准 GPIO/IRQ 属性，证据见[NFC 适配](archive/research/2026-11-10-sn100t-linux-nci.md)；不将本地 schema 通过写成上游合入或整机 DT 合规。

## TD-008：源码篇幅黄灯（P2，2026-11-10）

责任与保留原因：WP9 的 `kernel/power/` 下 `qcom_fg_core.c`（567）、`qcom_fg_io.c`（496）、`qcom_fg_measure.c`（351）、`qcom_pm8150b_hw.c`（324）、`umi_pps.c`（465）分别保留 FG 核心、SRAM 事务、代际读数、有线硬件访问、PPS 状态机完整性；同包 `tests/pps_harness.c`（590）保留故障注入桩。WP10 的 `kernel/nfc/sn100x.c`（325）保留传输/PM；WP0 的 `tests/test_boot_l2.py`（355）保留缓存/平台回归。括号为行数。

偿还：各工作包下次修改时复核内聚性，提取独立接口、代际后端或测试职责；所有相关文件≤300 行且适用回归通过后关闭。拆分与验证见[记录](archive/reviews/2026-11-10-source-responsibility-split.md)，本项不豁免红灯或供电安全要求。
