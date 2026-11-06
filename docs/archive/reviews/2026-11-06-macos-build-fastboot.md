# 2026-11-06 macOS L2 构建与 Fastboot RAM 启动

## 主机 L2

宿主为 Apple Silicon Mac，Darwin 25.6.0 arm64 / zsh；Colima 0.10.3、Docker CLI 29.9.0。使用 Colima VZ ARM64 Linux VM，先以 Rosetta 运行 Linux/amd64 构建容器。随后创建独立 profile `miseed-boot-l2`，在 PR #1 工作树冷缓存执行 `bash tools/build_kernel_l2_macos.sh`，退出码 0；当时的默认产物目录为 `.work/boot-l2/`。Ubuntu 24.04 多架构基础镜像固定为 `sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852`，直接 apt 工具版本与 hosted CI 一致。

输出 `.work/boot-l2/boot.img`：2,453,504 bytes，SHA-256 `e00640c502647b8e675442198028db426090563943eeb0bd8a520bdbbe1d620c`。构建检查的 152 个 ABL overlay symbols、Android v2 header segments 均通过；哈希与 PR hosted Ubuntu L2 一致。此为 L2，不表示设备启动成功。Ubuntu amd64 基础镜像 digest 已固定，apt 传递包哈希未闭合。

## Apple Silicon 架构对比

为选择本机利用率更高的方案，在同一台主机、同一 Colima VZ profile（4 vCPU、8 GiB）、相同源码与工具版本下，用独立空输出目录和 `MISEED_JOBS=4` 分别执行容器中的完整 `tools/build_kernel_l2.sh`。amd64/Rosetta 的输出目录为 `.work/perf-amd64-j4`，耗时 312.7 秒；native arm64 的输出目录为 `.work/perf-arm64-j4`，耗时 74.1 秒，约快 4.2 倍。计时不含首次下载和 Docker 镜像构建时间；源码和头模板缓存已存在。两次产物均为 2,453,504 bytes，SHA-256 都是 `e00640c502647b8e675442198028db426090563943eeb0bd8a520bdbbe1d620c`。本机复核正式 wrapper 时发现不同架构不能共享内核输出目录（其中含架构相关主机工具），因此入口现将产物分别写入 `.work/boot-l2-amd64/` 和 `.work/boot-l2-arm64/`。

arm64 镜像用与正式 Dockerfile 相同的 Ubuntu digest 和锁定直接包版本；Ubuntu arm64 的 GCC 交叉编译器包版本没有 amd64 包名中的 `cross1` 后缀。正式 Dockerfile 已按 `TARGETARCH` 选择该版本。对比只证明这台 Apple Silicon 主机上的此构建任务更快，不外推到 Intel Mac、其他内核配置或并行度，也不代表真机启动验证。

## RAM 启动尝试

Platform-Tools 37.0.2；Fastboot 只读查询返回 `product=umi`、`unlocked=yes`，未记录设备序列号。以已构建镜像执行 `fastboot boot <boot.img>`，输出传输和 `Booting OKAY` 成功；没有 `flash`、`erase`、格式化、锁定或分区写入。

维护者选择的结果为“黑屏、报错或重启”，未提供具体屏幕/错误原文；随后确认已主动断开 USB。断开前主机未见 ADB；断开后 Fastboot/ADB 均无设备（符合 USB 已断开的状态）。因此目前无法区分内核未启动、显示未初始化、设备重启或短暂断连，不能记为 L3 通过或失败。下次测试先重新进入 Fastboot 并保持连接，核对镜像哈希后单次 RAM 启动，记录具体屏幕和错误文本，再只读查询 Fastboot/ADB；状态不明时停止，不连续重试。

## 后续复现入口

macOS 构建入口为 `bash tools/build_kernel_l2_macos.sh`，Apple Silicon 产物为 `.work/boot-l2-arm64/boot.img`，Intel Mac 产物为 `.work/boot-l2-amd64/boot.img`；完成后核对 SHA-256。Apple Silicon 默认使用 Colima/VZ Linux/arm64，Intel Mac 使用 linux/amd64；默认 `MISEED_JOBS=4`，可用环境变量调整。两种架构的 Linux 构建脚本均已成功生成相同镜像；Dockerfile、macOS wrapper 与并行构建已验证。首次构建仍需网络下载固定源码/模板，apt 传递依赖哈希仍开放。
