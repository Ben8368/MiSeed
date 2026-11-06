# 风险与开放假设

优先级：P0 阻断安全/发布；P1 阻断相关能力声明；P2 可延期但须可见。维护者负责关闭，外部证据须带日期与输入身份。实现妥协见 [TECH_DEBT](TECH_DEBT.md)。

| 编号 | 风险与现有证据 | 阻断边界 | 关闭条件 |
| --- | --- | --- | --- |
| RISK-001 / P1 | 设备为 `umi` / Mi 10 / `kona`，维护者报告 CSOT、cmdline panel ID 0B。MiSeed `f7d50a25…d84689` 已进入诊断 initramfs 并 L3 输出屏幕与 CDC-ACM。Android 系统身份有冲突：2026-11-06 回到 Android 后读到 Xiaomi global V14.0.5.0.TJBMIXM / Android 13 / kernel 4.19.325；较早 partial ADB snapshot 记为 LineageOS 23.2 / release 16。当前 UFS/DTBO、SKU、板号仍未锁定 | 声称当前 ROM/固件、板级变体或 ABL/DTBO handoff 完全确认；原生面板/触控可用 | 明确不同 Android 快照的时间与来源；只用本机匹配输入核实 ABL/DTBO、保留区和面板；按 L3 分项关闭能力 |
| RISK-002 / P1 | 首个 Umi/CSOT boot L2 已在 Ubuntu hosted CI、WSL2 和 Apple Silicon macOS/Colima-Rosetta 构建，镜像哈希一致；runner digest、apt 传递包哈希和逐文件许可审计仍开放 | 可复现的完整 MiSeed 构建闭包、设备可启动或发布声明 | 闭合工具与依赖哈希/许可，PR 同 HEAD L2 CI 通过并独立重建一致；设备可启动声明另需维护者 RAM 测试 L3 |
| RISK-003 / P1（恢复限制） | 维护者报告曾以官方工具成功线刷且身份信息保持；官方包脚本静态审查未见身份/NV 目标，允许 `misc` 随官方流程更新。身份/NV 仍严格禁访；重锁 BL 绝对禁止。MiSeed 专用回退演练尚无 | 宣称 MiSeed 镜像已验证可回退；任何 BL 锁定操作 | 复用维护者既有官方恢复经验作回退依据，不要求重复官方流程；MiSeed 特有失败回退仅在后续获授权 L3 评估，流程始终维持 BL 解锁、排除 lock 脚本并保护身份/NV |
| RISK-004 / P0（充电功能；本次 PR1 按维护者决定延期） | 本机供电/温控匹配仍未闭合。较早 LOS 配置含 PM8150B SMB5/FG GEN4 与 BQ2597X 驱动，但当前 ROM 快照冲突，不能证明参数适用；colorgarden `.work` 电池参考明确承认容量/电压近似且关闭多项保护。当前 Linux v6.18.60 无匹配 SMB5/PM8150B FG 驱动。回到 Android 后 `dumpsys battery` 单次读数为 SOC 100、4368 mV、33.2°C、status 3、health 2、Li-poly；未连接充电源，sysfs power_supply uevent 被拒绝。维护者确认本次 PR1 可不含充电功能 | 不实现主动充电或宣称充电安全；不根据单次读数推断电芯参数；不得复制近似数据或关闭保护 | 后续从已确认当前系统/同板变体获取可信电池、PMIC/充电器、温控和保护链来源；实现兼容驱动并审查限流/过压/过温/故障路径；独立专项复核及授权 L3。未闭合前不控制充电 |
| RISK-005 / P1 | 自有原创内容已按 ADR-0005 采用 AGPL-3.0-only；第三方逐文件许可、GPL-2.0-only 隔离、安装信息及适用网络源码义务仍需审查 | 第三方代码/固件导入与公开分发 | 保留来源/NOTICE，完成文件级兼容审查、对应源代码、安装信息和网络交互源码提供方案 |
| RISK-006 / P2（维护者延期） | 保护 API 于 2026-11-05 返回 403 `Resource not accessible by integration`，当前仓库未启用分支保护。维护者决定等项目跑通并将仓库公开后再配置；届时以实际仓库套餐/规则能力复核 | 在配置前，不声称 PR 必需、检查必需或禁止绕过已生效；不阻断当前本地 L2 开发 | 项目跑通且仓库公开后，在 GitHub UI 配置并验证 Require PR、六项 CI、独立审查与更新后重审、禁止 Force Push/删除和未经授权绕过；记录实际规则与同 HEAD PR 结果 |
| RISK-007 / P1 | EDL Programmer 的来源、签名、授权和兼容性未知 | 声称 9008 可刷或可恢复 | 外部前提可核实并有独立授权的设备验证，否则继续研究 |
| RISK-008 / P1（直接 Linux→Fastboot 路径；本次 PR1 采用 Android 中转） | rotate:3 大字横屏/摄像头避让及 CDC-ACM 已 L3。镜像 `f7d50a25…d84689` 实拍显示 DWC3 peripheral/okay、ACM gadget 绑定、Windows COM6，并经串口收到 MiSeed `help`。当前镜像实测 Linux `bootloader` 请求→Android、ADB→Fastboot、Fastboot reboot→Android。当前 PDC 关闭诊断镜像 `33b986f6…9f76a3d6c` 的串口 `status` 证实 PON DT `mode-bootloader=0x2`，运行时 PMIC 子节点均默认可用且 `reg` 为合法两单元，但 `c440000.spmi` driver 未绑定、SPMI 设备列表为空、`qcom,pm8998-pon` 设备缺失。对照启用 `CONFIG_QCOM_PDC` 的镜像 L2 成功，但两次 RAM 启动均未枚举 USB，主机一次随后回到 Android；为避免回归已从候选配置撤销。维护者允许本次 PR1 先采用 Android 中转，Linux 直达 Fastboot 延后。近似 SM8250 上游用过 `mode-bootloader=<0x2>`，但 Umi ABL 行为仍是假设 | 不宣称 Linux→Fastboot 直达已完成；不依赖未验证 PON magic；不做未授权 misc/BCB 写入 | 后续定位 PDC/IRQ 与 SPMI Arbiter probe 失败；独立复核并以授权 L3 验证 USB 延迟注册/重连及 PON reboot 请求。若依赖一次性 BCB/misc 写入，先取得当轮明确授权并制定失败回退。充电安全另见 RISK-004 |
| RISK-009 / P2（历史设备标识） | PR1 记录的 4 处序列号已在工作区脱敏，但 `8f67d5f` 及已合并历史仍有副本，见[核实记录](archive/reviews/2026-11-06-pr1-coderabbit-verification.md) | 不宣称托管历史已脱敏；不得擅自改写已推送历史 | 维护者在公开分发前复核历史与导出副本并明确处理策略；历史清理须另行授权 |

关闭条目按 [GOVERNANCE](GOVERNANCE.md) 移入带日期归档，不以“已登记”解除阻断。
