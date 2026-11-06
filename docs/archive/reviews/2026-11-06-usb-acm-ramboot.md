# USB ACM 与屏幕控制台首轮验证

日期：2026-11-06。目标是从只读诊断 initramfs 建立可见屏幕输出、USB 串口诊断及进入 ABL Fastboot 的重启路径。设备证据仅对应维护者当前连接的 Mi 10 / umi / LineageOS 23.2；其具体销售 SKU、主板修订和 ABL 版本仍未锁定。

## 来源与实现边界

- 板级 UART12、DWC3 peripheral/USB2 PHY 节点和 PHY rail 映射，按 BSD-3-Clause 的 `colorgarden/umi-mainline@5fa2e0048f89b82355d2a9de89c1d74ab9910959` `dts/sm8250-xiaomi-umi-common.dtsi` 选择性改写；源文件 SHA-256：`d4f51be4d33db8c82fe5fdf37a103bad4e23acf566b3cb2487305434a3219afa`。没有整体导入该板级 DTS。
- USB PHY 的 0.88 V、1.8 V、3.072 V 约束与本机 LineageOS 23.2 DTB 解码结果交叉检查。生成的 `official.dts` SHA-256：`b0cfdc6f52043589a177567f7753832e0183ae002d39c77bc87d7f3e4af030cf`；对应电源 rail 的官方 DTB 属性与上述上游板级 DTS 数值一致。该解码快照在忽略的 `.work/`，未提交原厂分区或二进制镜像。
- 未引入上游 AMOLED 驱动、面板 DSI 初始化、触控、电池、boost rail 或 GPIO 电平切换；显示路径保留 ABL framebuffer 并由 `simplefb`/fbcon 输出。
- initramfs 用 configfs 创建单一 CDC-ACM gadget，启用 `ttyGS0` console；只暴露 help、版本、内存、cmdline、mounts、普通重启和 `restart2("bootloader")` 命令。代码不挂载存储、不写 `misc` 或其他分区。`restart2("bootloader")` 的设备 ABL 语义未验证。

## L2 构建

主机：Windows 10.0.26200 x64 / PowerShell 7.6.9；WSL2 Ubuntu 24.04 x86_64；Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；AArch64 GCC 13.3.0；GNU make 4.3。

命令：`MISEED_JOBS=8 bash tools/build_kernel_l2.sh`，退出码 0。DTB overlay 检查确认 152 个 ABL 锚点。构建解析配置含 `USB_DWC3_QCOM=y`、`USB_G_SERIAL=y`、`USB_CONFIGFS_ACM=y`、`U_SERIAL_CONSOLE=y`、`FRAMEBUFFER_CONSOLE=y`、`SERIAL_QCOM_GENI_CONSOLE=y`。

| 产物 | 大小 | SHA-256 |
| --- | ---: | --- |
| Uncompressed `Image` | 5,661,184 B | `1179b019d39df22baa862842eb77c9aa5f36af7eebf4b968a188f38e6e07aa6a` |
| initramfs | 258,260 B | `70a7953b5b1b810c36509be7bbebbadb062315c8c676bd2979b642ae1c92ae89` |
| DTB | 165,414 B | `3c0d61588424f85ef9189300e0fbb39590fd2a3eb62979e9b9394c87a9d02f80` |
| Android v2 RAM boot image | 6,098,944 B | `2ec71450db34e58b1559d476db0f35306a9cdb220edffeae096da2c6af90c415` |

产物留在忽略的 `.work/boot-l2/`，没有加入 Git。

## L3 首次 RAM 启动

设备先处于已授权 ADB 状态。维护者本轮明确要求经 ADB 进入 Fastboot，因此执行 `adb -s <已选设备> reboot bootloader`，退出码 0；Android 标准 bootloader 路径会写入一次性 `misc`/BCB 启动消息。Fastboot 只读核实 `product=umi`、`unlocked=yes`，随后 `fastboot boot .work/boot-l2/boot.img` 返回 `Sending` 与 `Booting` `OKAY`，未执行 `fastboot flash`。

设备屏幕反馈仍为黑屏；Windows 没有枚举 MiSeed CDC-ACM COM 端口，Fastboot 传输仍可见。随后 `fastboot reboot` 返回原 LineageOS，`sys.boot_completed=1`。ADB 对 `/sys/fs/pstore` 返回 Permission denied，`/proc/last_kmsg` 不可用，未取得 MiSeed 内核崩溃日志。故首次 L3 **失败/未证实启动**：ABL 接受 RAM boot 命令不等于内核进入 init。

没有刷写、格式化、分区调整或触碰身份/NV 分区。普通系统返回路径已在本轮验证。

## Umi 内存图修正与复测准备

