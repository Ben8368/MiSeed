# 设备与运行契约

## 本机身份

来源分类见[WP1 输入调查](archive/research/2026-11-05-wp1-input-review.md)。

| 项 | 当前值 / 证据 | 待核实 |
| --- | --- | --- |
| 设备身份 | DEVICE_OBSERVED：`umi` / Mi 10 / `kona`；hwversion `2.9.1`、board-id `<0x2b 0>`；USER_REPORTED：国行 | SKU、物理板号待核 |
| 内存 | USER_REPORTED：12GB | 本机容量、保留区、ABL 占用 |
| 存储 | USER_REPORTED：256GB；UPSTREAM_VERIFIED：UFS；观测到 122 个 by-name 链接，无 A/B 后缀 | GPT/LUN 与分区未知；本地包 `boot` 128MiB，[研究](archive/research/2026-11-06-stock-boot-layout.md)；名称不证明可写 |
| 显示 | USER_REPORTED：CSOT；DEVICE_OBSERVED：Android cmdline 报告 OLED panel ID 0B；MiSeed VA48/PA48 rotate:3 镜像 RAM boot 后，维护者确认 16×32 大字横向日志及倒转摄像头避让可用 | fbcon 旋转只覆盖控制台，DRM/DSI 未实现 |
| 触控 | UNVERIFIED；候选树同时含 ST FTS 与 Goodix | 实际 Touch IC、固件及驱动 |
| Bootloader | Fastboot：`umi`、已解锁、安全启动开启、下载上限 805306368；版本空 | 槽位、ABL 版本、内存图未知；下载上限非启动上限 |
| ABL / boot header | DEVICE_OBSERVED：官方 LOS 23.2 v2 boot 与仅替换 kernel 的同组件镜像均经 `fastboot boot` 启动；128 MiB boot 中 122.984 MiB kernel 启动完成 | ABL `Load Error` 未定位；最大尺寸未知 |
| DTB / DTBO | 152 个锚点、241 个符号；隔离已 L2，见[续查](archive/research/2026-11-08-pps-topology-followup.md) | 本机 DTBO / 新交接未验证 |
| Android | Android 16 / LOS 23.2-20261101、`4.19.325-cip131-st15-perf-g71b13e62f057`；root 只读后恢复普通 ADB | 固件未从分区核验 |
| 底层固件 | USER_REPORTED：国行；刷 Lineage 前官方 1.0.5；最新 Fastboot 包 1.0.3 | 当前设备固件未从分区读取 |
| Recovery | USER_REPORTED：无需个人数据备份；官方 Recovery 1.0.7 / Fastboot 1.0.3 在 `.work/` | `misc` 随官方线刷允许更新；Recovery 入口未验证；身份/NV 禁访，BL 重锁严禁 |
UPSTREAM_VERIFIED 非本机直测；本地包未鉴真。[root 核查](archive/hardware/2026-11-08-android-root-power-audit.md) PARTIAL（1），原始采集在忽略的 `local/`。

## 只读采集入口

用官方 [ADB](https://developer.android.com/tools/adb)，授权 USB 调试后指定设备：

```text
adb devices
python -X utf8 tools/collect_device.py --serial <设备序列号> --output local/device-identity.json
```

[采集器](../tools/collect_device.py)默认仅 `get-state`、`getprop`、`ls -l /dev/block/by-name`，不提权、重启或读分区。非 umi / 不可用即停止，不换设备。
仅存忽略的 local/，不覆盖；退出码 0＝完整、1＝部分、2＝失败；默认不提权。
`--power` 读取板级/内核、泵 ID 日志与缓存 PDO；不存原始 dmesg、不协商，见[续查](archive/research/2026-11-08-pps-topology-followup.md)。
不采集序列号/IMEI；公开前脱敏。采集不是硬件验收或写入授权。

## 待锁定的运行契约

| 项 | 必须取得的设备特定输入 |
| --- | --- |
| Linux / UEFI | 完整源码锁、最终配置和变体选择；先以 misaka CSOT 配置为参考，不整仓导入 |
| ABL 载荷 | 上游 header v2、kernel/initrd/DTB 与 DTBO 约束；MiSeed 产物仍须对照官方包静态核对 |
| 内存交接 | 采用上游 CSOT 配置起步；本机保留区未知，不能声明可运行 |
| 存储 / 恢复 | 用户报告官方 MiFlash 已成功恢复并保留身份信息；本项目不得触碰身份/NV，且所有恢复保持 BL 解锁 |
| 供电 / 温控 | 泵 ID 0x10、sense 选择 2 mΩ（仅配置）；GPIO8/68；实板热敏/OCP、完整保护待核；绿联45W屏显（用户报告，档位未核） |

未锁定项不得声明可用；RAM 载荷须审查写盘行为，其他设备参数不可直接套用。

## 硬件能力

仅记 MiSeed 证据，字段见[真机模板](templates/hardware-evidence.md)。

| 功能 | 状态 | 证据 |
| --- | --- | --- |
| Linux RAM bring-up / UEFI | `3e7083a7…383489f` RAM 启动、Linux→原生 Fastboot→Android 已 L3；PDC/SPMI/PON 绑定正常；UEFI 未测 | [本轮](archive/hardware/2026-11-08-upstream-power-ramboot.md) / [既有往返](archive/hardware/2026-11-07-direct-fastboot.md) |
| UEFI → Linux | 设计中 | 无 L2/L3 |
| CSOT 显示 / 触控 / USB | 本轮诊断 Image 的 macOS ACM/version/status/日志已 L3；既有 rotate:3 显示为 L3，本轮未复测显示；旧镜像 TLMM 禁用，新 UART-only 配置待 L3，触控未实现 | [本轮](archive/hardware/2026-11-08-upstream-power-ramboot.md) / [既有显示](archive/reviews/2026-11-06-usb-acm-ramboot.md) |
| Debian 用户空间 | 计划 | 无 L2/L3 |
| GPU / Wi-Fi / UFS 持久化 | 设计中 | 无 L2/L3 |
| 充电日志 / 管理 | 自动 SMB5/PD 已 L3；sink-only PPS 测试节点已启用。四轮 RAM 测试取得 APDO、约 8.8 V / 3 A 合约；第三版 switching=1 后退出，第四版因 VBAT 差 54 mV 未启泵；最新启动采样等待修复完整 L2/L3 未完成，不代表持续快充可用 | [本轮](archive/hardware/2026-11-08-umi-pps-ams-retry.md) / [原 L3](archive/hardware/2026-11-08-umi-runtime-power.md)；RISK-004 |
