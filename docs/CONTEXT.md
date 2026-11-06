# 当前状态

确认日期：2026-11-06。LOS 23.2 与 122.984 MiB 内核 RAM 启动通过，尺寸未定；[记录](archive/hardware/2026-11-06-kernel-size-fastboot.md)。`f7d50a25` 的大字横屏/摄像头避让、COM6/help 已 L3。当前诊断版 `33b986f6` 的 PON DT 值为 `0x2`，但 Arbiter/SPMI/PON 未绑定；PDC 对照版丢失 USB 枚举，已撤销。中转路径 Linux→Android→Fastboot→Android 已 L3。详见[记录](archive/reviews/2026-11-06-usb-acm-ramboot.md)。

PR #1 已于 2026-11-06 合并至 `main`（`8d73348b`）。当前工作分支 `codex/fastboot-charging-governance` 基于合并后的主线，目标是验证 Linux 直达 Fastboot，并在硬件参数和保护依据完整后推进电源/充电；未完成前不得声称已支持或主动充电。身份/NV 禁访，禁止重锁 BL，不刷写。自动审查意见仅作参考，按 [AI_RULES](AI_RULES.md) 实际核查。

## 近期优先级

1. 定位 PDC/IRQ 与 SPMI Arbiter，验证 Linux 直达 Fastboot；任何 BCB 写入须另行授权并有回退方案。
2. 依据匹配来源实现基础电源管理/充电；保护审查和授权 L3 未通过前不控制充电。
3. 持续锁定设备变体、固件与工具依赖；以设备实测和来源审计支撑后续能力声明。

## 最近验证

硬件尺寸及 RAM 启动：[记录](archive/hardware/2026-11-06-kernel-size-fastboot.md)；MiSeed USB、Framebuffer 与电源安全风险：[记录](archive/reviews/2026-11-06-usb-acm-ramboot.md)；构建 L2 证据和输入锁：[manifests/sources.lock.json](../manifests/sources.lock.json)。
PR1 自动审查核实与修复已通过 L0/L1/L2，USB 新行为待 L3：[核实记录](archive/reviews/2026-11-06-pr1-coderabbit-verification.md)。
