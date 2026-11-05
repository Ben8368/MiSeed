# 风险与开放假设

优先级：P0 阻断安全/发布；P1 阻断相关能力声明；P2 可延期但须可见。维护者负责关闭，外部证据须带日期与输入身份。实现妥协见 [TECH_DEBT](TECH_DEBT.md)。

| 编号 | 风险与现有证据 | 阻断边界 | 关闭条件 |
| --- | --- | --- | --- |
| RISK-001 / P1 | 采集观察到 `umi`/Mi 10、`kona`、Android/Lineage 元数据、`orange` 和 by-name 名称；采集仍为 PARTIAL。维护者报告 CSOT；misaka-link pinned CSOT 配置报告 umi ABL boot-header-v2 实机启动，已用于 MiSeed 设备契约。内存/本机 ABL 版本、具体面板/触控、固件/槽位语义未知 | 声称本机变体完全识别、原生面板/触控可用或 MiSeed 已真机启动 | 对齐上游已测 CSOT headless 契约；本机面板/触控只在声称相应能力前核实；MiSeed 设备启动证据按阶段取得 |
| RISK-002 / P1 | 已固定 Linux LTS/EDK II/板级参考提交及工具候选；主机端复现 misaka CSOT DTB/boot repack，另用 Ubuntu GCC 13.3 构建 Linux v6.18.60 通用 arm64 defconfig。MiSeed 板级配置/UEFI、候选 Debian GCC 14.2、补丁适配、逐文件许可和完整构建闭包仍未验证 | 可复现的 MiSeed 构建与镜像发布 | 完成依赖/许可/哈希闭包；将 CSOT DTB/配置接入锁定 Linux 输入并完成 L2，固定环境可复现 |
| RISK-003 / P1（恢复限制） | 维护者报告曾以官方工具成功线刷且身份信息保持；官方包脚本静态审查未见身份/NV 目标，允许 `misc` 随官方流程更新。身份/NV 仍严格禁访；重锁 BL 绝对禁止。MiSeed 专用回退演练尚无 | 宣称 MiSeed 镜像已验证可回退；任何 BL 锁定操作 | 复用维护者既有官方恢复经验作回退依据，不要求重复官方流程；MiSeed 特有失败回退仅在后续获授权 L3 评估，流程始终维持 BL 解锁、排除 lock 脚本并保护身份/NV |
| RISK-004 / P0 | 充电/温控保护与板级参数未核实 | 主动危险充电、超频、日用安全声明 | 参数来源与实际变体匹配、保护/错误路径审查及专项 L3 |
| RISK-005 / P1 | 自有原创内容已按 ADR-0005 采用 AGPL-3.0-only；第三方逐文件许可、GPL-2.0-only 隔离、安装信息及适用网络源码义务仍需审查 | 第三方代码/固件导入与公开分发 | 保留来源/NOTICE，完成文件级兼容审查、对应源代码、安装信息和网络交互源码提供方案 |
| RISK-006 / P1 | HEAD `69caa92` 的 push 与手动运行 L0/L1 成功；branch protection/rulesets API 返回 403，门禁状态 UNVERIFIED | 声称 PR 必需、检查必需或禁止绕过已生效 | 维护者在 GitHub UI 核验/配置 Require PR、相关 CI、禁止 Force Push 与未经授权绕过；记录同 HEAD 规则与 PR |
| RISK-007 / P1 | EDL Programmer 的来源、签名、授权和兼容性未知 | 声称 9008 可刷或可恢复 | 外部前提可核实并有独立授权的设备验证，否则继续研究 |

关闭条目按 [GOVERNANCE](GOVERNANCE.md) 移入带日期归档，不以“已登记”解除阻断。
