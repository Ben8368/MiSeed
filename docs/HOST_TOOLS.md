# 主机构建与设备工具

## macOS 构建 L2 镜像

内核脚本在 Linux 上运行；macOS 使用 Colima/VZ Linux 虚拟机和同架构容器，不在 Darwin 上原生编译。Apple Silicon 默认跑 native arm64 容器；Intel Mac 使用 amd64 容器。需要 Homebrew、Colima、Docker CLI 和 Docker Buildx：

```sh
brew install colima docker docker-buildx
bash tools/build_kernel_l2_macos.sh
case "$(uname -m)" in
  arm64) build_arch=arm64 ;;
  x86_64) build_arch=amd64 ;;
  *) exit 1 ;;
esac
shasum -a 256 ".work/boot-l2-${build_arch}/boot.img"
```

入口脚本创建独立 Colima profile（4 CPU、8 GiB、60 GiB）。工具容器使用固定 digest 的 Ubuntu 24.04；Apple Silicon native arm64，Intel Mac amd64。构建源码及 Kbuild 缓存保存在按容器架构隔离的 Colima volume，复用前对照固定 Git tree 核查源码，核查失败则保留原缓存并改用干净源码和构建目录；产物按架构分别写入忽略目录 `.work/boot-l2-arm64/` 或 `.work/boot-l2-amd64/`，避免切换容器架构时复用不兼容的主机工具。默认 `MISEED_JOBS=4`，可设为正整数调整。

2026-11-06 在同一 Apple Silicon/VZ profile（4 vCPU、8 GiB）用相同锁定源码比较：4 并行 amd64/Rosetta 冷输出构建 312.7 秒；4 并行 native arm64 构建 74.1 秒，约快 4.2 倍。两者生成 2,453,504-byte 镜像，SHA-256 都是 `e00640c502647b8e675442198028db426090563943eeb0bd8a520bdbbe1d620c`。复测命令和边界见[验证记录](archive/reviews/2026-11-06-macos-build-fastboot.md)。

首次构建需要网络拉取锁定的 Linux commit 和 Android v2 头模板。Dockerfile 固定多架构 Ubuntu 24.04 基础镜像 digest 与直接 apt 包版本；arm64/amd64 GCC 包版本按容器架构选择。apt 传递依赖哈希尚未闭合，不能宣称完整可复现。命令只构建主机产物；证据等级见 [CONTEXT](CONTEXT.md)。

## Windows / WSL 离线验证

本机便携 w64devkit 2.11.0（GCC 16.2.0）在 `.work/tools/w64devkit-2.11.0/w64devkit/bin/`，原生 DTC 1.7.0 的四项工具在 `.work/tools/dtc-1.7.0/`；来源、许可和哈希见[复核记录](archive/reviews/2026-11-09-pr3-coderabbit-verification.md)。不改系统或用户 PATH。

```powershell
pwsh -NoProfile -File tools/test_host.ps1
```

入口只临时补齐进程 PATH，检查工具后分别运行 Windows 与 WSL Ubuntu-24.04 回归及 ShellCheck，任一步失败即退出。Windows 不具备的 Linux 伪终端、FIFO、权限、Bash 用例由 WSL 实际执行，不能把原生跳过计为通过。WSL 需 Python 3.11+、Git、gcc/cc、Bash、dtc/libfdt 工具及 ShellCheck；手机 ARM64 L2 仍用锁定交叉工具链。已有 Windows 原生协议/日志测试的来源见[本机记录](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。

DT/schema 专用验证使用 `dtschema==2025.8` / `yamllint==1.38.0` venv（`python3 -m venv <venv>` 后安装 `tools/requirements-dtschema.txt`）。完成 L2 后执行 `<venv>/bin/python tools/validate_kernel_dt.py --source <源码> --build <Kbuild目录> --dtb <最终打包DTB>`；固定范围、依赖与四项 DT 反例均为必需，缺失/跳过/诊断即失败。CI 的 L2 job 同步执行。

make 时钟警告阻断构建，不改写输入时间戳。先查来宾 NTP、宿主同步日志及频率校正；WSL/Hyper-V 处置见[环境复核](archive/reviews/2026-11-10-kernel-validation-environment.md)，Colima/Lima 冲突、残留校频与回退见[本机修复](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。

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
