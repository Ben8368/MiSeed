# 技术债

只记有意保留的实现妥协；未知假设归 RISK_REGISTER。关闭后按 GOVERNANCE 归档。

## TD-001：构建环境闭包不完整（P2，2026-11-05）

取舍：首个 Boot L2 固定 Linux、DTS、配置、header 模板及直接工具版本；Ubuntu runner 和 apt 传递依赖尚未逐包哈希锁定。

影响：首个 Umi/CSOT boot L2 本机和 hosted Ubuntu 24.04 均通过，`boot.img` 哈希完全一致，证据见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。PR #1 首次 hosted Boot L2 run 因全 ARM64 `defconfig` 编译无关驱动、达到 30 分钟上限取消；脚本已改为 `allnoconfig` + 板级配置。直接 apt 包版本已钉住，传递包哈希和 runner 镜像 digest 未闭合；UEFI/rootfs 尚无构建 job，分支保护依维护者决定延期。

偿还：首个 L2 的远端结果与本机构建已独立比对一致；剩余闭合传递依赖与 runner digest，再分别接入 UEFI/rootfs 构建和产物校验。

## TD-002：TLMM 最小恢复尚缺本机验证（P1，2026-11-07）

取舍：原 GPIO-free RAM 诊断禁用 TLMM。PPS 测试 profile 按 LOS 接线只开放 UART12 GPIO34/35、I2C15 GPIO44/45、BQ IRQ GPIO68，其余仍保留；GPIO8 暂未接入。下游 TLMM overlay 继续隔离；来源见[续查](archive/research/2026-11-08-pps-topology-followup.md)。扩展掩码已通过 ARM64 L2，尚无新镜像 L3，不能继承旧镜像验证。本机两轮直接 Fastboot 证据及参考来源见[记录](archive/hardware/2026-11-07-direct-fastboot.md)。

影响：不能将此配置作为 GPIO、触控或完整板级电源支持；PDC 首版无 USB 且回 Android，无崩溃栈，具体故障引脚仍未知，不冒险批量读取 GPIO 寄存器。

偿还：从匹配本机的可信来源锁定有效/保留 GPIO 范围和实际消费者，独立审查后按需恢复 TLMM，以 L2 与授权 L3 重验 USB/PON 往返及受影响外设。
