# 风险与开放假设

优先级：P0 阻断安全/发布；P1 阻断相关能力声明；P2 可延期但须可见。维护者负责关闭，外部证据须带日期与输入身份。实现妥协见 [TECH_DEBT](TECH_DEBT.md)。

| 编号 | 风险与现有证据 | 阻断边界 | 关闭条件 |
| --- | --- | --- | --- |
| RISK-001 / P1 | 设备 umi / Mi 10 / kona，维护者报告 CSOT、panel ID 0B。既有 simpledrm/USB L3 保留。11-11 同次 ADB 已交叉核实当前 release=16/SDK=36、LOS 23.2-20261108，同时系统/vendor 指纹保留 Xiaomi Android 13；不混用指纹与当前系统版本，旧日期快照仍作历史，见[本机采集](archive/hardware/2026-11-11-umi-native-dsi-first-l3.md)。DTBO 已锁定；其余底层固件、UFS、SKU/物理板号仍待核 | 声称整套固件/物理变体及 ABL handoff 完全确认，或原生面板/触控可用 | 以本机匹配输入核实其余底层固件、保留区与面板；当前 OS 字段已澄清，不据旧快照推断刷机时点；按 L3 分项关闭 |
| RISK-002 / P1 | 首个 Umi/CSOT boot L2 已在 Ubuntu hosted CI、WSL2 和 Apple Silicon macOS/Colima-Rosetta 构建，镜像哈希一致；runner digest、apt 传递包哈希和逐文件许可审计仍开放 | 可复现的完整 MiSeed 构建闭包、设备可启动或发布声明 | 闭合工具与依赖哈希/许可，PR 同 HEAD L2 CI 通过并独立重建一致；设备可启动声明另需维护者 RAM 测试 L3 |
| RISK-003 / P1（恢复限制） | 维护者报告曾以官方工具成功线刷且身份信息保持；官方包脚本静态审查未见身份/NV 目标，允许 `misc` 随官方流程更新。身份/NV 仍严格禁访；重锁 BL 绝对禁止。MiSeed 专用回退演练尚无 | 宣称 MiSeed 镜像已验证可回退；任何 BL 锁定操作 | 复用维护者既有官方恢复经验作回退依据，不要求重复官方流程；MiSeed 特有失败回退仅在后续获授权 L3 评估，流程始终维持 BL 解锁、排除 lock 脚本并保护身份/NV |
| RISK-004 / P0（供电/发布） | 有线 PD/PPS 各约 10 分钟稳定，FG/SMB5、故障停机/恢复、固定 PD/禁止保持及手动旁路 L3 已过。`2bdb266` watchdog 正常刷新 205 秒、读回 `0x43` 无超时，state 8 停泵/固定 PDO/SMB5 恢复已验。11-12 同源镜像 `71619df1…aebbc6` 覆盖 6→38%：固定 PD ≤2 A、PPS 爬升及温升后 18 W 目标；state 8 请求降至 2050 mA 后持续 PPS，无故障退档，见[记录](archive/hardware/2026-11-11-umi-wired-pd-pps-recovery-l3.md)。固定 PD cooling 仅在 SDP 验证；完整温标、传感器异常、watchdog 到期、源端物理诱因与旧 BQ 故障开放。 | 不混同稳定运行与保护验收；不绕保护；watchdog 正常刷新及 PPS 自然温升降额已验，不外推故障注入 | 81%/自动满电旁路、固定 PD 温控、硬件保护/温标 L3；旧故障解释、独立复核与 HEAD CI |
| RISK-005 / P1 | 自有原创内容已按 ADR-0005 采用 AGPL-3.0-only；第三方逐文件许可、GPL-2.0-only 隔离、安装信息及适用网络源码义务仍需审查 | 第三方代码/固件导入与公开分发 | 保留来源/NOTICE，完成文件级兼容审查、对应源代码、安装信息和网络交互源码提供方案 |
| RISK-006 / P2（维护者延期） | 保护 API 于 2026-11-05 返回 403 `Resource not accessible by integration`，当前仓库未启用分支保护。维护者决定等项目跑通并将仓库公开后再配置；届时以实际仓库套餐/规则能力复核 | 在配置前，不声称 PR 必需、检查必需或禁止绕过已生效；不阻断当前本地 L2 开发 | 项目跑通且仓库公开后，在 GitHub UI 配置并验证 Require PR、六项 CI、独立审查与更新后重审、禁止 Force Push/删除和未经授权绕过；记录实际规则与同 HEAD PR 结果 |
| RISK-007 / P1 | EDL Programmer 的来源、签名、授权和兼容性未知 | 声称 9008 可刷或可恢复 | 外部前提可核实并有独立授权的设备验证，否则继续研究 |
| RISK-009 / P2（历史设备标识） | PR1 的 4 处和 PR3 的 2 处设备序列号已在当前树脱敏，但 `8f67d5f` 及已合并历史仍有副本，见[核实记录](archive/reviews/2026-11-06-pr1-coderabbit-verification.md) | 不宣称托管历史已脱敏；不得擅自改写已推送历史 | 维护者在公开分发前复核历史与导出副本并明确处理策略；历史清理须另行授权 |

| RISK-010 / P1（运行时 DTBO/显示交接） | DTBO 重放、隔离、live soc/aliases/phandle/RAM/UBWC L3；旧镜像原生点屏 L3 见[记录](archive/hardware/2026-11-11-umi-native-dsi-first-l3.md)。HEAD `2bdb266` RAM/USB/FDT 采集成功，但 DSI PLL lock failure、clk warning、MDSS probe 栈及独立 SMMU deferred timeout；未见旧 SID fault/DMA/FIFO，未作亮度/目视，root compatible/model 差异仍使严格审计 FAIL；见[新尝试](archive/research/2026-11-11-native-dsi-chip-boundaries.md)。MDSS reset L2/schema/反例见[续修](archive/research/2026-11-11-native-dsi-chip-boundaries.md) | 新镜像未复现旧 SID fault，但 PLL/MDSS 失败，不能宣称 reset/SMMU 交接闭合；ABL 根差异仍开放，TD-006 不豁免 | 修复 PLL/MDSS 与 SMMU 依赖后，以精确新镜像验证点屏、fault 时间线、Android 回退；WP3/WP4 交接及独立复核。TLMM 归 TD-002 |
| RISK-011 / P1（SN100T Linux NCI / eSE 支持） | SN100T 本地标准 NCI transport 已过 L2，见[NFC 适配](archive/research/2026-11-10-sn100t-linux-nci.md)；新驱动的 reset/init、驻留固件、RF 与共享 VEN/eSE 恢复尚无 L3。旧 NQ probe/node 与 Android 基线仅作历史线索 | 不宣称 Linux NFC/NCI、Debian 读卡、HCE 或 eSE 可用；不冒用其他芯片 compatible | 按 [NFC](NFC.md) 固定协议/固件与许可、验证标准 NCI 注册和用户空间 discovery/NDEF，HCE/eSE 分项证明或明确不支持；旧 ABI 取舍归 TD-003 |

关闭条目按 [GOVERNANCE](GOVERNANCE.md) 移入带日期归档，不以“已登记”解除阻断。
