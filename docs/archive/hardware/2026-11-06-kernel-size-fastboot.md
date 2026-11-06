# 2026-11-06 Umi 大内核 Fastboot RAM 启动验证

| 项 | 记录 |
| --- | --- |
| 日期 / 操作者 | 2026-11-06；维护者授权，Codex 执行主机打包与 Fastboot 命令 |
| 授权范围 | 明确授权构建 boot 镜像并 RAM 启动；明确禁止刷入。未执行 `flash`、`erase`、格式化、分区访问或供电修改 |
| 设备 / 变体 | Fastboot `product=umi`；Mi 10 / CSOT 为既有设备记录；序列号省略 |
| 原系统 / 底层固件 | 既有记录为 LineageOS 23.2；当前底层固件版本未从设备读取 |
| 仓库 / 输入身份 | `Ben8368/android_kernel_xiaomi_sm8250_ksu` 纯 LOS `lineage-24.0`；commit `6b34d24765327d746074c8e53b5ad0d24a51acc7`；本地实验分支 `codex/kernel-size-120m-probe`；Android Clang 12.0.5；Umi 配置及实验性尺寸探针 |
| 载荷 | 前三次大内核尝试见下文。基线包采用干净 LOS `lineage-24.0` commit `6b34d24765327d746074c8e53b5ad0d24a51acc7`，完整 Umi `.config`，未注入尺寸探针；内核 `Image` 50,655,744 字节，SHA-256 `eb64d5418da949c5c71a30e1805231c63917541ca603a2992b8f0ef29b935c14`；boot 镜像 51,802,112 字节，SHA-256 `a2efb298889ea3df3122b375f9490a5cfb59a25fcd201229bed789eafb20b0c9`。复用前次包的原厂 v2 boot 头、匹配 LOS Umi DTB（SHA-256 `88fe6048b98bfc2391cba66f09fdf6c7dbfa061bac4cebd6c227d8380f270474`）和 rescue initramfs；产物留在忽略的 `.work/` |
| 前置 / 回退 | Fastboot 只读查询：`product=umi`、`unlocked=yes`、`secure=yes`、`max-download-size=805306368`；ABL 版本未提供。救援 init 仅挂载 `devtmpfs/proc/sysfs`，无自动块设备挂载、fsck、swap 或持久写入。两次尝试后设备仍可被 Fastboot 识别 |
| 命令 | `fastboot -s <redacted> boot .work/kernel-size-probe/device-ramboot/boot-large-ram.img`；无写分区命令 |
| 结果 | 首包误用了 MiSeed L2 的 DTB，Fastboot 返回 `Booting OKAY`；约 12 秒后 Fastboot 仍在线、ADB 不在线，维护者观察到黑屏。随后改用同一原厂 OTA 的 Umi DTB；下载返回 `OKAY`，启动返回 `FAILED (remote: 'Failed to load/authenticate boot image: Load Error')`，退出码 1；Fastboot 仍在线。第三包改为完整 LOS Umi 配置及匹配的 LOS Umi DTB，并检查 Image 头 `image_size` 区间与 ramdisk、DTB 地址不重叠；第三次命令回执未捕获，之后 Fastboot、ADB 均未枚举。随后以无尺寸探针的干净 LOS 内核包测试两次：两次均 `Sending OKAY` / `Booting OKAY`，退出码 0；每次约 12 秒后只读查询均未枚举 Fastboot 或 ADB，维护者确认屏幕仍黑；无串口或内核日志 |
| 证据级别 | 以上为 LOS 24.0 历史实验：ABL 接受了部分镜像，但黑屏和 USB 枚举消失不能证明 Linux 已完成启动，也不能定位为编译错误或排除打包/配置错误。LOS 23.2 的后续控制实验与结论见下文 |
| 当时未覆盖 | 本机 ABL 版本与实际内存图未知；未取得屏幕图像、串口或内核日志；未测较小载荷来界定 ABL 接受边界。后续通过下方 LOS 23.2 官方 boot 对照确定可启动基线后，才恢复尺寸探测 |

## 地址与启动头摘要

第二个 boot 镜像保留 Android v2 头，使用 Umi 原厂 DTB。根据 DTB 中静态保留区和当时 ARM64 `Image` 头，试验包将 kernel、ramdisk、DTB 分别放在物理地址 `0x9e400000`、`0xa7000000`、`0xa8000000`；主机侧区间互不重叠。第三包及原始 LOS 基线包使用匹配 LOS Umi DTB，地址为 `0x9e400000`、`0xa8000000`、`0xa9000000`；均检查主机侧区间不重叠。该检查不覆盖 ABL 自身的内存保留区或实际分配策略。

存储预算探测与本次启动结果是不同结论：boot 分区容量计算不能证明 ABL 可加载，也不能证明大内核可运行。

## LineageOS 23.2 官方 boot 对照与源码重建

