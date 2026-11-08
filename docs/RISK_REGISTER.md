# 风险与开放假设

优先级：P0 阻断安全/发布；P1 阻断相关能力声明；P2 可延期但须可见。维护者负责关闭，外部证据须带日期与输入身份。实现妥协见 [TECH_DEBT](TECH_DEBT.md)。

| 编号 | 风险与现有证据 | 阻断边界 | 关闭条件 |
| --- | --- | --- | --- |
| RISK-001 / P1 | 设备为 `umi` / Mi 10 / `kona`，维护者报告 CSOT、cmdline panel ID 0B。MiSeed `f7d50a25…d84689` 已进入诊断 initramfs 并 L3 输出屏幕与 CDC-ACM。Android 系统身份有冲突：2026-11-06 回到 Android 后读到 Xiaomi global V14.0.5.0.TJBMIXM / Android 13 / kernel 4.19.325；较早 partial ADB snapshot 记为 LineageOS 23.2 / release 16。当前 UFS/DTBO、SKU、板号仍未锁定 | 声称当前 ROM/固件、板级变体或 ABL/DTBO handoff 完全确认；原生面板/触控可用 | 明确不同 Android 快照的时间与来源；只用本机匹配输入核实 ABL/DTBO、保留区和面板；按 L3 分项关闭能力 |
| RISK-002 / P1 | 首个 Umi/CSOT boot L2 已在 Ubuntu hosted CI、WSL2 和 Apple Silicon macOS/Colima-Rosetta 构建，镜像哈希一致；runner digest、apt 传递包哈希和逐文件许可审计仍开放 | 可复现的完整 MiSeed 构建闭包、设备可启动或发布声明 | 闭合工具与依赖哈希/许可，PR 同 HEAD L2 CI 通过并独立重建一致；设备可启动声明另需维护者 RAM 测试 L3 |
| RISK-003 / P1（恢复限制） | 维护者报告曾以官方工具成功线刷且身份信息保持；官方包脚本静态审查未见身份/NV 目标，允许 `misc` 随官方流程更新。身份/NV 仍严格禁访；重锁 BL 绝对禁止。MiSeed 专用回退演练尚无 | 宣称 MiSeed 镜像已验证可回退；任何 BL 锁定操作 | 复用维护者既有官方恢复经验作回退依据，不要求重复官方流程；MiSeed 特有失败回退仅在后续获授权 L3 评估，流程始终维持 BL 解锁、排除 lock 脚本并保护身份/NV |
| RISK-004 / P0（供电/发布） | 固定 LOS 自动充电已移植，RPMh/I2C15/BQ 绑定已 L3。四轮 RAM 测试取得 APDO 5–11 V / 5 A、PPS 约 8.8 V / 3 A；第三版 switching=1 后退出，第四版在 SOC 90% 因两路 VBAT 差 54 mV 被门控拦下，已恢复固定 PD/SMB5。当前源码对齐 LOS 电流采样并扩展启动采样等待；97 项 Linux 回归通过，最新完整 L2/L3 未完成，见[本轮记录](archive/hardware/2026-11-08-umi-pps-ams-retry.md)。用户要求停止构建/真机测试，先交草稿 PR。GPIO8、完整保护/温控/恢复及标度仍开放 | 不宣称本版持续 PPS、实测功率或完整充电保护已验收；短暂 APDO 合约、启泵和持续充电分列 | 恢复验证后完成最新 L2，核对设备、SOC 与精确镜像，RAM 验证本版 PPS 持续调节、保护、停止恢复、拔插和 GPIO8；完成独立专项复核及适用 L3，远端 CI 另行核验 |
| RISK-005 / P1 | 自有原创内容已按 ADR-0005 采用 AGPL-3.0-only；第三方逐文件许可、GPL-2.0-only 隔离、安装信息及适用网络源码义务仍需审查 | 第三方代码/固件导入与公开分发 | 保留来源/NOTICE，完成文件级兼容审查、对应源代码、安装信息和网络交互源码提供方案 |
| RISK-006 / P2（维护者延期） | 保护 API 于 2026-11-05 返回 403 `Resource not accessible by integration`，当前仓库未启用分支保护。维护者决定等项目跑通并将仓库公开后再配置；届时以实际仓库套餐/规则能力复核 | 在配置前，不声称 PR 必需、检查必需或禁止绕过已生效；不阻断当前本地 L2 开发 | 项目跑通且仓库公开后，在 GitHub UI 配置并验证 Require PR、六项 CI、独立审查与更新后重审、禁止 Force Push/删除和未经授权绕过；记录实际规则与同 HEAD PR 结果 |
| RISK-007 / P1 | EDL Programmer 的来源、签名、授权和兼容性未知 | 声称 9008 可刷或可恢复 | 外部前提可核实并有独立授权的设备验证，否则继续研究 |
| RISK-009 / P2（历史设备标识） | PR1 记录的 4 处序列号已在工作区脱敏，但 `8f67d5f` 及已合并历史仍有副本，见[核实记录](archive/reviews/2026-11-06-pr1-coderabbit-verification.md) | 不宣称托管历史已脱敏；不得擅自改写已推送历史 | 维护者在公开分发前复核历史与导出副本并明确处理策略；历史清理须另行授权 |

| RISK-010 / P1（运行时 DTBO/PMIC 完整性） | 2026-11-07 诊断镜像已完成本机两轮直接 Fastboot 往返，但串口显示 ABL/DTBO 加入的下游命名 PMIC 节点与主线节点重复，SPMI 注册有 `-EEXIST`；PM8150/PON、USB 已绑定。证据与已关闭 RISK-008 见[本轮记录](archive/hardware/2026-11-07-direct-fastboot.md) | 不宣称 DTBO/全部 PMIC 子设备已正确适配；当前结果只覆盖该诊断镜像的 USB/PON 路径 | 已实现 soc/SPMI/TLMM overlay 入口隔离并保留主线 phandle，见[续查](archive/research/2026-11-08-pps-topology-followup.md)；仍须锁定本机 DTBO、完成新镜像启动/重启及受影响外设 L3 后关闭。TLMM 最小恢复另见 TD-002 |

关闭条目按 [GOVERNANCE](GOVERNANCE.md) 移入带日期归档，不以“已登记”解除阻断。
