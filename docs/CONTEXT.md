# 当前状态

确认日期：2026-11-07。本机 Fastboot→RAM Linux→Bootloader Fastboot 已连续两轮 L3 通过，镜像 `ce196d57…734a726`；PDC/SPMI/PON 均绑定，MiSeed 串口 `bootloader` 不经 Android 中转。诊断配置启用 PDC 并暂时禁用 TLMM（TD-002）；仍有 DTBO 重复 PMIC 警告（RISK-010）。[本轮证据](archive/hardware/2026-11-07-direct-fastboot.md)。

PR #1 已合并；本次按授权整合到 PR #2 分支 `codex/fastboot-charging-governance`，保留既有 USB 重连和源码缓存校验修复。整合版 `3711704c…21c304ec` 已 L2，46 项 Linux 离线回归通过，L3 未复测；设备最终停在 Fastboot。此前 PR #1 的充电延期决定保留，充电未实现且不得主动控制。身份/NV 禁访、禁止重锁 BL、不刷写。此前屏幕/USB及 Android 往返见[记录](archive/reviews/2026-11-06-usb-acm-ramboot.md)。

## 近期优先级

1. 完成 PR #2 整合版验证及独立专项复核；消除 DTBO 重复 PMIC、锁定安全 GPIO 范围后再恢复 TLMM。任何后续 BCB 写入仍须当轮授权与回退方案。
2. 依据匹配来源实现电源/充电，过保护审查与 L3；未完成前不控制充电。
3. 持续锁定设备变体、固件与工具依赖；以设备实测和来源审计支撑后续能力声明。

## 最近验证

直接 Fastboot / L2与L3：[本轮记录](archive/hardware/2026-11-07-direct-fastboot.md)；硬件尺寸及 RAM 启动：[记录](archive/hardware/2026-11-06-kernel-size-fastboot.md)；构建输入锁：[manifests/sources.lock.json](../manifests/sources.lock.json)。

PR1 自动审查核实与 USB 重连/构建缓存证据：[记录](archive/reviews/2026-11-06-pr1-coderabbit-verification.md)。
