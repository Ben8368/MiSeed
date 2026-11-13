# 风险与开放假设

优先级：P0 阻断安全/发布；P1 阻断相关能力声明；P2 可延期但须可见。维护者负责关闭，外部证据须带日期与输入身份。实现妥协见 [TECH_DEBT](TECH_DEBT.md)。

| 编号 | 风险与现有证据 | 阻断边界 | 关闭条件 |
| --- | --- | --- | --- |
| RISK-001 / P1 | 设备 umi / Mi 10 / kona，维护者报告 CSOT、panel ID 0B。既有 simpledrm/USB L3 保留。11-11 同次 ADB 已交叉核实当前 release=16/SDK=36、LOS 23.2-20261108，同时系统/vendor 指纹保留 Xiaomi Android 13；不混用指纹与当前系统版本，旧日期快照仍作历史，见[本机采集](archive/hardware/2026-11-11-umi-native-dsi-first-l3.md)。DTBO 已锁定；其余底层固件、UFS、SKU/物理板号仍待核 | 声称整套固件/物理变体及 ABL handoff 完全确认，或原生面板/触控可用 | 以本机匹配输入核实其余底层固件、保留区与面板；当前 OS 字段已澄清，不据旧快照推断刷机时点；按 L3 分项关闭 |
| RISK-002 / P1 | 首个 Umi/CSOT boot L2 已在 Ubuntu hosted CI、WSL2 和 Apple Silicon macOS/Colima-Rosetta 构建，镜像哈希一致；runner digest、apt 传递包哈希和逐文件许可审计仍开放 | 可复现的完整 MiSeed 构建闭包、设备可启动或发布声明 | 闭合工具与依赖哈希/许可，PR 同 HEAD L2 CI 通过并独立重建一致；设备可启动声明另需维护者 RAM 测试 L3 |
| RISK-003 / P1（恢复限制） | 维护者报告曾以官方工具成功线刷且身份信息保持；官方包脚本静态审查未见身份/NV 目标，允许 `misc` 随官方流程更新。身份/NV 仍严格禁访；重锁 BL 绝对禁止。MiSeed 专用回退演练尚无 | 宣称 MiSeed 镜像已验证可回退；任何 BL 锁定操作 | 复用维护者既有官方恢复经验作回退依据，不要求重复官方流程；MiSeed 特有失败回退仅在后续获授权 L3 评估，流程始终维持 BL 解锁、排除 lock 脚本并保护身份/NV |
| RISK-004 / P0（供电/发布） | 旧有线稳定/旁路/watchdog及6→56%约49分钟PPS、1.2→2.0A恢复L3保留。新30W/3A、固定9V/2A与80/71策略见[审查](archive/reviews/2026-11-13-wired-policy.md)；历史超过3A为维护者报告，IBUS/IBAT分列。 | 新策略未完成L3；旧热跃迁定位结论保留，较早5→52%错误仍未知 | 新9V切档/热反馈、80/71循环、本版3A与热暂停/故障锁L3，硬件保护/温标、独立复核及精确HEAD CI |
| RISK-005 / P1 | 自有原创内容已按 ADR-0005 采用 AGPL-3.0-only；第三方逐文件许可、GPL-2.0-only 隔离、安装信息及适用网络源码义务仍需审查 | 第三方代码/固件导入与公开分发 | 保留来源/NOTICE，完成文件级兼容审查、对应源代码、安装信息和网络交互源码提供方案 |
| RISK-006 / P2（维护者延期） | 保护 API 于 2026-11-05 返回 403 `Resource not accessible by integration`，当前仓库未启用分支保护。维护者决定等项目跑通并将仓库公开后再配置；届时以实际仓库套餐/规则能力复核 | 在配置前，不声称 PR 必需、检查必需或禁止绕过已生效；不阻断当前本地 L2 开发 | 项目跑通且仓库公开后，在 GitHub UI 配置并验证 Require PR、六项 CI、独立审查与更新后重审、禁止 Force Push/删除和未经授权绕过；记录实际规则与同 HEAD PR 结果 |
| RISK-007 / P1 | EDL Programmer 的来源、签名、授权和兼容性未知 | 声称 9008 可刷或可恢复 | 外部前提可核实并有独立授权的设备验证，否则继续研究 |
| RISK-009 / P2（历史设备标识） | PR1 的 4 处和 PR3 的 2 处设备序列号已在当前树脱敏，但 `8f67d5f` 及已合并历史仍有副本，见[核实记录](archive/reviews/2026-11-06-pr1-coderabbit-verification.md) | 不宣称托管历史已脱敏；不得擅自改写已推送历史 | 维护者在公开分发前复核历史与导出副本并明确处理策略；历史清理须另行授权 |

| RISK-010 / P1（运行时 DTBO/显示交接） | DTBO 重放、隔离、live soc/aliases/phandle/RAM/UBWC L3；旧镜像原生点屏 L3 见[记录](archive/hardware/2026-11-11-umi-native-dsi-first-l3.md)。HEAD `2bdb266` RAM/USB/FDT 采集成功，但 DSI PLL lock failure、clk warning、MDSS probe 栈及独立 SMMU deferred timeout；未见旧 SID fault/DMA/FIFO，未作亮度/目视，root compatible/model 差异仍使严格审计 FAIL；见[历史尝试](archive/hardware/2026-11-11-umi-mdss-reset-smmu-attempt-l3.md)。MDSS reset L2/schema/反例见[续修](archive/research/2026-11-11-native-dsi-chip-boundaries.md) | 新镜像未复现旧 SID fault，但 PLL/MDSS 失败，不能宣称 reset/SMMU 交接闭合；ABL 根差异仍开放，TD-006 不豁免 | 修复 PLL/MDSS 与 SMMU 依赖后，以精确新镜像验证点屏、fault 时间线、Android 回退；WP3/WP4 交接及独立复核。TLMM 归 TD-002 |
| RISK-011 / P1（SN100T Linux NCI / eSE） | Linux 6.18.60 transport L2 与 Windows/WSL transport L1 回归通过；11-13 RAM L3 的 reset/init、空协议拒绝、1 个 RF target（protocol 4）、close/reopen 通过。普通卡未读 NDEF/UID。QUP1 对齐 Android DT 1 MHz 并加 CORE_RESET 有界重试后 `-ENXIO` 未复现，成功日志无法区分两者作用。GPIO 映射与活动 DT 一致，不代表 NFCC 内部启动状态。SN100_SE/NFC 共用 PMU；主机 VEN 复位规范未知。见[NFC 适配](archive/research/2026-11-10-sn100t-linux-nci.md)与[真机记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)。 | 不宣称 NDEF、PM、HCE/eSE 可用；不拉低共享 VEN、不冒用 compatible 或将旧 10ms 脉冲当规范。Debian 集成属后续阶段 | 后续验证只读 NDEF、PM、失败恢复；Debian 用户空间集成另阶段。若再遇 NACK，采集控制器/总线阶段；取得 SN100T 复位规范及 eSE 并发条件；HCE/eSE 分项验证或声明不支持。旧 ABI 归 TD-003 |

关闭条目按 [GOVERNANCE](GOVERNANCE.md) 移入带日期归档，不以“已登记”解除阻断。
