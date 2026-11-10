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

取舍：RQ-13 曾采用 NQ-NCI `/dev/nq-nci`/ioctl 作 bring-up；RQ-14 已改为 Linux 目标，暂保留以关联旧证据，不扩建 Android HAL。偿还：按 [NFC](NFC.md) 完成 Linux NFC/NCI、binding 与 Debian 标准入口分项验证后移除旧 ABI 默认依赖；SN100T 未知能力归 RISK-011。

## TD-004：有线充电诊断与私有协调（P1，2026-11-10；WP9）

取舍：FG/SMB5 已使用 power_supply、PD/PPS 已用 TCPM；但 BQ25970 未注册独立 power_supply，控制/状态依赖 `fastcharge_test` 与日志，SMB5 保留 `charge_test`，泵/SMB5 通过内部 `umi_smb5_*` 协调。偿还：补标准供电状态/通知与适用控制接口，审查驱动内部所有权和失败退出，迁移 Debian 消费者后将私有命令限于诊断；有线 PD/PPS 回归及保护专项不可省略，不以重命名节点代替实现。不影响已通过的有线稳定运行结论。

## TD-005：有线温控策略未接入 thermal（P1，2026-11-10；WP9/WP6）

取舍：有线 PPS 用 IIO 温度采样，但虚拟温度/限流策略在私有驱动中；`wireless_therm` 为被复用的传感器名称，不代表无线驱动。偿还：核实标度与板级模型，接入适用 thermal zone/cooling 接口并保留独立保护，取得超温/传感器失败/恢复 L3；无线 RX/TX 另做设计。

## TD-006：诊断启动与用户空间临时路径（P2，2026-11-10；WP2–WP5）

取舍：ABL framebuffer/fbcon、强制诊断命令行与 ignore_unused、静态 rescue init/CHARGE_LOG、TCPM debugfs/PMIC printk 支撑 bring-up。偿还：完成显示 DRM/KMS、资源/PM 交接与 Debian 服务，通过标准接口提供产品功能，诊断保留独立用途。Android boot header/Fastboot 是 ABL 加载契约，无须仅因名称移除；UEFI 产品路径仍按 WP3/WP4 验收。DTBO/GPIO 风险另见 RISK-010/TD-002。

## TD-007：本地驱动 binding 与 DT 语义（P1，2026-11-10；WP2/WP9/WP10）

取舍：本地 NFC/FG/SMB5/PPS/observer 扩展缺少 YAML/schema 证据；PPS 将标准 `monitored-battery` 指向 FG，偏离 simple-battery 语义，SMB5 `power-supplies` 用作 BMS 查找也须核对拓扑。偿还：按固定上游定义修正消费者/提供者、配套驱动查找与 binding，逐项通过 dt_binding_check/dtbs_check、L2 与受影响 L3；不得只改 DTS 造成驱动失配，也不冒用近似芯片 compatible。详见 [接口审计](archive/reviews/2026-11-10-linux-interface-audit.md)。
