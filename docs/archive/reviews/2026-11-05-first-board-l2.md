# MiSeed 首个 Umi/CSOT Boot L2 复核

日期：2026-11-05。宿主 Windows 10.0.26200 x64 / PowerShell 7.6.5；WSL2 Ubuntu 24.04 x86_64，GCC 13.3.0、GNU make 4.3、device-tree-compiler 1.7.0。仓库基线：远端 `main` / `4780d22`，工作分支：`codex/board-l2-closure`。本轮未访问手机、未刷写、未提交或推送。

## 交付与输入

- 固定 Linux stable v6.18.60 commit `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，arm64 `defconfig` 加 MiSeed 配置片段，生成 `Image.gz` 与 `qcom/umi-csot.dtb`。
- [板级 DTS](../../../kernel/umi-csot.dts) 保留 maintainer 报告的 CSOT 变体、上游报告的 board ID 与 simple-framebuffer 参数。增加 ABL architectural timer label、参考 DTB 中 152 个 overlay symbol（141 个空锚点，11 个由 Linux DTS 已有标签提供），并将上游报告的 splash 缓冲区标记为 `no-map`。
- 内存节点使用上游参考的零长度范围，预期 ABL 在交给 Linux 前补齐可用内存图；本机 ABL 是否如此尚未确认。这是维护者 RAM 测试前的关键开放项，不能据此声明已启动。
- 新增 AGPL-3.0-only 原创静态 PID 1 救援程序，仅挂载 `devtmpfs`、`proc`、`sysfs`，提供版本、内存、cmdline、mounts 只读查看；没有 shell、网络、自动块设备挂载、fsck、swap 或存储写入。
- Android v2 boot header 模板来源与 SHA-256 见 [sources lock](../../../manifests/sources.lock.json)。该 release boot 镜像只作为经过哈希校验的 header/DTB 符号元数据输入；最终 kernel、ramdisk、DTB、cmdline 均替换，不带入上游性能参数或 payload。DTB 抽取和 ABL anchor 生成逻辑由 [工具](../../../tools/extract_boot_v2_dtb.py) 从模板提取。
- 配置、构建、initramfs 生成、Android v2 打包分别见 [config](../../../kernel/umi-l2.config)、[build script](../../../tools/build_kernel_l2.sh)、[initramfs builder](../../../tools/make_initramfs.py)、[packer](../../../tools/pack_android_boot_v2.py)。DTS 来源/许可为 BSD-3-Clause；Linux/ABL-derived symbols 保留 GPL-2.0-only；原创代码按 AGPL-3.0-only。许可证文本归档在 `LICENSES/`。

## 构建产物

构建退出码：0。命令：

```text
MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_OUTPUT_ROOT=/tmp/miseed-boot-l2/artifacts bash tools/build_kernel_l2.sh
```

主机端产物在 WSL `/tmp/miseed-boot-l2/artifacts/boot.img`，维护者可从 Windows 工作区 `.work/boot-l2/boot.img` 取得待 RAM 测试文件。`.work` 被忽略，不进入 Git。

| 输入/产物 | SHA-256 |
| --- | --- |
| Linux v6.18.60 commit | `f45ed76d6791cf72ff1265ee93511175bbbc2f18` |
| Android v2 元数据模板 | `a7e4d341e35a663683bf41a8fe99d472bc6b8568aa1b54b05901dc7e9f2d842a` |
| `kernel/umi-csot.dts` | `3f9f2f6f4e065b413ab564a479586974e39ce70595f05ffa4b2b4d1d91ad9f1e` |
| `kernel/umi-l2.config` | `e0179269cebe333571fb3eb506a1dda94e56980cdd73164f9b01c37e89814ff8` |
| generated ABL anchor DTSI | `9f196b3a82932dbb581c623a41e670f6dfed00bb1bfeff60d3228732f7cb9a23` |
| resolved `.config` | `4fbf7e199be9af3edb8e178a73b1e0988ebff89e2eaebf1cea7780482609e8fe` |
| arm64 `Image.gz` | `05fd0e5204a9227ef05f5ed53f3ad844e439212a73ceafbbd408ffc8d3fc4c16` |
| rescue initramfs | `a607bfac326c2f7791b145320ba4cabec0eb4d3088095489b3b3159bd263395d` |
| `qcom/umi-csot.dtb` | `48580c7e16ecea2ec976a8830c4fad2cfbfbac2a615e2c16fc69847dc24a7734` |
| `boot.img` (15,876,096 bytes) | `5a05d0ddb3669e8e1218a302f9bf7dd98b91998bbbf7e455d31bcdcf4bf89486` |

镜像为 Android boot header v2、4096-byte page；kernel 15,450,456 bytes @ `0x8000`，ramdisk 252,977 bytes @ `0x01000000`，DTB 163,320 bytes @ `0x01f00000`。三段地址区间通过不重叠检查。cmdline 固定为 `console=tty0 loglevel=7 rdinit=/init`。

## 验证结果

| 检查 | 退出码 | 结果 |
| --- | ---: | --- |
| `bash tools/build_kernel_l2.sh`（锁定源码和工具链） | 0 | L2：内核、带 symbols 的 DTB、静态救援 initramfs 与 Android v2 `boot.img` 构建完成。 |
| ABL overlay 符号复核 | 0 | 参考 DTB 要求的 152 个符号全部存在于 MiSeed DTB；141 个占位节点，另 11 个命中 Linux DTS 标签。 |
| boot image read-back + 独立 segment 检查 | 0 | header/version/page/address、cmdline、kernel/ramdisk/DTB 段与输入逐字节匹配。 |
| `file` / `gzip -t` | 0 | PID 1 为 AArch64 静态 ELF；initramfs gzip 流完整。 |
| newc initramfs 目录回读 | 0 | 仅含 `/init`、`/dev`、`/proc`、`/sys` 与 console/null 节点。 |
| `python tools/check_repo.py` | 0 | L0 通过。 |
| `git diff --check` | 0 | 无空白错误。 |
| ShellCheck error 级 / `bash -n` | 0 | 构建脚本静态检查通过。 |
| actionlint v1.7.12 | 0 | CI workflow 静态检查通过。 |
| 锁文件 JSON 解析 | 0 | source/toolchain lock 均有效。 |
| GitHub Boot L2 CI | 未运行 | 需 PR 后取得同 HEAD CI 证据；分支保护仍按维护者决定延期。 |
| 独立第二次干净复建 | 未运行 | apt 传递包 hash 与 hosted runner digest 未闭合。 |

## 未覆盖与风险

- L2 证明固定主机工具链可构建并封装镜像，不证明 ABL 接受、kernel 进入、内存图正确、屏幕可见或设备安全可用。维护者尚未在 MiSeed 上 RAM 启动此产物；启动结果由维护者后续测试记录。
- 实际内存映射、ABL/DTBO 行为、原厂存储交接、panel/touch ID 仍未知。面板/触控驱动不在本产物能力声明内。
- Ubuntu 24.04 直接 apt 版本已钉住；完整依赖包哈希、runner 镜像 digest、独立复建和同 HEAD 远端 CI 仍开放。
- GitHub branch protection API 曾返回 403。本项目分支保护依维护者决定，等项目跑通且仓库公开后再设置并核验；当前延期不是已启用的保护。
- 本轮只生成文件，没有启动设备、刷写分区或改动供电设置。未推送、未创建 PR、未提交。