对比当前编译 DTB 与官方 LOS 23.2 DTB，发现 SM8250 MTP 默认保留区与 Umi 不同。官方 Umi 区域为 SLPI `0x88c00000+0x02f00000`、ADSP `0x8bb00000+0x02500000`、SPSS `0x8e000000+0x00100000`、CDSP secure heap `0x8e100000+0x04600000`。固定版本 `colorgarden/umi-mainline@5fa2e0048f89b82355d2a9de89c1d74ab9910959` DTS 也定义相同范围。修正并保留 UEFI log `0x80880000+0x14000`；编译 DTB 经 `fdtget` 读回这些值。这只证明布局对齐，尚未证明是黑屏根因。

完整对比显示 22 个官方静态保留范围中，21 个由 MiSeed 精确匹配；唯一未复制的是与 cont-splash 重叠的厂商 `disp_rdump_region`。固定版 Umi DTS 明确不采用该 MDSS dump 区域。新增 DFPS `0x9e300000+0x100000` 后，产物地址集合与其余官方范围精确相符。

最新 boot image 为 2,600,960 B，SHA-256 `5f53d23c0306501eb253685315da4f4f305fd22167c883e336902540dc2d28b4`。本轮第三次 `fastboot boot` 的 `Sending`/`Booting` 均 `OKAY`；屏幕仍黑，主机无 ADB/ACM，Fastboot 查询确认返回 Bootloader（`is-userspace=no`）。随后 `fastboot reboot` 回到原 LOS，ADB 恢复在线。内存图修正未解决启动失败。

当前仍无启动阶段日志和 ACM 枚举。下一步须取得早期日志，再分别验证 framebuffer、ACM shell 和 bootloader restart handoff；若需改走 BCB，须另行授权与准备回退方案。

未压缩 `Image` 对照：官方 LOS 使用 raw `Image`。此前 raw MiSeed `Image`（SHA-256 `a3a54985b00c6e4fd485da906dd9ba74a772e1b52abe11acf07669bcf5bb5ab4`，其它 DTB/initramfs/header/cmdline 不变）经 `fastboot boot` 后观察 45 秒，仍处于 Bootloader Fastboot；维护者报告屏幕黑。随后 `fastboot reboot` 回到原 LOS，ADB `/proc/version` 确认内核为原 4.19。因此 gzip 格式不是唯一故障来源。后续 profile 加入内置 `g_serial`，待单独 RAM 验证。

## 参考 DTBO 符号补齐与复测

从本地 `.work/miui_UMI_OS1.0.7.0.TJBCNXM_2cc115c0ee_13.0.zip` 仅提取 `firmware-update/dtbo.img` 作参考，SHA-256 为 `d6a6f4c07d7ce88f6213cfed0cee26d55f908e8eff38675faadc6f6e4671eee6`。解析出 29 个 overlay、241 个唯一目标符号；初始 MiSeed DTB 只有 156 个目标名可解析，缺 85 个。该包不是从已连接设备分区读取的，不能视为设备当前 DTBO 的证据。

新增 `kernel/umi-csot-dtbo-targets.txt`，仅记录 241 个目标标签名，不含 DTBO 二进制、属性或固件载荷。构建器与既有 152 个 ABL 参考符号合并生成空锚点，并在最终 DTB 逐项验证。构建输出写入 `.work/boot-l2/`：锚点 include SHA-256 `faad288a83a353234f0fd8b0300e9fdf18bdddf41463401091d31a4d77480361`；DTB 171,736 B，SHA-256 `793a0db07d4f00ac68ab556e04f3d2986835a5d00b577706bcf2a1b1e302c3f6`；raw `Image` 5,661,184 B；boot image 6,103,040 B，SHA-256 `60022402a31998b4adde7e4c7eefd5d1eff5058c28d841004645c9796db58952`。构建退出 0，152+241 符号检查通过。

复测前设备在原 LOS Android，ADB `sys.boot_completed=1`、kernel `4.19.325-cip131-st15-perf-g71b13e62f057`。维护者此前已授权 ADB 重启到 bootloader 并仅 RAM 启动。新包 `fastboot boot` 的 `Sending`/`Booting` 均 `OKAY`；等待 45 秒后 Windows 未枚举 ADB、Fastboot 或新的 USB 串口设备，只有主机 COM1。维护者观察黑屏。没有刷写。该证据确认 ABL 接受载荷，但仍未证实内核启动；尚不能区分早期内核停滞、USB 控制器/驱动未枚举或面板链路未启动。设备当前需恢复 Fastboot/LOS USB 连接后继续。

## 强制命令行与早期控制台诊断版

上一版无主机枚举后，核对本地固定提交 `colorgarden/umi-mainline@5fa2e0048f89b82355d2a9de89c1d74ab9910959` 的 Umi 启动诊断资料。参考资料提出强制内核命令行，避免 ABL 参数覆盖 `rdinit`/console，并保留尚未由主线驱动接管的时钟、电源域和稳压器。MiSeed 仅采用这些标准诊断开关，没有导入参考配置文件或其其他驱动：`CONFIG_SERIAL_EARLYCON=y`、`CONFIG_CMDLINE_FORCE=y`、`CONFIG_CMDLINE="console=ttyMSM0,115200n8 console=tty0 loglevel=8 keep_bootcon earlycon pd_ignore_unused clk_ignore_unused regulator_ignore_unused rdinit=/init"`。此项是诊断假设，不是已验证根因。

