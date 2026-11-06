# 设备与运行契约

## 本机身份

设备字段按证据来源分类；用户自述和候选项目记录都不等于本机观测。来源及回退调查详见[WP1 输入调查](archive/research/2026-11-05-wp1-input-review.md)。

| 项 | 当前值 / 证据 | 待核实 |
| --- | --- | --- |
| 设备身份 | DEVICE_OBSERVED：`device=umi`、`model=Mi 10`、`board=kona`；USER_REPORTED：国行 / SM8250 | SKU、板号待核 |
| 内存 | USER_REPORTED：12GB | 本机容量、保留区、ABL 占用 |
| 存储 | USER_REPORTED：256GB；UPSTREAM_VERIFIED：UFS；DEVICE_OBSERVED：122 个 by-name 链接，无 A/B 后缀 | 本机 GPT/LUN、分区与槽位未知；本地包 `boot` 128MiB，[研究](archive/research/2026-11-06-stock-boot-layout.md)；名称不证明可写 |
| 显示 | USER_REPORTED：CSOT；UPSTREAM_VERIFIED：misaka-link 在 umi/CSOT 配置上报告实机启动；其 DTS 为继承 ABL simple-framebuffer，panel 驱动关闭 | 本机 panel ID、DSC 型号/参数；上游仅支持 headless 显示路径，不证明本机原生 DRM panel 可用 |
| 触控 | UNVERIFIED；候选树同时含 ST FTS 与 Goodix | 实际 Touch IC、固件及驱动 |
| Bootloader | DEVICE_OBSERVED：Fastboot `umi`、`unlocked=yes`、`secure=yes`、下载上限 805306368；版本空 | 槽位、ABL 版本、内存图未知；下载上限非启动上限 |
| ABL / boot header | DEVICE_OBSERVED：官方 LOS 23.2 v2 boot 与仅替换 kernel 的同组件镜像均经 `fastboot boot` 启动；128 MiB boot 中 122.984 MiB kernel 启动完成 | ABL `Load Error` 未定位；最大尺寸未知 |
| DTB / DTBO | UPSTREAM_VERIFIED（上游 CSOT 配置）：`dtc -@` 生成 `__symbols__`，匹配 ABL `/abl_ovf` overlay 锚点并保留原厂 DTBO；其配置报告 152 anchors | 本机 DTB/DTBO 版本与匹配仍未知；方案可据上游锁定，非本机直接读取 |
| Android | DEVICE_OBSERVED：release=`16`、Lineage=`23.2-20261101-NIGHTLY-umi`、fingerprint 含 `umi_global`/`TJBMIXM` | Lineage fingerprint 不作为实物区域证据；release 与 fingerprint 字段差异待解释 |
| 底层固件 | USER_REPORTED：国行；刷 Lineage 前官方 1.0.5；最新 Fastboot 包 1.0.3 | 当前设备固件未从分区读取 |
| Recovery | USER_REPORTED：无需个人数据备份；官方 Recovery 1.0.7 / Fastboot 1.0.3 在 `.work/` | `misc` 随官方线刷允许更新；Recovery 入口未验证；身份/NV 禁访，BL 重锁严禁 |
UPSTREAM_VERIFIED 非本机直测；LOCAL_PACKAGE_OBSERVED 是未独立鉴真的 `.work/` 数据。ADB 采集 PARTIAL（退出码 1），输出在忽略的 `local/`。AOSP 的 `orange` 映射为 UNLOCKED（[Boot flow](https://source.android.com/docs/security/features/verifiedboot/boot-flow)），不证明 Fastboot 能力。停止条件见调查记录。

“硬件变体”指 `umi` 下 SKU、主板修订及面板/触控物料组合。当前只观察到通用 model `Mi 10`；`umi_global` fingerprint 是软件标识，不能核实销售 SKU。CSOT 自述不能推出 panel ID 或 Touch IC。

## 只读采集入口

使用官方 [Android SDK Platform Tools](https://developer.android.com/tools/adb)，在现有 Android 中启用并授权 USB 调试，再选择明确设备：

```text
adb devices
python -X utf8 tools/collect_device.py --serial <设备序列号> --output local/device-identity.json
```

[采集器](../tools/collect_device.py)只执行固定的 `get-state`、逐项 `getprop` 和 `ls -l /dev/block/by-name`，不提权、不重启、不读分区内容。型号不是 umi 或设备不可用时停止，不自动选择其他设备。
输出只允许保存到忽略的 local/，拒绝覆盖已有文件；退出码 0＝字段采集完整，1＝部分字段缺失/失败且已保存，2＝用法/设备/保存失败。权限不足只记录失败，不尝试 root。
不主动查询序列号/IMEI，不保存 adb 原始错误；设备返回值仍是未核实的本地资料，公开前需人工脱敏。采集成功不证明面板/触控、固件哈希、分区容量、临时启动能力或回退可用，也不授权写入。

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
| Linux RAM bring-up / UEFI 基础执行 | L3：LOS 23.2 内核与 122.984 MiB 探针启动至 Android；MiSeed UEFI 未测 | ADB 在线、启动完成；[硬件记录](archive/hardware/2026-11-06-kernel-size-fastboot.md) |
| UEFI → Linux | 设计中 | 无 L2/L3 |
| CSOT 显示 / 实际触控 / USB 诊断 | 计划 | 无 L2/L3 |
| Debian 用户空间 | 计划 | 无 L2/L3 |
| GPU / Wi-Fi / UFS 持久化 | 设计中 | 无 L2/L3 |
| 电池 / 充电 / 温控 | 设计中 | 无 L2/L3 |
