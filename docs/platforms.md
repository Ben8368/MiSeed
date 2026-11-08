# 设备与运行契约

## 本机身份

来源分类见[WP1 输入调查](archive/research/2026-11-05-wp1-input-review.md)。

| 项 | 当前值 / 证据 | 待核实 |
| --- | --- | --- |
| 设备身份 | DEVICE_OBSERVED：`umi` / Mi 10 / `kona`；USER_REPORTED：国行、SM8250 | SKU、板号待核 |
| 内存 | USER_REPORTED：12GB | 本机容量、保留区、ABL 占用 |
| 存储 | USER_REPORTED：256GB；UPSTREAM_VERIFIED：UFS；观测到 122 个 by-name 链接，无 A/B 后缀 | GPT/LUN 与分区未知；本地包 `boot` 128MiB，[研究](archive/research/2026-11-06-stock-boot-layout.md)；名称不证明可写 |
| 显示 | USER_REPORTED：CSOT；DEVICE_OBSERVED：Android cmdline 报告 OLED panel ID 0B；MiSeed VA48/PA48 rotate:3 镜像 RAM boot 后，维护者确认 16×32 大字横向日志及倒转摄像头避让可用 | fbcon 旋转只覆盖控制台，DRM/DSI 未实现 |
| 触控 | UNVERIFIED；候选树同时含 ST FTS 与 Goodix | 实际 Touch IC、固件及驱动 |
| Bootloader | Fastboot：`umi`、已解锁、安全启动开启、下载上限 805306368；版本空 | 槽位、ABL 版本、内存图未知；下载上限非启动上限 |
| ABL / boot header | DEVICE_OBSERVED：官方 LOS 23.2 v2 boot 与仅替换 kernel 的同组件镜像均经 `fastboot boot` 启动；128 MiB boot 中 122.984 MiB kernel 启动完成 | ABL `Load Error` 未定位；最大尺寸未知 |
| DTB / DTBO | L2 检查 152 个 ABL 锚点、241 个目标符号；新增总线 overlay 隔离，见[续查](archive/research/2026-11-08-pps-topology-followup.md) | 本机 DTBO 未锁定；MIUI 包仅为符号参考；启动交接未验证 |
| Android | 2026-11-08 实测 Android 16、kernel `4.19.325-cip131-st15-perf-g71b13e62f057`，启动完成、普通 ADB；2026-11-06 曾观测 Android 13 | 历史 ROM 快照变化未追溯；底层固件未重新读取 |
| 底层固件 | USER_REPORTED：国行；刷 Lineage 前官方 1.0.5；最新 Fastboot 包 1.0.3 | 当前设备固件未从分区读取 |
| Recovery | USER_REPORTED：无需个人数据备份；官方 Recovery 1.0.7 / Fastboot 1.0.3 在 `.work/` | `misc` 随官方线刷允许更新；Recovery 入口未验证；身份/NV 禁访，BL 重锁严禁 |
UPSTREAM_VERIFIED 非本机直测；LOCAL_PACKAGE_OBSERVED 是未鉴真的 `.work/` 数据。ADB 采集 PARTIAL（退出码 1），输出在忽略的 `local/`。

## 只读采集入口

用官方 [ADB](https://developer.android.com/tools/adb)，授权 USB 调试后指定设备：

```text
adb devices
python -X utf8 tools/collect_device.py --serial <设备序列号> --output local/device-identity.json
```

[采集器](../tools/collect_device.py)默认只执行固定的 `get-state`、逐项 `getprop` 和 `ls -l /dev/block/by-name`，不提权、不重启、不读分区内容。型号不是 umi 或设备不可用时停止，不自动选择其他设备。
输出仅保存到忽略的 local/，不覆盖；退出码 0＝完整，1＝部分，2＝失败。权限不足不提权。
可选 `--power` 只读补充板级字段、内核、泵 ID 日志与缓存 PDO；不保留原始 dmesg，不主动协商。限制见[续查](archive/research/2026-11-08-pps-topology-followup.md)。
不采集序列号/IMEI；公开前脱敏。采集不是硬件验收或写入授权。

## 待锁定的运行契约

| 项 | 必须取得的设备特定输入 |
| --- | --- |
| Linux / UEFI | 完整源码锁、最终配置和变体选择；先以 misaka CSOT 配置为参考，不整仓导入 |
| ABL 载荷 | 上游 header v2、kernel/initrd/DTB 与 DTBO 约束；MiSeed 产物仍须对照官方包静态核对 |
| 内存交接 | 采用上游 CSOT 配置起步；本机保留区未知，不能声明可运行 |
| 存储 / 恢复 | 用户报告官方 MiFlash 已成功恢复并保留身份信息；本项目不得触碰身份/NV，且所有恢复保持 BL 解锁 |
| 供电 / 温控 | 电池、采样、温度与保护配置的匹配来源 |

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
| 充电日志 / 管理 | `793ca218…7030c5` 的 600 秒准备、约 25.5 秒受限启充、拔出停充、恢复策略、1524 条日志/复读 L3；600 秒到期未测；新增 PPS 路径仅 L1/L2、默认禁用，无快充 L3 | [本轮](archive/reviews/2026-11-08-fastcharge-ci.md) / [原 L3](archive/hardware/2026-11-08-umi-runtime-power.md) / [PPS 移植](archive/research/2026-11-08-lineage-pps-port.md)；RISK-004 |