| 项 | 记录 |
| --- | --- |
| 官方来源 | [Umi builds API](https://download.lineageos.org/api/v2/devices/umi/builds) 的 `lineage-23.2-20261101-nightly-umi-signed.zip` 条目；官方 `boot.img` SHA-256 `c36e11ed589e02250bc5c8852b3f589b950533b0a9e18b0c3839e8e7a8f4eef3`，大小 134,217,728 字节。`build-manifest.xml` 锁定 kernel `71b13e62f057a649b77fe4062feb73ee72ad609c`、`android_device_xiaomi_sm8250-common` `da4ba935256abcc07973fb17d6b7729058fc9f9b`、`android_device_xiaomi_umi` `8e115b1ed63caa3ee5c3d52df0a7bce7c27512cd` |
| 内核配置 / 工具链 | 从官方 boot kernel 提取嵌入 `.config`；按源码 `olddefconfig` 后 5,312 项配置零差异。源码配置链为 `vendor/kona-perf_defconfig`、`vendor/debugfs.config`、`vendor/xiaomi/sm8250-common.config`、`vendor/xiaomi/umi.config`。Google Android Clang `r563880c`，来自官方 manifest revision `9916fb51ccb914d62d35ad9a7b9b21d2ef046928`，Clang/LLD 21.0.0；启用官方配置中的 LTO/CFI |
| 构建产物 | 干净上游源码 commit `71b13e62f057a649b77fe4062feb73ee72ad609c`；内核 `Image` 53,461,016 字节，带识别标记的 SHA-256 `706da926c802f1424a224d381c9288895727422f38fd8910447235011abbf627`。从官方 kernel 提取出的文件同为 53,461,016 字节、SHA-256 `b57fa319cfd4ab24a1c441da7ce77b3c93afd9112f7648ea07599107e2718a31`；配置、源码提交、工具链与版本字符串相同，但二进制哈希不同，未宣称可复现构建 |
| 对照包 | 基于官方 boot 重算 Android v2 SHA-1，仅替换 kernel payload；保留原厂 1,494,000 字节 ramdisk、1,424,944 字节 DTB、header、cmdline、加载地址及其它所有字节。镜像大小 134,217,728 字节，SHA-256 `7b9f644b395443ecdb144b53875801cc241237506319a900ebf5112c6fdab210`。只留在忽略的 `.work/` |
| 真机结果 | 先 RAM 启动原官方 boot：Fastboot `Sending OKAY` / `Booting OKAY`，ADB 回来；`ro.lineage.version=23.2-20261101-NIGHTLY-umi`，内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。再 RAM 启动源码重建包：Fastboot 同样 `Sending OKAY` / `Booting OKAY`；ADB `device`，`sys.boot_completed=1`，`/proc/version` 显示 `(root@codex-l23) ... #2`，确认进入 Android 的是本地重建 kernel。全程未执行 `flash` / `erase` |
| 证据级别 / 边界 | L3：LineageOS 23.2 Umi 精确源码提交 + 官方配置/boot 组件上的重建 kernel 已启动到 Android。该结果验证此构建与替换流程；尚不证明 bit-for-bit 可复现，也不证明大尺寸 kernel 可启动 |

这次对照与前面 LineageOS 24.0 / Clang 12 / 救援 ramdisk / 手工 DTB 的实验不同；此前黑屏不能归因于 kernel 编译本身。后续尺寸测试应以本节官方组件和配置为基线，单独改变 kernel payload。

## LOS 23.2 尺寸探针递增

均以官方 LOS 23.2 commit `71b13e62f057a649b77fe4062feb73ee72ad609c`、官方 `.config`、Android Clang 21 和官方 boot 为基线；只增长 kernel `Image`，官方 header、ramdisk、DTB、cmdline 与加载地址保持不变。镜像均在 RAM 中启动，未刷写。

| Kernel `Image` | 构建 / 打包 SHA-256 | 真机结果 |
| --- | --- | --- |
| 118.984 MiB（124,764,184 字节） | kernel `dcbf0a9af3eb058b18c0062154e027813f953b62759b124e1e19dcdfa25fe4ae`；boot `03736419e43768d30a8f146b362005b0101c8b2e700109f53cf8acfcf231bc19` | `fastboot boot` 成功；ADB 在线，`sys.boot_completed=1`，`/proc/version` 显示本地构建标记 `#2` |
| 122.984 MiB（128,958,488 字节） | kernel `983271e1dd6907c058f34770dfa2fd0499fb5dfec8a0f8ccd7126869cbbb1cb9`；boot `7f99672c52adc2a3f621e199262985f60331a8f824eafc4859671a45afbcb16b` | `fastboot boot` 成功；ADB 在线，`sys.boot_completed=1`，Lineage `23.2-20261101-NIGHTLY-umi`，`/proc/version` 显示本地构建标记 `#3` |

122.984 MiB 包含 128,958,488 字节 kernel、1,494,000 字节官方 ramdisk 和 1,424,944 字节官方 DTB，均按原 header v2 的 4096 字节页边界布局；DTB 段末端对齐到文件偏移 131,887,104，低于 134,217,728 字节 boot 镜像长度。此结果证明该配置下 122.984 MiB kernel 可 RAM 启动；它不是最大值。此前出现的 ABL `Load Error` 未确定根因，真实上限仍需继续递增探测。