WSL2 Ubuntu 24.04 x86_64 上，Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、GCC 13.3.0、make 4.3 构建成功；resolved config 检查确认 `SERIAL_EARLYCON`、`CMDLINE_FORCE` 及完整 `CMDLINE` 均保留。boot image SHA-256 `5f583df666f17c1842bfb5f19906ecd94d11cf2a5369c8b6847276613528395c`，大小 6,103,040 B；raw `Image` SHA-256 `32f78078c12d00809adfab87777817c694197f2492245be3d411166709fe7d34`。28 项单元测试、仓库 L0 检查、`git diff --check`、`bash -n` 与 ShellCheck error 级通过。

该诊断版尚未再次 RAM 启动：上一版测试后 Windows 主机当前看不到设备，需设备回到 Fastboot 后继续。构建通过不是硬件启动证据。

## 维护者实拍与控制台改进

维护者提供的实拍与 VA48/PA48 镜像 `ec5452da8ee349cfffa2d971d2d36bf03f99bafeb8e3350dc2a48f5be67a1a15` 对应。屏幕显示 `MiSeed Umi/CSOT RAM diagnostic`；dmesg 可见 `simple-framebuffer ... registered`、fbcon 135×146 字符控制台及 `/init` 执行，确认 Linux 已进入救援 initramfs 并能使用 ABL simplefb。照片同时包含 `g_serial: couldn't find an available UDC` 与 ACM function 链接失败；Windows 检查未见 ADB/Fastboot/新增串口。此前“黑屏”观察已由这张图修正为 framebuffer 有输出。

用户要求日志横屏并增大字号。新 L2 配置启用 `CONFIG_FONT_TER16x32`、`CONFIG_FRAMEBUFFER_CONSOLE_ROTATION`，强制 cmdline 增加 `fbcon=font:TER16x32,rotate:1`。该功能只旋转 Linux fbcon，不会旋转面板原生模式或 Android UI，须新镜像 RAM 启动验证实际方向、可读范围和字号。

由于实拍明确显示 SoC 设备驱动缺失，配置补齐 `PINCTRL_MSM/SM8250`、Qualcomm common clock/SM8250 GCC、RPMh/电源域、Qualcomm interconnect、SMEM、SMMU 和 hwspinlock。该诊断假设并未证明它们是唯一 UDC 根因。新镜像 `.work/boot-l2/boot.img` SHA-256 `8bf2c622bc8bd7f6b5e1e11bcf64e907a12f4abd8e6421fd69593039a480b101`，大小 6,930,432 B；resolved config SHA-256 `7cc913b8b02065f8ae478846a810783d7d2c0fa9e329cc167dea7e6bd5f80571`，raw `Image` SHA-256 `435fc80f0dff5dda7b51555bbad40c149ebcf9d7735f0e8c8ec8f07094532d3d`。WSL2 L2 编译退出 0；有文件系统 mtime skew 警告，需以重复/稳定构建复核。尚未 RAM 启动新镜像。

新增要求：USB 主机需可控制 Linux 诊断内核、Fastboot 与 Android 三状态往返，且完成基础电源管理和充电。此处仅新增需求追溯和工作包验收门槛；当前 ACM/UDC、Android bootloader 重启语义及 PMIC 电池/保护链均未完成，不能将需求写成现有能力。

## WSL 构建 I/O 复核

同一固定源码、配置、GCC 13.3.0 和 make 4.3 完整 L2 构建改用 `MISEED_OUTPUT_ROOT=/tmp/miseed-boot-l2/fast-output`，将 Kbuild 对象放在 WSL ext4；退出码 0，无 clock-skew 警告。配置、`Image`、DTB、initramfs、boot 镜像的 SHA-256 与 Windows `/mnt/c` 输出相同，最终 boot 镜像 6,930,432 B。两次运行观察到 ext4 构建约 1 分 16 秒，挂载盘构建约 2 分 36 秒；单次对比不是稳定基准，但显示了默认工作目录优化价值。构建脚本现默认把 Kbuild 对象放在 `${TMPDIR:-/tmp}/miseed-boot-l2/build-<revision>`，最终产物仍留 `.work/boot-l2`，可用 `MISEED_BUILD_ROOT` 覆盖。

