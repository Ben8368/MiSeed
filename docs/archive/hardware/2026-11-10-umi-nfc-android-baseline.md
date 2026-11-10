# Umi NFC Android 只读基线与移植缺口

日期：2026-11-10。执行：Codex；用户本轮授权 ADB root，也允许切换 Fastboot 临时启动镜像。

## 范围与输入

| 项 | 本次值 / 边界 |
| --- | --- |
| 宿主 | Windows 10.0.26200 / X64；PowerShell 7.6.9 / X64；Python 3；ADB 来自忽略的 `.work/platform-tools-20261105/platform-tools/` |
| 仓库 | `codex/nfc-wireless-charging`，`42f54a2d7a338a6e329ec432c5d23c3d085effd2`；采集前工作区干净 |
| 设备 | 唯一在线 ADB 设备；`ro.product.device=umi`，不记录序列号；面板、触控及其他硬件变体本轮未复测 |
| 原系统 | Android 16，LOS `23.2-20261108-NIGHTLY-umi`，内核 `4.19.325-cip131-st15-perf-g71b13e62f057` |
| 权限 / 设备改动 | `adb root` 后仅只读采集；未停止 NFC/eSE 服务，未打开 `/dev/nq-nci`，未发 ioctl、NCI 或 APDU；未修改 NFC 开关 |
| 启动 / 存储 / 供电 | 未切换 Fastboot、未启动镜像；未刷写、读写原始分区、修改 GPIO、充电驱动或供电配置；仅在设备上计算所列系统文件哈希，未提取二进制或固件 |
| 前置 / 回退 | 核验唯一设备与 `umi` 后才采集；结束执行 `adb unroot` 并复核 shell UID、Android 启动状态和 NFC 开关；没有临时镜像需要回退 |
| 本地证据 | `local/nfc-android-20261110/`（Git 忽略）：`runtime.json`、`additional.json`、`final-state.json`；每项保存命令、退出码和筛选输出 |

## 运行时核对

- `cmd nfc status` 返回 `Nfc is enabled`；`mState=on`、`mAlwaysOnState=off`、屏幕 `OFF_LOCKED`。这只证明服务状态，不证明正在发射 RF、成功读卡或 HCE/eSE 可用。`Firmware version=<Unknown>`，不凭库文件名推断实际固件版本。
- `/dev/nq-nci` 为字符设备，权限 `0660 nfc:nfc`，SELinux 标签 `nfc_device`；对应 `/vendor/etc/ueventd.rc` 条目。
- Android 设备为 `0-0028`，绑定 `nq-nci`，OF 路径 `soc/i2c@984000/nq@28`。运行时 DT 大端 GPIO cell 确认 IRQ=111、VEN=83、FIRM=110、CLKREQ=7，I²C 地址 `0x28`，与 MiSeed DTS 相符。Linux adapter 编号是运行时结果：既有 MiSeed L3 为 `1-0028`，不能照抄 Android 的 adapter 0。
- NFC 进程 `android.hardware.nfc-service.nxp` 与 `com.android.nfc` 在运行；注册 AIDL `android.hardware.nfc.INfc/default` 和 `vendor.nxp.nxpnfc_aidl.INxpNfc/default`。
- VINTF 文件包含来源路径注释 `hardware/nxp/nfc/snxxx/aidl/1.0/nfc-service-nxp.xml`。它是源码定位线索，不是已固定的源码 commit 或许可证据。
- HAL ELF 直接依赖 `nfc_nci_nxp_snxxx.so`、Binder NDK、NFC/NXP AIDL 接口库和 Bionic 等；实现库另依赖 HIDL/NXP eSE 接口。NFC 进程映射中实际包含 `/vendor/lib64/libsn100u_fw.so`。
- `/vendor/etc/libnfc-nxp.conf` 指定 `NXP_NFC_DEV_NODE="/dev/nq-nci"`、`NXP_FW_NAME="libsn100u_fw.so"`、`NXP_FW_TYPE=0x01`。`/vendor/etc/libnfc-nci.conf` 的 `NFA_STORAGE="/data/nfc"`、poll mask `0x4F`；system 对应配置 poll mask `0x0F`。本轮未确认配置优先级，不能混合或默认二者可互换；未访问 `/data/nfc`。
- eSE 是另一条依赖链：`vendor.qti.secure_element@1.2-service`、`com.android.se`、HIDL secure-element 1.0/1.1/1.2 的 `SIM1`/`eSE1` 声明；实际进程映射含 `se_nq_extn_client.so`、`ls_nq_client.so`、`jcos_nq_client.so`、eSE power manager、QTEE 相关库。注册/加载不证明 OMAPI 或安全交易已通过。
- NXP 配置的 `NXP_NFC_SE_TERMINAL_NUM="eSE2"` 与 HAL 实例名 `eSE1` 分别记录，语义映射待源码核查，不据名称差异判为故障。

## 有线与无线路径分别记录

此次 USB 数据线连接只用于 ADB。采样为 `usb/present=1`、`usb/online=0`；无线路径 `dc/present=0`、`dc/online=0`、`rx1619/present=0`。不由 USB 插线推断正在充电，也不将 DC/RX1619 状态归为 PD/PPS。

