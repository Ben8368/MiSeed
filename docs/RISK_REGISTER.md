# 风险与开放假设

优先级：P0 阻断安全/发布；P1 阻断相关能力声明；P2 可延期但须可见。维护者负责关闭，外部证据须带日期与输入身份。实现妥协见 [TECH_DEBT](TECH_DEBT.md)。

| 编号 | 风险与现有证据 | 阻断边界 | 关闭条件 |
| --- | --- | --- | --- |
| RISK-001 / P1 | 设备为 `umi` / Mi 10 / `kona`，维护者报告 CSOT、cmdline panel ID 0B。MiSeed `f7d50a25…d84689` 已进入诊断 initramfs 并 L3 输出屏幕与 CDC-ACM。Android 系统身份有冲突：2026-11-06 回到 Android 后读到 Xiaomi global V14.0.5.0.TJBMIXM / Android 13 / kernel 4.19.325；较早 partial ADB snapshot 记为 LineageOS 23.2 / release 16。当前 UFS/DTBO、SKU、板号仍未锁定 | 声称当前 ROM/固件、板级变体或 ABL/DTBO handoff 完全确认；原生面板/触控可用 | 明确不同 Android 快照的时间与来源；只用本机匹配输入核实 ABL/DTBO、保留区和面板；按 L3 分项关闭能力 |
| RISK-002 / P1 | 首个 Umi/CSOT boot L2 已在 Ubuntu hosted CI、WSL2 和 Apple Silicon macOS/Colima-Rosetta 构建，镜像哈希一致；runner digest、apt 传递包哈希和逐文件许可审计仍开放 | 可复现的完整 MiSeed 构建闭包、设备可启动或发布声明 | 闭合工具与依赖哈希/许可，PR 同 HEAD L2 CI 通过并独立重建一致；设备可启动声明另需维护者 RAM 测试 L3 |
| RISK-003 / P1（恢复限制） | 维护者报告曾以官方工具成功线刷且身份信息保持；官方包脚本静态审查未见身份/NV 目标，允许 `misc` 随官方流程更新。身份/NV 仍严格禁访；重锁 BL 绝对禁止。MiSeed 专用回退演练尚无 | 宣称 MiSeed 镜像已验证可回退；任何 BL 锁定操作 | 复用维护者既有官方恢复经验作回退依据，不要求重复官方流程；MiSeed 特有失败回退仅在后续获授权 L3 评估，流程始终维持 BL 解锁、排除 lock 脚本并保护身份/NV |
| RISK-004 / P0（供电/发布） | 新镜像 `44854093…e98a89f94c`：维护者宣告 PD 与 PPS 均已稳定（分别约 10 分钟真机、充电中无故障）。PPS：RUN 约 729 秒、2400 条样本；拔线窗口 `-34` 与 VBUS off 吻合。固定 PD：仅固定 PDO、协商 5 V/3 A，SOC 70%→83%、净充约 2.3–2.4 A；无 APDO 时 `error=-95` 为预期退出。主机串口导出仍有丢行；较早镜像掉功率/`-ENOTCONN`/`fault=1` 根因仍开放。8 秒 PPS 刷新与扩容日志已过本地全量回归与 L2。见[本轮记录](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。 | 不把已宣告稳定的 PD/PPS 运行与未完成的保护专项或发布验收混同；不把拔线故障窗口归为充电中复位；不绕过任何保护；不把丢行导出当作完整协议证据 | 修复或验证串口导出行完整性；保护/停机路径与温度标度分项 L3；逐项复现/解释旧 BQ 锁存故障与 `-ENOTCONN`；测试结束完整导出 RAM、按时段读取 TCPM；独立专项复核与精确 HEAD CI |
| RISK-005 / P1 | 自有原创内容已按 ADR-0005 采用 AGPL-3.0-only；第三方逐文件许可、GPL-2.0-only 隔离、安装信息及适用网络源码义务仍需审查 | 第三方代码/固件导入与公开分发 | 保留来源/NOTICE，完成文件级兼容审查、对应源代码、安装信息和网络交互源码提供方案 |
| RISK-006 / P2（维护者延期） | 保护 API 于 2026-11-05 返回 403 `Resource not accessible by integration`，当前仓库未启用分支保护。维护者决定等项目跑通并将仓库公开后再配置；届时以实际仓库套餐/规则能力复核 | 在配置前，不声称 PR 必需、检查必需或禁止绕过已生效；不阻断当前本地 L2 开发 | 项目跑通且仓库公开后，在 GitHub UI 配置并验证 Require PR、六项 CI、独立审查与更新后重审、禁止 Force Push/删除和未经授权绕过；记录实际规则与同 HEAD PR 结果 |
| RISK-007 / P1 | EDL Programmer 的来源、签名、授权和兼容性未知 | 声称 9008 可刷或可恢复 | 外部前提可核实并有独立授权的设备验证，否则继续研究 |
| RISK-009 / P2（历史设备标识） | PR1 的 4 处和 PR3 的 2 处设备序列号已在当前树脱敏，但 `8f67d5f` 及已合并历史仍有副本，见[核实记录](archive/reviews/2026-11-06-pr1-coderabbit-verification.md) | 不宣称托管历史已脱敏；不得擅自改写已推送历史 | 维护者在公开分发前复核历史与导出副本并明确处理策略；历史清理须另行授权 |

| RISK-010 / P1（运行时 DTBO/PMIC 完整性） | 2026-11-07 诊断镜像已完成本机两轮直接 Fastboot 往返，但串口显示 ABL/DTBO 加入的下游命名 PMIC 节点与主线节点重复，SPMI 注册有 `-EEXIST`；PM8150/PON、USB 已绑定。证据与已关闭 RISK-008 见[本轮记录](archive/hardware/2026-11-07-direct-fastboot.md) | 不宣称 DTBO/全部 PMIC 子设备已正确适配；当前结果只覆盖该诊断镜像的 USB/PON 路径 | 已隔离 soc/SPMI/TLMM 具名符号并保留主线 phandle；target-path/数值 phandle/别名不在覆盖内，见[续查](archive/research/2026-11-08-pps-topology-followup.md)；仍须锁定本机 DTBO、完成新镜像启动/重启及受影响外设 L3 后关闭。TLMM 最小恢复另见 TD-002 |
| RISK-011 / P1（Android NFC 用户空间闭包） | NQ-NCI L2 已构建；Mi 10 RAM boot 实测 QUP/I2C1、SN100T `1-0028`/`nq-nci` 绑定和 `/dev/nq-nci` 节点。诊断 initramfs 没有 Android NXP HAL、eSE middleware/service/framework、配置或许可审查后的固件 | 不宣称 Android NFC 用户体验完整；不以 `/dev/nq-nci` 和内核 probe 替代读卡、HCE、OMAPI/eSE 验收 | 固定 HAL/middleware/firmware 闭包及许可证，移植到 Android 用户空间后完成 NFC discovery、NDEF、ISO-DEP/HCE、eSE/OMAPI 与并发/恢复专项。见 [NFC](NFC.md) |

关闭条目按 [GOVERNANCE](GOVERNANCE.md) 移入带日期归档，不以“已登记”解除阻断。
