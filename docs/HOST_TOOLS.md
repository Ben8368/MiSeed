# 主机构建与设备工具

## macOS 构建 L2 镜像

内核脚本在 Linux 上运行；macOS 使用 Colima/VZ Linux 虚拟机与 Rosetta amd64 容器复用同一脚本，不在 Darwin 上原生编译。Apple Silicon 已构建出与 PR hosted L2 相同的镜像哈希。需要 Homebrew、Colima、Docker CLI 和 Docker Buildx：

```sh
brew install colima docker docker-buildx
bash tools/build_kernel_l2_macos.sh
shasum -a 256 .work/boot-l2/boot.img
```

入口脚本创建独立 Colima profile（4 CPU、8 GiB、60 GiB），Apple Silicon 启用 VZ/Rosetta；Linux 工具容器使用 Ubuntu 24.04 amd64 和仓库中的 apt 版本锁。构建缓存保存在 Colima volume，镜像保存在忽略目录 `.work/`。`MISEED_JOBS=1` 默认限制并行度；可设置为正整数调整。Intel Mac 使用 amd64 虚拟机路径。

首次构建需要网络拉取锁定的 Linux commit 和 Android v2 头模板。Dockerfile 固定 Ubuntu 24.04 amd64 基础镜像 digest 和直接 apt 包版本；apt 传递依赖哈希尚未闭合，不能宣称完整可复现。命令只构建主机产物；证据等级和本次结果见 [CONTEXT](CONTEXT.md) 与[验证记录](archive/reviews/2026-11-06-macos-build-fastboot.md)。

## ADB / Fastboot 工具与连接

安装 Google [SDK Platform-Tools](https://developer.android.com/tools/releases/platform-tools)（含 `adb`、`fastboot`），或用 Android Studio SDK Manager 安装。二进制放在忽略目录 `.work/host-tools/platform-tools/`，不要提交。当前 Mac 使用 37.0.2，官方 ZIP SHA-256：`de86d62321ad38049d488fb1854c7b9a29f7e1b17e8dfb597c304b038cc37ae7`。

```sh
export PATH="$PWD/.work/host-tools/platform-tools:$PATH"
adb version
fastboot --version
adb devices
adb reboot bootloader
fastboot devices
```

正常 Android 模式下需先开启 USB 调试并在手机上授权；Bootloader Fastboot 模式可直接运行 `fastboot devices`。序列号是设备标识，公开日志先脱敏。只看到设备仅能证明 USB/Fastboot 通信可用。

## RAM 启动与持久刷写

对已核对产品、解锁状态、镜像 SHA-256 且获得本轮明确授权的设备，可用 `fastboot boot <boot.img>` 做一次性 RAM 启动；该命令不刷写分区。`Sending ... OKAY` / `Booting OKAY` 只说明载荷已发送并被 Bootloader 接受，不证明 Linux 已启动。随后记录屏幕/错误原文，并只读检查 `fastboot devices`、`adb devices`；状态不明或 USB 断开时停止，不连续重试。

本仓库没有经审查的持久刷写清单。禁止据此运行通用 `flash`、`flashall`、`erase`、`format` 或任何 lock 命令；身份/NV 分区始终禁止访问。任何分区写入均需当轮明确授权、目标与哈希复核、逐分区清单及可行回退方案；不得重锁 Bootloader。L2 构建、RAM 启动和持久刷写是不同证据与授权阶段。