脚本默认路径更新后再以 WSL Ubuntu 24.04 + `MISEED_JOBS=8 bash tools/build_kernel_l2.sh` 从空 ext4 构建目录完整构建一次，退出码 0，无 clock-skew 警告。实际默认 Kbuild 路径为 `/tmp/miseed-boot-l2/build-f45ed76d6791cf72ff1265ee93511175bbbc2f18`，最终 artifact 留 `.work/boot-l2`；boot SHA-256 仍为 `8bf2c622bc8bd7f6b5e1e11bcf64e907a12f4abd8e6421fd69593039a480b101`。

使用镜像 `8bf2c622…9480b101` 在设备保持 Fastboot 时执行 `fastboot boot`，返回 `Sending OKAY`、`Booting OKAY`，没有 `flash`。维护者确认屏幕出现大字横屏日志，故显示/旋转的 L3 通过。Windows 45 秒轮询未见 ADB、Fastboot、Android USB VID、ACM 或串口设备，USB gadget/控制的 L3 失败。随后实拍确认 `g_serial: couldn't find an available UDC` 和 `Link ACM function failed: No such file or directory`；该输出与旧配置镜像对应。

维护者说明摄像头在横屏左下角，优先希望左侧竖排 `MiSeed`、日志排在右侧。该方案要由 init 接管 framebuffer/VT 并重绘 kmsg，需校准列布局与摄像头区域；维护者接受 180°旋转作为更简单的避让方案。新配置切换为 `fbcon=font:TER16x32,rotate:3`，并保留此前添加的 configfs functions/config/UDC 目录清单；boot 镜像 SHA-256 `1c06d856c4217097c80e4ecde1eb443e9f7f0de360c397d16920d43526ff53a4`，大小 6,930,432 B；resolved config `1e07a1b7e2a18fdc0151edc5454d5688e65459d23dffa8621c6fc14c208996b5`。WSL ext4 默认 Kbuild 目录构建退出 0，无 clock-skew 警告。新镜像尚未 RAM boot；需维护者物理倒转设备观察文字方向及摄像头避让，同时拍摄新的 UDC/configfs 诊断行。

维护者随后进入 Fastboot 并确认 USB 连接；校验镜像 SHA-256 后只执行 `fastboot boot`，`Sending`/`Booting` 均 `OKAY`，无刷写。维护者确认 rotate:3 后文字方向和摄像头避让效果正常，故新方向为 L3 通过。Windows 再观察 45 秒仍无 ADB、Fastboot、Android USB VID、ACM 或新增串口；configfs/UDC 诊断内容待维护者实拍核对。

## ACM configfs 链接路径修正

`1c06d856…26ff53a4` 的实拍显示 `functions/acm.usb0`、`configs/c.1` 存在而 `/sys/class/udc` 为空，`symlink()` 返回 ENOENT。Linux v6.18.60 `Documentation/usb/gadget_configfs.rst` 的 `ln -s functions/acm.usb0 configs/c.1` 是从 gadget 根目录执行；`fs/configfs/symlink.c:get_target()` 调用 `kern_path(symname, ...)`，所以相对目标仍按调用进程 cwd 解析。初次修正只将目标字符串改为 `functions/acm.usb0`，但 PID 1 cwd 是 `/`，最新 `c530db03…03d6251c` 实拍仍报告 ENOENT，证明该修正不完整。当前源码改用 configfs 内函数的绝对路径作为 `symlink()` 目标，保留 link 名 `configs/c.1/acm.usb0`，待新镜像真机确认。

新 L2 镜像 SHA-256 `55c514dedddb9148008d2e6cbf3c43c7346416fa368cf5b0b4fc16e8d27706f2`，大小 6,930,432 B；initramfs SHA-256 `06805c4a6840022727982981397f4410364d087d7a7fe08413a3131f4fbc11e5`。构建成功，尚未 RAM boot。此前空 UDC 列表是独立的底层驱动/设备树问题；修复 configfs link 不应被当作 USB 控制器已工作。

## USB 控制器探测诊断

维护者在 Fastboot 下确认设备为 `umi`，执行 `fastboot boot`，`Sending`/`Booting` 均 `OKAY`，未刷写。`c530db03…03d6251c` 的最新实拍新增只读 deferred-probe 清单；可见 `a6f8800.usb` 等待 `1500000.interconnect`，`1859000.interconnect` 等待 `/soc@0/rsc@18200000/clock-controller`，`88e3000.phy` 等待 RPMh LDO2。主机 30 秒内无 ADB、Fastboot、COM 口或匹配 USB PnP 设备。目标主线 DTS 的 `apps_rsc` 有 `power-domains = <&cluster_pd>`；该构建 resolved `.config` 关闭 `CPU_IDLE`，因此 PSCI cpuidle domain provider 缺失是 RPMh 子 provider 未就绪的一个待真机验证根因。目标内核源码的 compatible 与驱动匹配检查已通过：SM8250 QCOM glue 匹配 `dwc3-qcom-legacy` 的 `qcom,dwc3`，子控制器匹配 `snps,dwc3`。

