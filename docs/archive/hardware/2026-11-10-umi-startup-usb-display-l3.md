# 2026-11-10 Umi 启动、USB 与显示真机验证

## 环境与载荷

| 项 | 结果 |
| --- | --- |
| 日期 / 主机 | 2026-11-10；Windows 10.0.26200 x64 / PowerShell 7.6.9 |
| 授权范围 | 按维护者请求执行 RAM `fastboot boot`、USB 诊断与 Fastboot 返回；不刷写分区、不改充电参数 |
| 设备 | 单台 Fastboot 设备，`product=umi`、`unlocked=yes`、`is-userspace=no`；序列号不留档 |
| 仓库 / 输入 | `codex/linux-standard-interfaces`，MiSeed HEAD `b53705e17e1b9db15a66f0e972c6a3978cb5c1de`；Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` |
| 工具 | Google Android Platform-Tools 37.0.1；官方 Windows ZIP SHA-256 `45f4d63113e895ebde0c90f194099a4676b6ac653bd28d54314a9e022bbc1a99` |
| 载荷 | `.work/linux-standard-l2/boot.img`，9,617,408 bytes，SHA-256 `9f4713dbcb3b502d3f1db8a06d9ee61b6f6154935be8bfff2162baed69e32014`；仅 RAM 启动 |

## 结果

- 启动前 Fastboot 只读查询确认设备身份和解锁状态；`fastboot boot <boot.img>` 退出码 0，Sending 与 Booting 均 `OKAY`。没有执行 `flash`。
- USB 复合设备 `VID_1D6B&PID_0104` 及 CDC 串口 `COM6` 枚举成功。115200 串口 `version` 返回 Linux 6.18.60 / MiSeed 构建标识；只读 `status` 显示 PON、DWC3 glue、DWC3 与 `a600000.usb` UDC 已绑定。该证据证明内核到达救援用户空间、USB CDC 控制台可交互。
- 串口 `bootloader` 请求后 Fastboot 重新枚举，`product=umi`、`unlocked=yes`；随后 `fastboot reboot` 返回 0。ADB 重新枚举，`sys.boot_completed=1`，恢复原 Android 内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。恢复往返通过。
- 本构建 `CONFIG_DRM_SIMPLEDRM=y`、`CONFIG_DRM_FBDEV_EMULATION=y`。维护者在第二次 RAM 启动时目视确认屏幕正常显示日志、方向正确；simpledrm 继承显示 L3 通过。
- 显示确认后再次串口请求 bootloader，Fastboot 识别 `umi`；`fastboot reboot` 后 ADB 报告 `sys.boot_completed=1`，恢复原 Android 内核。最终设备已离开 MiSeed RAM 系统。

## 范围边界

L3 仅适用于该 Umi 设备与以上精确载荷。未验证 UEFI 产品路径、原生 DSI/面板驱动、触控或其他变体；未刷写、分区、执行充电控制或运行 NFC RF 测试。忽略目录 `.work/linux-standard-l2/` 保留本机脱敏会话输出及 fastboot 状态文件。
