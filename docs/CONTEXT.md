# 当前状态

确认日期：2026-11-06。LOS 23.2 与 122.984 MiB 内核 RAM 启动通过，尺寸未定；[记录](archive/hardware/2026-11-06-kernel-size-fastboot.md)。`f7d50a25` 的大字横屏/摄像头避让、COM6/help 已 L3。当前诊断版 `33b986f6` 的 PON DT 值为 `0x2`，但 Arbiter/SPMI/PON 未绑定；PDC 对照版丢失 USB 枚举，已撤销。中转路径 Linux→Android→Fastboot→Android 已 L3。详见[记录](archive/reviews/2026-11-06-usb-acm-ramboot.md)。

分支 `codex/board-l2-closure`，PR #1 open。维护者于 2026-11-06 明确本次 PR1 可暂不包含充电；当前验收采用 Linux→Android→Fastboot→Android 中转路径，Linux 直达 Fastboot 和充电留后续。不得声称这两项已完成，也不得主动充电。身份/NV 禁访，禁止重锁 BL，不刷写。

## 近期优先级

1. 后续定位 PDC/IRQ 与 SPMI Arbiter，验证 Linux 直达 Fastboot；任何 BCB 写入须另行授权并有回退方案。
2. 后续依据匹配来源实现电源/充电，过保护审查与 L3；未完成前不控制充电。
3. 持续锁定设备变体、固件与工具依赖；以设备实测和来源审计支撑后续能力声明。

## 最近验证

硬件尺寸及 RAM 启动：[记录](archive/hardware/2026-11-06-kernel-size-fastboot.md)；MiSeed USB、Framebuffer 与电源安全风险：[记录](archive/reviews/2026-11-06-usb-acm-ramboot.md)；构建 L2 证据和输入锁：[manifests/sources.lock.json](../manifests/sources.lock.json)。
