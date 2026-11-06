# 2026-11-06 macOS L2 构建与 Fastboot RAM 启动

## 主机 L2

宿主为 Apple Silicon Mac，Darwin 25.6.0 arm64 / zsh；Colima 0.10.3、Docker CLI 29.9.0。使用 Colima VZ ARM64 Linux VM，启用 Rosetta 运行 Linux/amd64 构建容器。随后创建独立 profile `miseed-boot-l2`，在 PR #1 工作树冷缓存执行 `bash tools/build_kernel_l2_macos.sh`，退出码 0；Ubuntu 24.04 amd64 基础镜像固定为 `sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852`，直接 apt 工具版本与 hosted CI 一致。

输出 `.work/boot-l2/boot.img`：2,453,504 bytes，SHA-256 `e00640c502647b8e675442198028db426090563943eeb0bd8a520bdbbe1d620c`。构建检查的 152 个 ABL overlay symbols、Android v2 header segments 均通过；哈希与 PR hosted Ubuntu L2 一致。此为 L2，不表示设备启动成功。Ubuntu amd64 基础镜像 digest 已固定，apt 传递包哈希未闭合。

## RAM 启动尝试

Platform-Tools 37.0.2；Fastboot 只读查询返回 `product=umi`、`unlocked=yes`，未记录设备序列号。以已构建镜像执行 `fastboot boot <boot.img>`，输出传输和 `Booting OKAY` 成功；没有 `flash`、`erase`、格式化、锁定或分区写入。

维护者选择的结果为“黑屏、报错或重启”，未提供具体屏幕/错误原文；随后确认已主动断开 USB。断开前主机未见 ADB；断开后 Fastboot/ADB 均无设备（符合 USB 已断开的状态）。因此目前无法区分内核未启动、显示未初始化、设备重启或短暂断连，不能记为 L3 通过或失败。下次测试先重新进入 Fastboot 并保持连接，核对镜像哈希后单次 RAM 启动，记录具体屏幕和错误文本，再只读查询 Fastboot/ADB；状态不明时停止，不连续重试。

## 后续复现入口

macOS 构建入口为 `bash tools/build_kernel_l2_macos.sh`，冷缓存产物为 `.work/boot-l2/boot.img`；完成后核对 SHA-256。入口默认单线程，并在 Colima volume 缓存源码。Dockerfile、macOS wrapper 与 `MISEED_JOBS` 已由冷缓存完整构建和增量复建验证通过；构建仍需网络下载固定源码/模板。apt 传递依赖哈希仍开放。
