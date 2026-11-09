# Umi NFC / eSE 移植

本记录对应 RQ-13 / WP10。目标是保留 Xiaomi Mi 10 SN100T Android 下游驱动使用的 `/dev/nq-nci` 字符设备与 NXP HAL ioctl ABI，使后续 Android NFC HAL 能沿用已有控制接口。

## 当前内核路径

- 采用 LineageOS `android_kernel_xiaomi_sm8250` revision `6fcac56423a078c7bd180e8d2c626c7d66bf1677` 的 `drivers/nfc/nq-nci.c` 与对应头文件，按 GPL-2.0-only 保留版权与 SPDX。移植差异包括 Linux 6.18 的 I²C probe/remove、`class_create()` API，以及随驱动闭合的下游私有 `linux/nfcinfo.h` UAPI 头。
- 设备运行时 sysfs 链接确认 NFC 设备路径为 `soc/i2c@984000/nq@28`，I²C 地址 `0x28`，驱动名 `nq-nci`。Linux 6.18 SM8250 基线将 `i2c@984000` 映射为 I2C1，使用 GPIO4/5。
- 固定的 Umi/Kona 设备树源中，NFC pinctrl 定义 IRQ GPIO111、VEN GPIO83、FIRM GPIO110、CLKREQ GPIO7。`umi-pinctrl.dtsi` 将 NFC enable 状态中的 GPIO6 覆盖为 GPIO83；本次 L3 RAM boot 已确认 QUP/I2C1 注册、`1-0028` 绑定 `nq-nci` 且 `/dev/nq-nci` 存在。
- SN100T 的 NFC 与 eSE 共用 VEN；本 DTS 不虚构独立 eSE 电源 GPIO。下游驱动保留 `NFC_SET_PWR`、`ESE_SET_PWR`、`ESE_GET_PWR`、`NFCC_GET_INFO` 等 ioctl，以及冷复位、固件下载和读写路径。
- 驱动在 probe 时会拉低/拉高 VEN，并向控制器发送 reset/version 检查命令。VEN 同时供 NFCC 与 eSE 使用，因此 RAM 启动探测会重启这两个器件。
- 相关 GPIO 从原 TLMM 保留范围中按最小集合释放。该变更不修改有线充电或无线充电驱动、配置、供电参数。

## 用户空间边界

MiSeed 目前构建的是诊断 Linux initramfs，不包含 Android NXP NFC HAL、eSE middleware、Android NFC service/framework、标签协议配置、供应商固件或权限策略。内核兼容设备节点可以作为后续 HAL 的接口基础，但不会单独提供读卡、HCE、Android OMAPI 或 eSE 安全服务。没有来源与再分发许可审查前，不从设备提取或提交厂商二进制与固件。

完整 Android 兼容验收还需要固定并审查 HAL/middleware/固件闭包，在相应 Android 用户空间中验证 NFC discovery、NDEF、ISO-DEP/HCE、eSE/OMAPI 和 VEN 协同。当前 RAM boot 验证了 Linux kernel/DT 驱动 probe 与 `/dev/nq-nci`，不代表完整用户空间栈或日用 NFC 能力。

## 验证记录

源码与构建记录在 `manifests/sources.lock.json`；本机运行能力状态在 [platforms](platforms.md)，开放边界见 [RISK-011](RISK_REGISTER.md)。本次 L2 与真机 RAM boot 证据见 [2026-11-09 NFC L3 记录](archive/hardware/2026-11-09-umi-nfc-l2-ramboot.md)。设备只通过 `fastboot boot` 临时启动，没有写入分区；完成后已返回 Android 并确认 ADB `sys.boot_completed=1`。
