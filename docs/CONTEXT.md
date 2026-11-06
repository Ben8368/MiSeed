# 当前状态

确认日期：2026-11-06。官方 LOS 23.2 boot、同组件重建 kernel，以及 118.984/122.984 MiB 尺寸探针均 RAM 启动至 Android；源码 commit `71b13e6`、官方配置、Clang 21。此前 LOS 24/Clang 12 与手工 DTB/救援 ramdisk 组合黑屏，不能据此判定编译失败。当前可启动尺寸至少 122.984 MiB，最大值未定；[验证记录](archive/hardware/2026-11-06-kernel-size-fastboot.md)。首个 MiSeed boot L2 CI / macOS 复建已通过；apt 传递包哈希、runner digest 未闭合，详见[构建记录](archive/reviews/2026-11-06-macos-build-fastboot.md)。
分支 `codex/board-l2-closure` 的 PR #1 面向 `main` 且保持 open；此前 HEAD `be05ff3f` 的 [CI run 8](https://github.com/Ben8368/MiSeed/actions/runs/42151528210) 六项全通过，本轮更新待新 CI。设备/许可调查见 [WP1](archive/research/2026-11-05-wp1-input-review.md)；身份/NV 禁访、BL 重锁禁止。

## 近期优先级

1. 用 LOS 23.2 官方源码、配置、Clang 21 继续递增尺寸，固定 boot 其它组件；已到 122.984 MiB，最大值未定。补齐 apt 包哈希和 runner digest。
2. 维持 L0/L1/L2/L3 证据分级，不将 host 产物或上游报告记作 MiSeed 实机启动。
3. 分支保护延期；维护者负责 RAM 测试。设备写入、身份/NV 访问和 BL 重锁仍禁止。

## 最近验证

[原厂 boot 分区研究](archive/research/2026-11-06-stock-boot-layout.md)；[大内核 Fastboot 验证](archive/hardware/2026-11-06-kernel-size-fastboot.md)；[macOS 构建与 RAM 启动记录](archive/reviews/2026-11-06-macos-build-fastboot.md)；[首个 Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。
