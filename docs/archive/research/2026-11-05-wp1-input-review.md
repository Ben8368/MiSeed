# WP1 输入调查与回退草案

日期：2026-11-05。来源快照由 GitHub API 读取默认分支 HEAD；SHA 是可复核的调查快照，不是 MiSeed 构建锁。只查看公开文本、树和少量实际源码；未克隆、构建、运行上游脚本或导入代码。对来源自述的真机结果只作为线索。

## 上游候选矩阵

| 仓库 / 分支与调查 SHA | SPDX / 证据 | 用途、源码观察与决定 |
| --- | --- | --- |
| [reAAAq/uefi-umi](https://github.com/reAAAq/uefi-umi)，main `727f6c26347055ab70eac2104e6a4edc80c8d556` | BSD-3-Clause 文本在 `LICENSE`；README 要求保留目录/文件许可及 SPDX | `Platforms/Xiaomi/umiPkg/umi.dsc`、`Drivers/InheritedFbDxe`、`UmiButtonsDxe`、`UmiRamPayloadDxe`、`BootShim/BootShim.S`。umi 板级、继承 ABL framebuffer、按键与 RAM payload 值得参考；README 明确其配套 XNU 验证，UEFI FTS 仍为实验代码，不能视为 Linux EFI Stub 或独立显示/触控已验证。仅参考。 |
| [LonelyFool/EDK2-SM8250](https://github.com/LonelyFool/EDK2-SM8250)，main `270b09f6c0fdcb9d8a8cd2c67f768b3e577a91cb` | 无仓库级 SPDX / LICENSE；`SM8250/SM8250.dsc` 有 BSD License 头；`Dxe/UFSDxe/UFSDxe.efi` 是无源码预编译二进制 | `SM8250/SM8250.dsc`、`SM8250/SM8250Lib`、`Dxe/UFSDxe`。README 自述移植自 Pixel 3 XL、需 `clk_ignore_unused`，Windows PE 卡标志；非 umi 平台、UFS 二进制来源/许可不清。拒绝导入二进制；源码只作历史参考，逐文件清权后再评估。 |
| [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)，main `6b3d90b79e191e08171bfedd3980234a7e49d340` | API 标识 BSD-2-Clause；上游 README 说明 `GPLDrivers`、`GPLApplications`、`GPLLibrary` 为 GPLv2-or-later，另有 Binaries 子模块 | Project Mu 的 ARM64 设备端口、`Platforms/Xiaomi/umiPkg`、BootShim 与设备配置。是 reAAAq UEFI 路线相关的实际平台树；逐目录核许可证、子模块和供应链后仅参考，不整体导入。 |
| [TianoCore/edk2](https://github.com/tianocore/edk2)，master `561c006ac6bb962d8e53b369b6a6a01d2d9ae579` | `License.txt`: BSD-2-Clause-Patent；第三方目录另审 | 通用 UEFI、BaseTools、AArch64 构建基座；无 SM8250/umi 板级配置。作为后续通用基线候选，需连同精确子模块、edk2-platforms 与逐文件许可再锁。 |
| [microsoft/mu_basecore](https://github.com/microsoft/mu_basecore)，release/202608 `6f805f26a4366fe56790130dc2f8f3e1b4c2fe28` | `License.txt`: BSD-2-Clause-Patent；逐包检查仍需做 | Mu 核心 UEFI 库与模块；不含已核实的 umi 板级。依赖候选，须与 Mu-Silicium 的 manifest/补丁成组锁定。 |
| [microsoft/mu_tiano_platforms](https://github.com/microsoft/mu_tiano_platforms)，main `fd0a4a741a93566508cc75521d161351d77d1f0a` | `License.txt`: BSD-2-Clause-Patent；逐包检查仍需做 | Mu 平台集成接口；并非 Mu-Silicium 产品集成分支本身。仅与被选中的 Mu manifest 一起评估。 |
| [colorgarden/umi-mainline](https://github.com/colorgarden/umi-mainline)，master `5fa2e0048f89b82355d2a9de89c1d74ab9910959` | `LICENSE` 声明混合许可；DTS BSD-3-Clause、内核驱动 GPL-2.0-only、脚本 CC0-1.0、文档 CC-BY-4.0，按 SPDX/路径逐文件 | `dts/sm8250-xiaomi-umi*.dts[i]`、`patches/panel-xiaomi-umi.c`、`docs/DISPLAY.md`、`docs/TOUCH.md`。含 DSI/DSC 面板、FTS/Goodix 变体线索；docs 明说 DTS 同一 I²C 总线上定义两种 IC，单看 DTS 不能识别实机。CSOT 型号 `j2_mp_42_02_0b` 是该来源设备报告，不是本机观测。按文件、提交和配置筛选参考，不整仓 fork。 |
| [Ben8368/umi-mainline](https://github.com/Ben8368/umi-mainline)，master `fcae40369c8fb721de84d4ddda0544b630391049` | 混合许可；LICENSE、`LICENSES/` 和逐文件 SPDX，额外有 MIT-DockDSM、GPL 充电策略文件 | 与 colorgarden 比较为 diverged：43 commits ahead、1 behind；同有 DTS、panel 和 touch 树，另有 thermistor/charge-limit 配置、工具、CI 与治理文件。优先核对用户旧工作线索；不可继承旧验证或直接复制充电/温控配置。参考，不采用整仓。 |
| [misaka-link/umi-linux](https://github.com/misaka-link/umi-linux)，main `cf83073887899f485a5c606f9842c5e691ea796c` | 根 `LICENSE`: GPL-2.0-only；CSOT DTS 文件头 BSD-3-Clause；其余逐文件审查 | 实际检查 `rootfs-build/patch/kernel/archive/sm8250-6.18/dt/sm8250-xiaomi-umi-csot.dts`、`boot-build/`、README 和构建脚本。维护者的 CSOT 用户报告与该树设备变体相符。上游报告 umi/CSOT ABL header-v2 实机启动：kernel/initrd/DTB、`dtc -@` symbols、保留原厂 DTBO、匹配 152 个 `/abl_ovf` overlay anchors，并从 ABL 继承 simple-framebuffer。采用为 headless boot contract 与测试证据，不整体导入；其 panel 驱动关闭、触控不支持，不作为相应能力证据。拒绝超频、主动充电和未审固件。 |
| [torvalds/linux](https://github.com/torvalds/linux)，master `05441d697f7f9a927264f4058cbbc6a03dfcac9d` | 根 `COPYING`: GPL-2.0-only；目录/文件许可证可能更具体 | Linux 主线、arm64 EFI Stub、Qualcomm UFS/USB/DRM/GPU/thermal 驱动。通用驱动可参考；不能因 SoC 相同推定 Umi DTS、CSOT DSI 或触控已上游可用。 |
| [kernel.org/stable/linux](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git)，tag `v6.18.60`，commit `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，tag object `16829bf498b811706e98e480879e3fe1754d797e` | 根 COPYING / LICENSES：GPL-2.0-only；逐文件许可仍适用 | 选作长期维护基线，官方列为 LTS；提交身份已固定。umi DTS/驱动补丁尚未验证能应用，非构建通过证据。 |
| [reAAAq/UniXNU-umi](https://github.com/reAAAq/UniXNU-umi)，main `3eb72578597d2ca16471b172d45eddc096a34a04` | API 未识别单一 SPDX；逐文件/依赖许可待审 | 只供理解 Umi UEFI 配对构建与运行报告；XNU、Apple 组件和设备私有内容不适合作为 MiSeed/Linux 输入。仅来源线索。 |

**恢复机制与许可证观察：** [Linux initramfs `/init` 机制](https://docs.kernel.org/filesystems/ramfs-rootfs-initramfs.html)不需要为机制本身引入独立代码。Recovery 可评估 Debian 13 arm64 最小 rootfs，由 `debootstrap` 从官方仓库生成；必须按日期 snapshot、包清单与每个包的版权/许可记录。BusyBox 可作为日后评估的 GPL-2.0-only 工具例子，当前未选择或纳入来源清单；若分发，须满足对应源代码和许可文本要求。Rust 核心及 Cargo 依赖须逐项锁定并保存许可证清单。Recovery 暂不引入第三方二进制或闭源固件。

### 功能比较结论

- 启动链：TianoCore/Mu 提供通用框架；reAAAq BootShim 是针对其配套系统的参考。misaka-link 的 umi/CSOT headless ABL header-v2 路径有上游实机报告，作为 MiSeed 首个 Linux boot contract；MiSeed 对其 exact input 做了主机端复现，仍没有自有设备 L3。
- SM8250/UFS：抽查 Linux `drivers/ufs/host/ufs-qcom.c`、`drivers/usb/dwc3/`、`drivers/gpu/drm/msm/`、`drivers/thermal/qcom/`、`drivers/net/wireless/ath/ath11k/` 与 `drivers/firmware/efi/libstub/`，可作为通用 SoC/EFI 实现参考；该 SHA 未找到 `arch/arm64/boot/dts/qcom/sm8250-xiaomi-umi.dts`，Umi 仍需板级 DTS/补丁。LonelyFool 的 SM8250 EDK2 带预编译 UFSDxe，来源不明。reAAAq 的 SMMU/UFS/内存观察属于其设备报告；不能视为 Linux UFS 驱动或分区映射证据。
- 显示/触控：ColorGarden/Ben 树包含多面板/触控变体 DTS 和驱动；CSOT 型号/触控 IC 需本机 `getprop`、已授权 Android 只读接口或维护者资料确认。单独面板供应商不能推导触控 IC。
- GPU/USB/供电：SM8250 通用驱动与下游配置线索不等于完整 GPU、USB host/gadget、温控及电池保护可用。来源中有超频和充电控制方案，均排除为直接移植项；保护/板级参数需本机匹配与专项审查。
- DTB/DTBO：候选仓库明确存在不同 CSOT/Samsung 与配置组合；未发现可据此锁定本机 ABL、DTB、DTBO 匹配关系的设备证据。

除另有标注的主机端复现外，以上为截至日期的源码/文档检查，不是 MiSeed 设备验证。候选摘要仍由 `manifests/sources.json` 管理；`revision: null` 保持不变。misaka-link 精确 SHA 另以“tested reference only”纳入 `manifests/sources.lock.json`，不作为整仓构建输入。

## 工具链候选

| 工具 | 建议调查基线 / 来源 | 固定与校验 | 状态 |
| --- | --- | --- | --- |
| GCC / AArch64 GNU | Debian 13 `gcc-aarch64-linux-gnu` 14.2.0-1 / GCC 14.2（[Debian package](https://packages.debian.org/trixie/gcc-aarch64-linux-gnu)）；`GCC5` 是 EDK II toolchain tag，不等于 GCC 5。 | Debian Snapshot 日期、每个 `.deb` SHA-256；`aarch64-linux-gnu-gcc --version`、`-dumpmachine`。 | 未交叉编译。 |
| Clang / LLVM | Linux Kbuild 首选候选 LLVM/Clang/LLD 23.1.5（2026-11-03 [官方发布](https://releases.llvm.org/)）；[内核文档](https://docs.kernel.org/process/changes.html)称当前最新正式 LLVM 发布受支持，旧版不保证。工具族须同版。 | 官方 release manifest/签名与包 SHA-256；记录 `clang --version`、`ld.lld --version`、`llvm-ar --version`。 | 未验证 Umi 配置；不是构建锁。 |
| EDK II BaseTools | 精确 [EDK II](https://github.com/tianocore/edk2) commit 配套；Debian 13 Python `3.13.5-1`、NASM `2.16.03-1`、ACPICA `20250404-1` 候选及 AArch64 GCC 14。 | APT snapshot 锁所有包/传递依赖 SHA；BaseTools 与包版本一致后再运行构建。 | 版本固定候选；未构建。 |
| Rust / Cargo | Rust 1.98.0（[官方发布](https://blog.rust-lang.org/releases/)，2026-08-20）；Recovery 暂选 `aarch64-unknown-linux-musl`。 | `rust-toolchain.toml` 固定 toolchain/target，依赖确定后再提交 Cargo.lock，并核 crate 许可。 | 无 Cargo 工程；未编译。 |
| Python | [Python](https://www.python.org/downloads/) Host tools 兼容 3.11.18；EDK II BaseTools 采用 Debian 13 Python 3.13.5-1 候选。 | 固定 patch、官方哈希和 Debian snapshot 包闭包。 | 仓库当前 L0/L1 有 3.11/3.14 证据；3.11.18 与 EDK 均未重新验证。 |
| QEMU | [官方 QEMU](https://www.qemu.org/download/) 10.2.4 + AArch64 `virt` / AAVMF 候选，用于 EFI Stub、initramfs、串口与基础内存边界回归。 | release tarball 签名/哈希；记录 `qemu-system-aarch64 --version` 和 AAVMF 固件哈希。 | 本轮未运行；不模拟 SM8250、面板、UFS PHY、ABL 或设备触控。 |
| Debian ARM64 | [Debian 13](https://www.debian.org/releases/trixie/) (trixie) `debootstrap`/官方包仓库作为 Recovery rootfs 候选。 | 固定 Snapshot 日期、suite、package manifest、包的 SHA-256 与 `/usr/share/doc/*/copyright`。 | 未生成 rootfs。 |

Host 初期固定为 Debian 13 amd64；Windows 仍只作为已有 L0/L1 证据的治理工具宿主。CI 使用 Ubuntu 24.04 / Windows runner，并非产品构建镜像。版本快照见 `manifests/toolchains.lock.json`，但容器 digest、APT 传递包哈希、Python/Rust/LLVM/QEMU 实际工件 SHA、Cargo 和 rootfs 闭包尚未完整记录，因此该文件明确标为 `reproducible-closure-incomplete`，不能宣称可复现构建。

安装方法与精确版本见 `manifests/toolchains.lock.json`。此处没有安装或交叉编译工具链，也没有执行 QEMU、EDK II、Rust 或 rootfs 构建。

## 维护者设备保护补充（2026-11-05）

维护者确认个人数据无保留需求，且 `.work/` 已有官方恢复包/工具；无需准备个人文件备份，但不能据此认定 ROM 可恢复。维护者禁止访问 `modemst1/2`、`fsg`、`fsc`、`persist*`、`mdm1m9kefs*`、`mdm1m9kefsc` 的分区内容，禁止读取、备份、计算分区哈希、写入、擦除或恢复；不得改变 IMEI、Wi-Fi MAC、SN 等身份信息。原厂 `dtbo` 在 MiSeed 实验中保持不动；官方恢复可按官方流程更新 `misc`。维护者严禁重锁 Bootloader。

维护者随后确认 `.work/` 为其准备的小米官方包，目标国行 umi；刷 Lineage 前运行官方 1.0.5，官方最新 Fastboot ROM 为 1.0.3。Lineage fingerprint 的 `TJBMIXM` 是当前软件标识，不用于否定实物销售区域或包来源。最新包内脚本范围见下文；均未执行，恢复入口仍未验证。

## 设备证据与恢复草案

| 项目 | 当前证据 / 状态 |
| --- | --- |
| 设备身份 | DEVICE_OBSERVED：`ro.product.device=umi`、`ro.product.model=Mi 10`、`ro.board.platform=kona`。USER_REPORTED：国行、SM8250、12GB/256GB。精确 SKU/板号和 SM8250 标识未独立核对。 |
| 内存 / 保留区 | USER_REPORTED 12GB；本机容量、reserved-memory、ABL 占用均 UNVERIFIED。 |
| 存储 / 分区 | USER_REPORTED 256GB；UPSTREAM_VERIFIED 候选说明使用 UFS；DEVICE_OBSERVED：by-name 查询得到 122 个链接名，含 `boot`、`recovery`、`dtbo`、`vbmeta*`、`super`、`userdata`、`abl`/`xbl_*` 及 `modemst1/2`、`fsg`/`fsc` 等；未见 `_a`/`_b` 后缀。真实 UFS 容量/LUN、GPT、分区大小及槽位语义仍 UNVERIFIED；无后缀不能单独证明非 A/B。misaka 自述 A-only，不推广到本机。 |
| 显示 | USER_REPORTED CSOT。UPSTREAM_VERIFIED ColorGarden 文档举例 `j2_mp_42_02_0b` 与 CSOT 的关联；本机 panel ID、DSC 型号/时序 UNVERIFIED。 |
| 触控 | UNVERIFIED：ST FTS 与 Goodix 均出现在同一候选下游设备树；不能由屏幕厂商推定触控 IC。具体 IC、固件、驱动需本机观测。 |
| Bootloader / ABL | DEVICE_OBSERVED：`ro.boot.verifiedbootstate=orange`；AOSP 将该值映射为 UNLOCKED（[Boot flow](https://source.android.com/docs/security/features/verifiedboot/boot-flow)），但 `ro.boot.flash.locked` 不可用。版本、fastboot 能力、ABL 来源/哈希均未采集。 |
| DTB / DTBO | UNVERIFIED：来源、版本、板型/面板选择和与当前 ABL 匹配关系未采集。禁止复用其他设备配置。 |
| Android / 固件 | DEVICE_OBSERVED：Android release=`16`、Lineage=`23.2-20261101-NIGHTLY-umi`、fingerprint=`Xiaomi/umi_global/umi:13/RKQ1.211001.001/V14.0.5.0.TJBMIXM:user/release-keys`。fingerprint 中 `umi_global` 是 Lineage 软件构建标识，不能证明或否定实物 SKU；其中 `13` 与 release 属性 `16` 不同，待解释。USER_REPORTED：国行设备在刷 Lineage 前运行小米官方 1.0.5；官方最新 Fastboot 包为 1.0.3。 |
| Recovery | USER_REPORTED：个人数据无保留需求；国行官方 Recovery ZIP 1.0.7、Fastboot ROM 1.0.3 和工具在 `.work/`。维护者允许官方脚本更新 `misc`；Recovery 入口未验证，lock 脚本严格禁止。未访问原厂分区。 |

ADB 最初不在宿主 PATH；使用 Google 官方 Android SDK Platform Tools 37.0.1（Windows）放在忽略的 `.work/` 临时目录运行，没有改系统 PATH。用户在手机上确认 RSA 调试授权后，`adb devices -l` 仅见一台状态为 `device` 的设备；具体标识不写入记录。以该明确选择的设备运行现有只读采集器，命令为 `python -X utf8 tools/collect_device.py --serial <已明确选择的设备> --output local/device-identity-20261105.json`，退出码 1（PARTIAL）。采集 JSON 留在忽略的 `local/`，不提交、不复制原始内容；未采集序列号/IMEI，未重启或切换手机模式。采集器只读取 allowlist 属性及 `/dev/block/by-name` 链接清单，不读取分区内容。AOSP 的 [Boot flow](https://source.android.com/docs/security/features/verifiedboot/boot-flow)将 `orange` 映射为 UNLOCKED；本机属性据此表明 Android 报告设备为解锁状态，但仍需确认 fastboot 能力/版本及可行恢复路径。维护者后续需提供/确认：脱敏设备完整资料、fingerprint 与 Android 版本差异、官方固件完整版本/地区与官方包 SHA-256、面板/触控 ID 的只读证据、Bootloader/槽位/ABL 版本；不得发送 IMEI、序列号或原始分区镜像。

### 回退准入方案（研究，不执行）

1. 维护者说明设备没有需保留的个人数据，故无需准备个人文件备份；系统恢复能力仍须独立核实。`userdata` 加密裸镜像不等同可恢复数据备份。
2. `.work/` 有两份中国区 ROM 文件：OS1.0.7.0 Recovery ZIP 和较旧 OS1.0.3.0 Fastboot TGZ。二者哈希仅与第三方目录摘要比对；来源签名及与当前设备固件/SKU 的匹配性尚未验证，不能据此保证恢复。MiFlash 压缩包未执行。
3. 本机 Android 属性 `orange` 按 AOSP 映射为 UNLOCKED；没有验证 fastboot 能力。不得为了验证而执行解锁或切换模式。若后续需任何状态变化，仍须单独当轮授权与回退方案。
4. 只在经核实支持时讨论一次性 RAM 启动；目前 `fastboot boot` 与本机 BootROM/ABL 接受格式、DTB、内存上限均未验证。即使不持久写盘，实验失败也可能失去启动/恢复入口，故这不是已成立回退手段。
5. A/B 槽位未知；不得假设另一个槽可回滚，也不得切槽/改槽位元数据。A-only 的上游陈述不能代替设备观察。
6. Fastboot/Recovery 无法进入时仅能转官方维修渠道。EDL/9008 需要匹配 Programmer、授权和设备兼容性；本项目没有相关输入或授权，不将 EDL 列为可用回退方案。

本机观测到的关键分区名包括 `boot`、`recovery`、`dtbo`、`vbmeta*`、`super`、`userdata`、`metadata`、`abl`/`ablbak`、`xbl_4/5`、`xbl_config_4/5`、`aop`、`tz`、`hyp`、`devcfg`、`persist`/`persistbak`、`modemst1/2`、`fsg`、`fsc`、`mdm1m9kefs*`。维护者明确禁止访问身份/NV/校准分区：`modemst1/2`、`fsg`、`fsc`、`persist*`、`mdm1m9kefs*`；禁止读取内容、备份、散列、擦除、写入或恢复。`misc`、`devinfo`、安全/防回滚分区及 `metadata` 不作为通用镜像回刷目标。行为约束见 [RECOVERY](../../RECOVERY.md)。

未审清官方恢复入口、当前启动链及一条保护身份/NV 分区、维持 BL 解锁的可行恢复路径前，不进入 L3 真机启动实验。个人数据无需备份（按维护者说明）；这不替代系统回退能力。任何刷写、解锁、分区、清除/格式化和供电更改均不在本轮授权内。

### 主机恢复包审查（只读）

维护者确认 `.work/` 中为其准备的小米官方包（USER_REPORTED）；文件 SHA-256/MD5 是本地完整性记录，不作为质疑来源的依据。只读打开 ZIP/TGZ 目录并读取安装脚本文本，未执行工具或脚本、未向设备写入镜像。设备为用户报告的国行 umi；Lineage fingerprint 的 `MIXM` 不用于判断硬件 SKU。维护者补充刷 Lineage 前运行官方 1.0.5、官方最新 Fastboot 包为 1.0.3；`.work/` 中另有 Recovery ZIP 1.0.7。

| 文件（相对 `.work/`） | 大小 | SHA-256 | 公开 MD5 核对 |
| --- | ---: | --- | --- |
| `miui_UMI_OS1.0.7.0.TJBCNXM_2cc115c0ee_13.0.zip` | 4,420,307,317 B | `1bfd996623dab1396bbb1b643bb8e4c2d21e46e52b08f9727f4841551fb84128` | `2cc115c0ee518846012b0f2972885e63` 匹配目录 |
| `umi_images_OS1.0.3.0.TJBCNXM_20240320.0000.00_13.0_cn_b616f9f481.tgz` | 5,651,129,785 B | `adc21dd1010b06dab5951f9328854aaa8d4c60f11dff7e300869429da6add389` | `b616f9f481971a00d614ad14f39bc238` 匹配目录 |
| `MiFlash2020-3-14-0.rar` | 69,989,804 B | `a3d924293e1dfa40163ac76241b16f176025cff6e8c9dbfa3b5e16e9969391a5` | 维护者提供的官方工具包；未解包或执行 |
| `platform-tools-latest-windows.zip` | 8,044,989 B | `45f4d63113e895ebde0c90f194099a4676b6ac653bd28d54314a9e022bbc1a99` | 与 Google Platform Tools 37.0.1 下载工件一致 |

Reference [umi-linux README](https://github.com/misaka-link/umi-linux) 的安全章节警告 modemst/fsg/fsc/persist/mdm1m9kefs 分区；MiSeed 继续禁止访问这些身份/NV 分区。维护者确认官方工具可正常更新 `misc` 且不会因此丢失身份信息，故该官方流程的 misc 更新允许。Fastboot `flash_all_except_storage` 更新多个系统/固件分区；`flash_all` 还擦除 `metadata` 并写 `userdata`；`flash_all_lock` 会重锁 BL，严格禁止该脚本及其他重锁操作。Recovery ZIP 的可读 updater-script 更新 boot、ABL/XBL、modem、DTBO、VBMeta 及动态系统分区，未列出 misc、userdata、metadata 或受保护 NV 分区；其 update-binary 未执行。Recovery ZIP 的 `boot.img`（SHA-256 `71acc2eb62b74d47da4d7ea856b21075073d8f767c6e74ae5780446a2b6fef6d`）优先审查，Fastboot 1.0.3 的 boot.img（SHA-256 `e81aae005fe1a768ab721bb9b248c665c974bfb8ec66b6911558b3fd1e5cf768`）作版本对照；本轮无设备操作授权。

## 许可证决定（2026-11-05）

| 方案 | 商用 / 再分发 | 专利与上游边界 |
| --- | --- | --- |
| BSD-2-Clause 或 MIT | 可商用、可闭源再分发；保留版权/许可文本与免责声明。 | 无明确专利授权条款；只许可 MiSeed 自有、可授权部分。不能放宽 Linux GPL-2.0-only 驱动或其他上游条件。 |
| Apache-2.0 | 可商用、可再分发；保留许可、版权/NOTICE，修改文件标明改动。 | 明示贡献者专利许可及特定专利诉讼终止条款；不适合与 GPL-2.0-only 代码合并成单一衍生作品（AGPLv3 兼容不代表 GPLv2-only 兼容）。仍只覆盖 MiSeed 可授权内容。 |

维护者随后指示采用强 copyleft 方向，本项目原创内容已选择 AGPL-3.0-only（[ADR-0005](../../ADR/0005-project-license.md)）。AGPLv3 对设备用户产品可能要求提供安装信息；支持远程网络交互的修改版本还须向交互用户提供源码，并含贡献者专利授权条款。AGPLv3 与 GPL-2.0-only 不兼容，Linux 派生文件须维持独立 GPL-2.0-only 边界。第三方许可不被根 LICENSE 覆盖，逐文件审查和镜像分发条件仍开放。

## 记录核对与覆盖

历史数量各属不同快照，不覆盖或合并：bootstrap 记录 17 项；recovery-foundation 记录完整回归 27 项（另有采集器 8 项子集）；first-commit 记录初次 27 项及后续初始化验收 28 项。development-basis 仅 L0。每条历史命令、退出码和范围保持原样；本轮同 HEAD 的新结果另记在 review 报告。

GitHub Actions：HEAD `69caa92e21892c58fdc800321428513c0d79c9d3` 的 push run [41985238351](https://github.com/Ben8368/UmiSeed/actions/runs/41985238351) 与 workflow_dispatch run [41985050729](https://github.com/Ben8368/UmiSeed/actions/runs/41985050729) 均为 completed/success；三个 Repository job（Ubuntu/Python 3.11、Ubuntu/Python 3.14、Windows/Python 3.14）success，`PR commits` skipped（非 PR）。GitHub 连接读取 branch protection/rulesets 返回 403，故 Require PR、Required checks、Force Push、bypass 状态均 `UNVERIFIED`。维护者建议配置 Require a pull request、要求上述相关 CI checks、禁止 Force Push、禁止未经授权绕过保护规则；本轮未改托管设置。

### 来源/工具版本固定结果

`manifests/sources.json` 的 11 个候选均已填入完整 SHA；正式输入方案见 `manifests/sources.lock.json`：Linux 6.18.60 LTS、TianoCore edk2，ColorGarden 及 Mu/umi 项目只作参考。锁文件记录 tag/object、仓库、用途、许可证据路径、子模块/补丁及闭包状态。其锁定的是源码身份，不表示参考代码已准入或补丁可应用。

`manifests/toolchains.lock.json` 固定了 Linux LLVM 23.1.5、Debian GCC 14.2 cross package、Python 3.11.18/EDK Python 3.13.5、NASM 2.16.03、ACPICA 20250404、Rust 1.98、QEMU 10.2.4 与 Debian trixie rootfs Snapshot 时间。工具官方版本依据见对应上游/release 和 Debian 包信息；安全工件 SHA、容器 digest、包依赖闭包、Cargo/rootfs 清单仍待材料化及校验。因此工具版本已选，但完整可复现工具链没有完成。
