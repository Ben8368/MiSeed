# 设备与运行契约

## 本机身份

设备字段按证据来源分类；详见[WP1 输入调查](archive/research/2026-11-05-wp1-input-review.md)。

| 项 | 当前值 / 证据 | 待核实 |
| --- | --- | --- |
| 设备身份 | DEVICE_OBSERVED：`umi` / Mi 10 / `kona`；USER_REPORTED：国行、SM8250 | SKU、板号待核 |
| 内存 | USER_REPORTED：12GB | 本机容量、保留区、ABL 占用 |
| 存储 | USER_REPORTED：256GB；UPSTREAM_VERIFIED：UFS；观测到 122 个 by-name 链接，无 A/B 后缀 | GPT/LUN 与分区未知；本地包 `boot` 128MiB，[研究](archive/research/2026-11-06-stock-boot-layout.md)；名称不证明可写 |
| 显示 | USER_REPORTED：CSOT；DEVICE_OBSERVED：Android cmdline 报告 OLED panel ID 0B；MiSeed VA48/PA48 rotate:3 镜像 RAM boot 后，维护者确认 16×32 大字横向日志及倒转摄像头避让可用 | fbcon 软件旋转只覆盖 Linux 控制台，不代表 native DRM/DSI panel 驱动或 Android UI 已支持 |
| 触控 | UNVERIFIED；候选树同时含 ST FTS 与 Goodix | 实际 Touch IC、固件及驱动 |
| Bootloader | Fastboot：`umi`、已解锁、安全启动开启、下载上限 805306368；版本空 | 槽位、ABL 版本、内存图未知；下载上限非启动上限 |
| ABL / boot header | DEVICE_OBSERVED：官方 LOS 23.2 v2 boot 与仅替换 kernel 的同组件镜像均经 `fastboot boot` 启动；128 MiB boot 中 122.984 MiB kernel 启动完成 | ABL `Load Error` 未定位；最大尺寸未知 |
| DTB / DTBO | L2 通过 152 个 ABL 锚点、241 个参考目标符号检查 | 本机 DTBO 未锁定；MIUI 包仅为符号参考；启动交接未验证 |
| Android | 2026-11-06 返回后见 Xiaomi global Android 13 / `V14.0.5.0.TJBMIXM`、kernel `4.19.325...`；较早采集称 LOS 23.2 | 快照冲突，ROM/分区状态待确认；fingerprint 不证明硬件区域 |
| 底层固件 | USER_REPORTED：国行；刷 Lineage 前官方 1.0.5；最新 Fastboot 包 1.0.3 | 当前设备固件未从分区读取 |
| Recovery | USER_REPORTED：无需个人数据备份；官方 Recovery 1.0.7 / Fastboot 1.0.3 在 `.work/` | `misc` 随官方线刷允许更新；Recovery 入口未验证；身份/NV 禁访，BL 重锁严禁 |
UPSTREAM_VERIFIED 非本机直测；LOCAL_PACKAGE_OBSERVED 是未鉴真的 `.work/` 数据。ADB 采集 PARTIAL（退出码 1），输出在忽略的 `local/`。

## 只读采集入口

使用官方 [Android SDK Platform Tools](https://developer.android.com/tools/adb)，在现有 Android 中启用并授权 USB 调试，再选择明确设备：

```text
adb devices
python -X utf8 tools/collect_device.py --serial <设备序列号> --output local/device-identity.json
```

[采集器](../tools/collect_device.py)只执行固定的 `get-state`、逐项 `getprop` 和 `ls -l /dev/block/by-name`，不提权、不重启、不读分区内容。型号不是 umi 或设备不可用时停止，不自动选择其他设备。
输出只允许保存到忽略的 local/，拒绝覆盖已有文件；退出码 0＝字段采集完整，1＝部分字段缺失/失败且已保存，2＝用法/设备/保存失败。权限不足只记录失败，不尝试 root。
不采集序列号/IMEI 或原始错误；公开前人工脱敏。采集成功不证明面板/触控、分区容量、临时启动能力或回退可用，也不授权写入。

## 待锁定的运行契约

| 项 | 必须取得的设备特定输入 |
| --- | --- |
| Linux / UEFI | 完整源码锁、最终配置和变体选择；先以 misaka CSOT 配置为参考，不整仓导入 |
| ABL 载荷 | 上游 header v2、kernel/initrd/DTB 与 DTBO 约束；MiSeed 产物仍须对照官方包静态核对 |
| 内存交接 | 采用上游 CSOT 配置起步；本机保留区未知，不能声明可运行 |
| 存储 / 恢复 | 用户报告官方 MiFlash 已成功恢复并保留身份信息；本项目不得触碰身份/NV，且所有恢复保持 BL 解锁 |
| 供电 / 温控 | 电池、采样、温度与保护配置的匹配来源 |

锁定前不生成或声明本机可用包。RAM 加载能力待核实，RAM 载荷本身也须审查写盘行为；其他设备的地址、DTB/DTBO、分区号或触控组合不能直接填入本机契约。

## 硬件能力

只记录 MiSeed 自身证据；通过条件见实施计划，记录字段见 [真机模板](templates/hardware-evidence.md)。

| 功能 | 状态 | 证据 |
| --- | --- | --- |
| Linux RAM bring-up / UEFI | LOS 122.984 MiB、既有 rotate:3 显示/ACM 为 L3；2026-11-07 PDC 开启、TLMM 禁用的诊断镜像连续两轮直接返回 Bootloader Fastboot，`ce196d57` 的 PON 已 L3；PR2 集成版 `3711704c` 仅 L2；UEFI 未测 | [直接 Fastboot](archive/hardware/2026-11-07-direct-fastboot.md) / [尺寸记录](archive/hardware/2026-11-06-kernel-size-fastboot.md) |
| UEFI → Linux | 设计中 | 无 L2/L3 |
| CSOT 显示 / 触控 / USB | 既有 rotate:3、COM6/help 为 L3；`ce196d57` 的 macOS ACM status/bootloader 及 Linux→Fastboot 为 L3；PR2 集成版待 L3；TLMM 禁用，触控未实现 | [本轮记录](archive/hardware/2026-11-07-direct-fastboot.md) / [既有显示](archive/reviews/2026-11-06-usb-acm-ramboot.md) |
| Debian 用户空间 | 计划 | 无 L2/L3 |
| GPU / Wi-Fi / UFS 持久化 | 设计中 | 无 L2/L3 |
| 充电日志 / 管理 | 本机断线记录、重连导出 L3；主动充电/温控未实现 | [记录](archive/hardware/2026-11-08-charging-log.md)；RISK-004 |