没有无线 RX/TX 激励或状态切换，未验证无线与 NFC 共存；也未测试有线充电控制。原有有线 PD/PPS 稳定性结论继续以其专属 L3 记录为准。

## 文件身份（仅设备内计算 SHA-256）

以下哈希固定本次 Android 对照文件，不是允许提取/再分发的许可，也不是 MiSeed 构建输入。

```text
4330979702e354f00e7056163506ac661315d38753afccd222a800d3efaf72bb  /vendor/bin/hw/android.hardware.nfc-service.nxp
7561504a6e92c9f49b4622079c27dc7d277fb180dff9ffc57fe61c4310af432f  /vendor/bin/hw/vendor.qti.secure_element@1.2-service
869fd87ef7c6695d4182d95de51d24afc2f662d2926138df5c688bf0c22bcdd3  /vendor/lib64/libsn100u_fw.so
333d17f256be7e52c175f578f066d2eaba0b14b0b5552e58b398c6422993cef2  /vendor/etc/libnfc-nxp.conf
2b3bcea7d9f9402a72f4a1bb2214c7f1d55e8693fe9dfc1a8c81a8f6a31706c4  /vendor/etc/libnfc-nci.conf
9967fc48a04a6260ffad98db7dcd6e894a821c3abe591b0cb7478b554f704f1d  /system/etc/libnfc-nci.conf
e68c0c79c9c725ac1e0c310e2a141691ad515f5b1d96ab6e860dbfa299f9c762  /vendor/etc/init/nfc-service-nxp.rc
2600ecaa356280a54330e4166d4eadb95250c228ce5f23e95b7a128dc3123558  /vendor/etc/vintf/manifest/nfc-service-nxp.xml
cc35c1d6a2d4e3318b43f06199bcf7f1754120b30aabcbad12d6d6747b670433  /vendor/lib64/nfc_nci_nxp_snxxx.so
```

## 命令与结果

实际顺序：`adb devices` → 指定唯一设备核验 `getprop ro.product.device` → 普通 shell 采集 → `adb -s <device> root` → root 只读采集 → `adb -s <device> unroot` → 最终复核。设备标识在记录中替换为 `<device>`。

只读命令包括 `cmd nfc status`、筛选后的 `dumpsys nfc`、`service list`、`lshal`、`ps -A`、`ls -lZ /dev/nq-nci`、`readlink -f`、对指定 DT 属性的 `od -An -tx1`、对指定 HAL/实现库的 `readelf -d`、配置键 `grep`、HAL 进程映射筛选与上述文件 `sha256sum`。没有使用 `ldd` 执行目标库，也没有复制设备二进制到宿主。

结构化采集 `runtime.json` 的 15 组、`additional.json` 的 3 组命令均退出 0；`root`/`unroot` 均退出 0。此前探查中未匹配文件/权限不足的组合命令有退出 1；一次宿主临时采集脚本语法错误在执行前退出，修正后才生成证据。不得把这些失败记为成功。

最终复核退出 0：`uid=2000(shell)`、`ro.product.device=umi`、`sys.boot_completed=1`、`Nfc is enabled`。设备留在 Android，NFC 开关未改。

## 仓库验证

仅修改文档；`python -X utf8 tools/check_repo.py` 和 `git diff --check` 均退出 0。未改源码，未运行 L1、L2 构建或新的 MiSeed RAM boot L3。活动文档篇幅、链接与来源锁检查通过。

## 对 MiSeed 的结论与未覆盖

这是 Android 对照系统的 L3 只读运行时调查，不是 MiSeed NFC 功能验收。MiSeed 现有驱动/DT 的 L2 与 probe/node L3 仍引用 [2026-11-09 记录](2026-11-09-umi-nfc-l2-ramboot.md)。本轮没有重新构建或执行该镜像。

本机缓存 `.work/boot-l2/boot.img` 的 SHA 为 `d3b6f4113be0c3b7bdfe3f910ae3b51ee35fb8db47ce411f8094a37d4ff047ff`；`boot-nfc-validation.img` 为 `c9e24126d7797bb118b097371586c14a49d66f105135c6bee793c51adf4e1f13`，均不等于上述历史 L3 镜像。未为这些缓存镜像建立本轮输入对应关系，因此没有使用它们重复验证或继承旧结论。

下一阶段需先闭合 HAL/配置/固件来源与运行环境，再分项验证 NCI 传输、NDEF、ISO-DEP/HCE、OMAPI/eSE 和共享 VEN 恢复；有线、无线 RX、无线 TX 协调单独验收。具体步骤只维护在 [NFC](../../NFC.md)，RISK-011 保持开放。

## 同日后续方向更新

以上为 Android 对照采集阶段的历史证据。随后 RQ-14 明确 Linux/Debian 目标，原文的 HAL 用户空间计划不再执行；当前路线以 [NFC](../../NFC.md) 为准。同日后续诊断源码、L1/L2 与接口治理结果另见 [Linux 接口审计](../reviews/2026-11-10-linux-interface-audit.md)，不回写为本次 Android 调查的验证。