`c530db03…03d6251c` 这轮测试已 RAM boot；新 L2 镜像已修正为绝对 configfs target `symlink(function, .../configs/c.1/acm.usb0)`，并在 config fragment 加入 `CONFIG_CPU_IDLE=y`、`CONFIG_ARM_PSCI_CPUIDLE=y`、`CONFIG_ARM_PSCI_CPUIDLE_DOMAIN=y`。上一 resolved config 中这三项关闭，而 SM8250 `apps_rsc` 节点通过 `power-domains = <&cluster_pd>` 依赖 PSCI idle-domain provider；RPMh RSC 是否因此恢复，仍待真机确认。新镜像 `.work/boot-l2/boot.img` SHA-256 `66cab926364a786477877581991f6cc3091f88d9fa63073b9013785a7a757b89`，7,131,136 B；resolved config `3c83bc378ed3cc6bf51b6385b5e30b7b7fe0f116490caee7db85074aa5b85587`；raw `Image` `8b862b638c3f9b12ae98f1af968a8cf8299158d3241649a6cc35d399a53d60eb`；initramfs `afffa7214c5c082a46bff9dc27769776e4b5fb1fc7e07a32c04978eaf5ff1161`。WSL Ubuntu 24.04 x86_64 / GCC 13.3.0，`MISEED_JOBS=8 bash tools/build_kernel_l2.sh` 退出 0，152 个 ABL anchors 与 241 个 DTBO symbols 检查通过；当前镜像尚未 RAM boot。任何 RPMh/UDC 因果关系仍以新镜像 deferred 列表和设备枚举为验收依据。

## 充电来源安全审查

检查 `.work/kernel-size-probe/lineage-23.2-official/extracted/official.config` 可见 LineageOS 23.2 配置启用 `QPNP_SMB5`、`QPNP_FG_GEN4`、`QPNP_QNOVO5`、`BQ2597X_CHARGE_PUMP` 和 `QCOM_SPMI_ADC5`。当前固定主线 Linux v6.18.60 的 `drivers/power/supply/Kconfig` 没有 PM8150B SMB5 或 FG GEN4 对应驱动；`CHARGER_QCOM_SMBB/SMB2` 不是可直接当作这些驱动的替代证据。

`.work/kernel-references/colorgarden-umi-mainline/docs/BATTERY.md` 与 `dts/xiaomi-umi-battery.dtsi` 是候选研究资料，不纳入源码。它们明确把 4.68 Ah / 4.4 V 标为近似电芯值，并在 BQ2597X 节点关闭多项电池、温度保护，且未配置完整主充电限流/温控策略。这些参数和补丁不得直接移植或启用。当前可继续核对本机 LOS DTB/DTBO、内核驱动支持和安全遥测；主动充电等待匹配板级数据、保护审查及专项 L3。

## VA48/PA48 诊断镜像 RAM 启动

与 LOS 官方配置（VA39/PA48）以及固定版 Umi bring-up 配置（VA48/PA48）对照后，发现 allnoconfig 本轮解析为 VA52/PA52。Linux 6.18 Kconfig 对 VA52 说明为硬件支持时启用 52-bit userspace VA，否则内核回退到 48-bit；PA52 支持 LPA CPU且对不支持者增加内存/性能开销。为贴近 Umi 主线板级 bring-up，将 MiSeed 解析配置固定为 VA48/PA48。该差异是待验证启动假设，不是已证实根因。

最终 `.work/boot-l2/boot.img` SHA-256 `ec5452da8ee349cfffa2d971d2d36bf03f99bafeb8e3350dc2a48f5be67a1a15`，6,103,040 B；resolved config SHA-256 `672ef107337d1243cdf2a24ef7a1167a2ad4b1f843f263927acc211f3c403d87`，raw Image SHA-256 `b67f2943938ea46de66da16e53431a62d635a7e5ffe77ef61280537e75bef483`。通过本机 Fastboot RAM 启动，`Sending`/`Booting` 均 `OKAY`。等待 45 秒后 Windows 未枚举 ADB、Fastboot 或 USB ACM。

维护者在该次启动流程前后报告曾看到文字界面，随后手动重启回 Fastboot；该文字界面未与特定镜像哈希及内容对应，故暂不记作本镜像的 L3 显示验收。需要确认画面文本，特别是是否显示 MiSeed rescue initramfs 标识。ARM64 Image 头部与 LOS 官方 kernel 也有 `text_offset` 差异（MiSeed 0、LOS 0x80000）；依据 Linux ARM64 boot protocol，载入地址需遵守 2 MiB 基址加 `text_offset` 的约束，ABL 对当前 raw Image 的实际放置仍需进一步证据。参见[Linux ARM64 启动文档](https://docs.kernel.org/arch/arm64/booting.html)。

## PM8150L provider、CDC-ACM 与 USB 状态往返

`c530db03…03d6251c` 实拍显示 `regulators-0` 的 BOB supplier、QUSB2 PHY LDO2 和 DWC3 处于 deferred 链。对照固定 `colorgarden/umi-mainline@5fa2e0048f89b82355d2a9de89c1d74ab9910959` 的 `dts/sm8250-xiaomi-umi-common.dtsi` 及锁定 Linux v6.18.60 `drivers/regulator/qcom-rpmh-regulator.c` 后，确认 Umi `apps_rsc/regulators-1` 的 BOB provider 是 `qcom,pm8150l-rpmh-regulators`、PMIC ID `c`；原 MiSeed DTS 错配为 PM8150B / ID `b`。仅修正 compatible 与 ID，不改电压范围或充电参数。镜像 `a34f6e72…b517124df` 构建并 RAM boot，主机 20 秒未见设备。

该镜像后续诊断版 `3a776049…5d0b8fe7` 的实拍显示运行时 DWC3 `compatible=snps,dwc3`、`dr_mode=peripheral`、`status=okay`，PHY、Qualcomm glue 和 DWC3 platform devices 均有 driver symlink。ACM configfs function link 成功，但给 UDC 写值返回 `Device or resource busy`，同时出现 `g_serial ready`。这证明 `CONFIG_USB_G_SERIAL` 自动 gadget 和 initramfs configfs gadget 争用一个 UDC。

修正版 `.work/boot-l2/boot.img` SHA-256 `f7d50a25e1d6c4c644133c6ef77764851283ed6d8cfea6c96ad15ff808d84689`，大小 7,127,040 B；关闭 `CONFIG_USB_G_SERIAL`，由 configfs ACM 单独绑定 UDC。WSL2 Ubuntu 24.04 x86_64 / GCC 13.3.0 / GNU make 4.3 上 `MISEED_JOBS=8 bash tools/build_kernel_l2.sh` 退出码 0，152 个 ABL anchor、241 个 DTBO target symbol 静态检查通过。

维护者将该镜像以 `fastboot boot` RAM 启动，屏幕实拍确认 DWC3 peripheral/okay、输出 `USB CDC-ACM diagnostic gadget bound to a600000.usb`。Windows 设备管理器枚举 USB Composite Device `VID_1D6B&PID_0104` 及 CDC Serial `COM6`。通过 COM6 115200 连接后发送 `help`，收到 MiSeed 菜单，USB ACM console L3 通过。该结果不代表完整状态切换。

串口 `bootloader` 命令执行 `restart2("bootloader")` 后 USB COM 消失；主机 25 秒内未见 Fastboot，但随后 ADB 枚举恢复。只读 ADB 查询显示 Android `sys.boot_completed=1`、kernel `4.19.325-cip131-st15-perf-g71b13e62f057`，确认此命令回到了原 Android，未进入 Fastboot。之后执行 `adb -s ec04f57f reboot bootloader`，Fastboot 检测到同一设备 `product=umi`；`fastboot reboot` 成功回 Android，ADB 再次看到 `sys.boot_completed=1` 和原 4.19.325 kernel。因此 Android→Fastboot→Android L3 往返通过，MiSeed diagnostic Linux→Fastboot 仍失败。

没有执行 `fastboot flash`。通过 ADB 请求 bootloader 会使用 Android 标准一次性启动消息；MiSeed 自身若需写 `misc`/BCB 才能进入 Fastboot，仍须先明确分区身份、准备回退方案并取得该轮明确授权。当前 PR1 不推送、不合并；充电仍受 RISK-004 安全阻断。

## Bootloader 消息与只读电源观察

在 Android AOSP `bootable/recovery` 中核对 `write_reboot_bootloader()`：它先读取 bootloader message，拒绝覆盖非空 pending command，再将 `command` 设为 `bootonce-bootloader` 并写回 BCB；消息位于 misc 的偏移 0。Android U-Boot 参考实现识别此 command，并将其当作一次性 bootloader 请求。来源：[AOSP recovery `bootloader_message.cpp`](https://android.googlesource.com/platform/bootable/recovery/%2B/master/bootloader_message/bootloader_message.cpp)、[AOSP bootloader message 定义](https://android.googlesource.com/platform/bootable/recovery/%2B/refs/heads/main/bootloader_message/include/bootloader_message/bootloader_message.h)。MiSeed 当前 `restart2("bootloader")` L3 实测回到 Android；如果采用 BCB，当前 L2 还未启用 UFS host（resolved config 无 `CONFIG_SCSI_UFSHCD` / `CONFIG_SCSI_UFS_QCOM`，SM8250 默认 UFS 节点也为 disabled），须先完成只读存储枚举与准确 misc 识别。不得在授权前增加或执行真实块设备写入。

`adb reboot bootloader` 所需的 Android 标准路径与 `fastboot reboot` 已在设备上通过；这不表示 MiSeed 可写 BCB。只读 ADB 显示当前 Android 的 `/dev/block/by-name/misc` 符号链接为 `/dev/block/sda11`，`ro.boot.bootdevice=1d84000.ufshc`，slot suffix 为空；这些标识只对应当前 Android 内核视图，不能直接硬编码到 MiSeed。该操作返回 Android 后，`getprop` 观察为 Xiaomi global V14.0.5.0.TJBMIXM / Android 13、kernel `4.19.325-cip131-st15-perf-g71b13e62f057`，与较早 partial ADB snapshot 所记 LineageOS 23.2 存在冲突，当前 OS/固件输入需要重新锁定。

同一次 Android 会话中只读 `dumpsys battery` 返回原始值：SOC 100、4368 mV、battery temperature 332（Android 常用十分之一摄氏度）、status 3、health 2、technology Li-poly，充电电源均 false；`/sys/class/power_supply/*/uevent` 从普通 ADB shell 读取被 Permission denied。该瞬时 Android 遥测没有用来推导 MiSeed 的电芯上限、充电电流、保护策略或热阈值；主动充电仍禁止，见 RISK-004。

## Qualcomm PON bootloader 模式候选

Linux 主线 SM8250 Xiaomi elish/pipa 与 Qualcomm SM8250 参考板 DTS 使用 PON 属性 `mode-bootloader = <0x2>`；Linux v6.18.60 的 `qcom-pon` 匹配 `qcom,pm8998-pon`，reboot-mode notifier 会在 `restart2("bootloader")` 时把对应 magic 写到 PMIC `PON_SOFT_RB_SPARE`，不访问存储分区。基于此加入 Umi PON reboot-mode 属性，并启用 `MFD_SPMI_PMIC`、`POWER_RESET_QCOM_PON`。这仅是同平台上游证据，Umi ABL 的识别行为仍待实机确认。来源：[SM8250 Xiaomi PON DTS patch](https://lists.openwall.net/linux-kernel/2024/12/02/397)、锁定 Linux 源码 `drivers/power/reset/qcom-pon.c` 与 `reboot-mode.c`。

L2 在 WSL2 Ubuntu 24.04 x86_64、GCC 13.3.0、GNU make 4.3 上执行 `MISEED_JOBS=8 bash tools/build_kernel_l2.sh` 成功，返回码 0；152 个 ABL anchors 与 241 个 DTBO targets 检查通过。产物 `.work/boot-l2/boot.img` SHA-256 `d671391c8925be38cbc85c175348745de2b09fbc9b831db093bfc98b1f83d48f`，7,127,040 B；resolved config `8c6e7caac2f17f1e657bf9eedb228130ce57394524c5fd8006eeeb99a1822c9a`；kernel Image `45b4e25540d5c877869eca06c8372acea995abdb91bb8ba6259bffd696dfe9d1`；DTB `32331bad24d382f7f3be99b8ed330b0a84a1ec4132bf1a5843c180e3408993e3`。Fastboot `boot` 返回 `Sending`/`Booting OKAY`，未刷写。

RAM 启动约 15 秒后，Windows 未枚举 ADB、Fastboot 或 COM 设备。Windows System PnP 日志最近一次 MiSeed Composite USB 记录仍为 19:23（VID `1D6B`、PID `0104`，对应先前 f7d50a25 ACM 验证）；19:45 启动新候选后没有匹配的新 PnP 事件。仅能确认主机未观察到枚举，不能据此判断手机内核停在哪一步；手机当前屏幕/状态待维护者确认。未触发串口 `bootloader` 命令，因此本轮未测试 PON handoff。

为区分 PON 探测与 USB gadget 初始化状态，initramfs 新增只读输出：读取 DT `/pon/mode-bootloader` 的 u32 值，并遍历 platform device 的 `of_node/compatible` 查找 `qcom,pm8998-pon` 及其 `driver` symlink；bootloader 命令提示改为“PMIC PON mode”，明确不写存储。L2 重建退出码 0，152 个 ABL anchors 与 241 个 DTBO targets 检查通过；当前 `.work/boot-l2/boot.img` SHA-256 `bac940c29880d3a17888decb87537f4399a5d78a65f7632526d44842c4bdc65d`，initramfs SHA-256 `b9a6660594f8659befa79334f3b97cb32c40b2bf4b6ad6bbb6b23dbf1ad2fd88`。新诊断版尚未 RAM 启动，须先恢复 Fastboot USB 链路；没有设备画面或 PON 行为的 L3 结论。

维护者重新确认手机处于 Fastboot 且 USB 连接。主机 `fastboot devices -l` 识别 `ec04f57f fastboot`；镜像 SHA-256 与 `bac940c2…c4bdc65d` 相符。执行 `fastboot -s ec04f57f boot .work/boot-l2/boot.img`，`Sending` 和 `Booting` 均返回 `OKAY`，未运行 flash 命令。等待 15 秒后主机没有 ACM、ADB、Fastboot 设备，PnP 列表只有 COM1；短时间窗口内未见匹配的 USB PnP 日志。设备屏幕状态待维护者实拍，因此尚不清楚内核是否到达 initramfs、PON 是否绑定或 UDC 是否就绪；没有触发 `bootloader` 命令。

维护者实拍该启动画面，确认内核已进入 initramfs；`DT PON mode-bootloader=0x00000002`，但 `/sys/bus/platform/devices` 扫描未找到 `qcom,pm8998-pon` 设备；DWC3 `compatible=snps,dwc3`、`dr_mode=peripheral`、`status=okay`，PHY、Qualcomm glue、DWC3 驱动均有绑定链接，但 `/sys/class/udc` 为空，initramfs 输出 `no USB device controller registered`。日志还显示 GCC clock controller 的 `sync_state()` 因 `88e3000.phy` pending。该实拍确认诊断内核启动到用户空间，但 PMIC PON 子设备和 gadget UDC 均未就绪；尚未触发 PON reboot 命令。注意，initramfs 对 PON 设备的扫描只查 `/sys/bus/platform/devices`；后续增加 SPMI PMIC driver directory 和更晚的 kmsg 尾部以确认父设备是否实例化。

为捕获本次遗漏的后续探测日志，新增诊断版把 `/dev/kmsg` 中最后 18 条匹配 DWC3/USB/PHY/PON/SPMI/UDC/pending 的信息保存在固定大小环形缓冲，并列出 `/sys/bus/platform/drivers/qcom-pon`、`/sys/bus/spmi/drivers/pmic-spmi` 绑定项。首次编译因 `-Werror=format-truncation` 失败，改为显式 `strnlen` + 有界 `memcpy` 后重建成功。`MISEED_JOBS=8 bash tools/build_kernel_l2.sh` 在 WSL2 Ubuntu 24.04 x86_64 / GCC 13.3.0 / GNU make 4.3 退出码 0；152 个 ABL anchors、241 个 DTBO targets 检查通过。当前 `.work/boot-l2/boot.img` SHA-256 `7c0c67accb82648519da6367fa8be0353750d026d5603c15d7b81afc48cfeb96`；initramfs `024908621690b9c2709a31bf612f0a48720efeefdf983724b60425db954ef299`。尚未 RAM 启动该新镜像。

## 暂用 Android 中转路径与 PON/SPMI 状态

2026-11-06 在当前 MiSeed 工作镜像上完成用户接受的中转路径。最新 PDC 关闭诊断镜像 `.work/boot-l2/boot.img` SHA-256 `33b986f64a57b4ee8d1c5528e657c3bc38e85f0ac40faf5700dd4ad9f76a3d6c`，大小 7,131,136 B；initramfs `84c5eb874bbce40e8848ec1e144b152eb2f798e0217fe85b9cd6f99d91a3cd67`；resolved config `8c6e7caac2f17f1e657bf9eedb228130ce57394524c5fd8006eeeb99a1822c9a`。`MISEED_JOBS=8 bash tools/build_kernel_l2.sh` 在 WSL2 Ubuntu 24.04 x86_64 / GCC 13.3.0 / GNU make 4.3 退出码 0；152 ABL anchors 和 241 DTBO target symbols 检查通过。

RAM 启动后 Windows 枚举 CDC-ACM `COM6`。串口 `status` 读到 `mode-bootloader=0x2`；运行时 PMIC 子节点 `status` 缺省（默认启用），`reg` 均为两个有效单元；但 `c440000.spmi` 的 platform driver 未绑定、`/sys/bus/spmi/devices` 为空，`qcom,pm8998-pon` 设备缺失，PON 仍不可用。将 `CONFIG_QCOM_PDC=y` 的对照镜像 L2 构建成功，哈希 `b65bf4f0e6d35b8228481d56ca7829e559c3ee3126dbd271f66b2d78c6dcf9dc`；两次 RAM 启动均未枚举 USB，主机曾在约 40 秒后看到 Android ADB。为避免回归，PDC 选项已撤销；当前源配置不包含 `CONFIG_QCOM_PDC=y`。

在 PDC 关闭版上发送串口 `bootloader` 后，COM6 断开并回到 Android；ADB 检测到 `umi`、`sys.boot_completed=1`、内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。随后 `adb reboot bootloader` 进入 Fastboot，设备 `ec04f57f` 被识别；`fastboot reboot` 回 Android，ADB 再次确认 `sys.boot_completed=1`、产品 `umi` 和原 4.19.325 内核。该次全程未 `fastboot flash`；Android 标准 bootloader 请求使用一次性 BCB 路径，Fastboot reboot 为恢复点。

维护者决定暂缓 Linux 直达 Fastboot/PON 问题，先使用 Linux→Android→Fastboot→Android 中转路径。电源管理和充电仍未达标，继续受 RISK-004 安全阻断；本记录不宣称充电或直达 Fastboot 已实现。
