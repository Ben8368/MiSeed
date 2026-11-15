# 2026-11-11 原生 DSI 镜像首次 RAM 验证

## 环境、授权与载荷

| 项 | 本次事实 |
| --- | --- |
| 主机 / 日期 | 2026-11-11 12:05 起（Asia/Shanghai）；Windows 10.0.26200 x64、PowerShell 7.6.9、Python 3.14、pyserial 3.5 |
| 授权 | 用户明确授权启动镜像验证，设备已连接 Fastboot；按前一轮方案仅做临时 RAM 启动、显示/最终 DT/绑定检查与正常回退 |
| 设备 | 唯一设备 `product=umi`、`unlocked=yes`、`is-userspace=no`；沿用此前 CSOT 身份，未读取面板 IC ID |
| 工作树 | `codex/native-dsi-dt-power`，起点 `b73f19db3c7ce52209ea26a058f58f747f31157b` 加未提交改动；镜像内附来源锁的全部项目输入哈希与启动前代码一致 |
| 内核 | Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，串口返回 6.18.60 / MiSeed@Ubuntu2404 / GCC 13.3.0 |
| 镜像 | `.work/native-dsi-20261111/boot.img`，10,887,168 bytes，SHA-256 `cc7687f9a3cc1e1214dbb09664cd95606217f8abbf0c8b0fcda1c3c8c8dada9e` |
| 配置 | SHA-256 `6a82231e018e725a6c41ae0e6fd1903151522b935b4871526544773648dc609c`；原生 DSI/CSOT、BQ core/auxiliary/PPS 已启用；MMC/SCSI 未启用 |
| 前置 / 回退 | 已完成 L0/L1/L2 与产物哈希检查；FDT 审计失败即保全日志，经救援 `bootloader` 回 Fastboot，再 `fastboot reboot`；不刷写分区 |

## 实际操作和结果

1. 用项目已准备的 Google Platform-Tools 查询唯一 Fastboot 设备，按枚举到的真实序列号定向执行 `fastboot -s <设备> boot .work/native-dsi-20261111/boot.img`。Sending 与 Booting 均 OKAY，退出 0；约 8 秒内 MiSeed VID 1d6b/PID 0104 的 COM6 出现。
2. 按 VID/PID 与 MiSeed console greeting 核对串口身份，115200 下读取 `version`、`status`，均完整返回提示符。PON、SPMI controller、USB DWC3 与 BQ I2C 驱动已绑定。
3. `python -X utf8 .work/capture-native-fdt.py --port COM6 --reference .work/native-dsi-20261111/umi-csot.dtb --output .work/native-dsi-l3-20261111/runtime-fdt.txt` 完整保存最终 FDT，但严格审计退出 1：`root hardware identity changed: compatible`。不是导出丢段；保留失败，不放宽判断。
4. `python -X utf8 tools/collect_charge_log.py --port COM6 --output .work/native-dsi-l3-20261111/boot-charge.log --timeout 45` 退出 0：144 条、序号 0–143、storage_errors=0、无覆盖。此日志含筛选后的供电/USB/SPMI 内核消息，不是完整 dmesg。
5. 保存材料后发送救援 `bootloader`；Fastboot 重新枚举且 `product=umi`。随后定向 `fastboot reboot` 退出 0。主机后续实际枚举未发现 ADB、Fastboot 或 MiSeed CDC，不能据此断言 Android 启动成功或失败；最终屏幕状态待维护者确认。

## PMIC/SPMI：本载荷未复现重复注册

- live `/soc@0/spmi@c440000` 与镜像的整个子树逐属性完全相同；8 个地址 00/01/02/03/04/05/0a/0b 均绑定 `pmic-spmi`，没有 live 下游重复命名节点。筛选后的启动日志未见 `-EEXIST`/重复注册消息；不把日志筛选当作完整内核无错证明。
- 下游 `qcom,pm8150@*`、`qcom,pm8150b@*` 等节点实际落入 disabled `/abl_ovf/isolated-spmi-bus`，说明符号隔离在这次真实 ABL 合并中生效。
- live TLMM 全子树、三个 live/sink 符号、隔离区 disabled 状态和 aliases 保持不变；未发现重复 phandle。这是本机本载荷的运行时隔离证据，不等于全部 PMIC 功能验收。

## 实际发现的额外 DT 改写

- 根 compatible 从 `xiaomi,umi` / `qcom,sm8250` 改为 `qcom,kona-mtp` / `qcom,kona` / `qcom,mtp`；model 也被下游覆盖。
- `/soc@0/mmc@8804000`（原符号 `sdhc_2`）从 disabled 改成 ok，增加 cd-gpios、pinctrl、供电引用与下游电气属性。本镜像 `CONFIG_MMC` 未启用，没有执行存储读写，但该覆盖违反预期隔离边界。
- `/soc@0/display-subsystem@ae00000/display-controller@ae01000`（原符号 `mdss_mdp`）新增 `#cooling-cells`、`connectors` 和 201 个下游子节点。live soc 直属子节点虽未改变，后代已被污染。
- 当前审计的 soc 检查仅覆盖直属拓扑/属性，SPMI/TLMM 才比较完整子树；根兼容值失败后逐节点人工复核找出了以上内容。因此不能只给根 compatible 加白名单；须扩大受保护范围并为深层 overlay 变化补反例，再评估 symbol 隔离目标。仍未取得本机 DTBO 分区内容/所选条目的哈希，不从符号差异臆断具体 overlay 条目。

## 显示与有线驱动范围

最终 FDT 中 MDSS、DSI0、7nm PHY 均为 okay，`xiaomi,umi-csot` panel 节点保留。但没有取得 DRM card/connector/DSI/panel 的 sysfs 绑定与 modeset 记录，也没有维护者目视反馈。`charge-log` 的过滤词不包含正常 DRM/DSI 消息，`status` 也未覆盖显示绑定；这是本轮采集准备缺项，不能以“无 DSI 错误日志”或 DT 节点存在判点屏成功。需先离线补齐显示状态与完整启动日志采集，再安排下一次测试；本轮未现场重建或再次启动。

有线 `15-0066` 已绑定 `bq25970`，内核明确记录 auxiliary `bq25970_core.charger.0` 的 Umi PPS policy ready；这是新拆分驱动的首次 probe/标准供电注册 L3。BQ 采样保持 Not charging、等待合格 PD/PPS。未做 PPS 充电、解绑/故障注入或互锁恢复专项，不能据绑定成功关闭 TD-004。

未发送 charge-test/charge-stop/thermal 控制，未改 watchdog 或电气阈值。镜像原有 SMB5 自动策略在电脑 USB 连接下进入 fullon/Charging，电池读数约 42→40°C；“未发起充电测试”不等于整机始终禁止充电。无线 RX/TX 未测试。

## 证据与后续

原始文件仅在忽略目录 `.work/native-dsi-l3-20261111/`；完整摘要在 `evidence-hashes.json`。最终 FDT 551044 bytes，SHA-256 `acd856dc5178692471b7d2f8f8bf9806080568154ecaf688a593fd7bc7b219d0`；144 条日志 SHA-256 `25dcf9fde4c96009b4b67e546b4fe1b19a4595c1793d88a0a471f3a5996e82fd`；状态采集 SHA-256 `13e79fb2b9381b690b9bfe4cd0d0c444c5ffbd25edda500c9d8145e3fdb510de`。

🚦 本镜像 RAM 启动、USB、8 个 PMIC 绑定、SPMI/TLMM 隔离及 BQ provider/policy probe 证据为绿灯；原生显示和 Android 启动完成未确认。DT 整体审计失败，root/DPU/MMC 覆盖与采集缺项是下次验证前必须处置的阻断项，RISK-010 保持开放。没有新增源码修改、刷写、分区或保护控制；本轮仅记录 L3 结果，文档执行仓库 L0 与 diff 检查，未提交/推送/合并。

## 维护者随后反馈（本轮续接对话）

用户明确反馈：「手机现在是安卓，刚才 RAM 启动期间，屏幕黑屏」。因此本次原生镜像的目视结果是黑屏，正常返回 Android 为 USER_REPORTED；此前主机没有 ADB 枚举不构成返回失败。没有显示绑定/modeset 证据，黑屏根因仍不能归结为单一亮度或单一 overlay 问题。

参考项目与补救实施续查见[同日研究记录](../research/2026-11-11-native-dsi-chip-boundaries.md#黑屏反馈后的续查与补救)。该反馈不产生新镜像的 L3，也不改变首次载荷哈希和原始采集范围。

## 本机 DTBO 后续只读采集与重放（2026-11-11）

### 授权、环境与边界

用户在本轮确认手机已连接 Android，用于已准备的 DTBO 只读采集；沿用[此前明确允许 Android 临时 adb root 获取信息的授权](2026-11-08-android-root-power-audit.md#范围身份与授权)，本轮未撤回。Windows 10.0.26200 x64 / PowerShell 7.6.9 / Python 3.14、Google ADB 37.0.1；唯一授权 Umi，实际序列号仅用于 -s 定向，不输出或归档。工作树仍为 b73f19db3c7ce52209ea26a058f58f747f31157b 加未提交改动，无提交、推送或合并。

初始普通 shell 为 uid=2000，没有 su；DTBO 与 devicetree 属性读取均 Permission denied。没有把 exec-out 的退出 0 当作有效数据：实际输出为错误文本，DTBO magic 不符。现场未执行 adb root；先安全结束只读探查并告知用户可断开，在主机离线准备现有/临时 adbd root 支持、权限恢复及失败反例。Windows/WSL 各 8 项通过后，执行显式 --temporary-adb-root 采集；不安装 su、不 remount，不重启 Android，不写手机文件或分区，不访问身份/NV，不操作有线/无线充电配置。ADB 服务的临时 root/unroot 不冒充全程普通权限。

### 实际采集结果

| 项 | 本机结果 |
| --- | --- |
| 系统交叉核实 | release=16、SDK=36、LineageOS 23.2-20261108-NIGHTLY-umi；kernel 4.19.325-cip131-st15-perf-g71b13e62f057；userdebug |
| 指纹口径 | 同次系统/vendor fingerprint 均保留 Xiaomi/umi_global/umi:13/RKQ1.211001.001/V14.0.5.0.TJBMIXM:user/release-keys；不能单凭指纹把当前系统判作 Android 13。旧日期快照仍作为历史，不据此臆断 ROM 切换时间 |
| 权限 | ro.debuggable=0、无 su，但已获授权的标准 adb root 可用；采集前/后 ADB shell uid 都为 2000，实际读取期间临时为 root |
| 操作 | python -X utf8 tools/collect_dtbo.py --adb .work/platform-tools-20261105/platform-tools/adb.exe --output .work/root-dt-device-20261111 --temporary-adb-root；退出 0，约 8.2 秒 |
| DTBO | 只读 /dev/block/by-name/dtbo，33,554,432 bytes；SHA-256 f9e1a4b9e38b0c79cc9ad86aefd75bb4060d0b8a4e46f1baf5b516881f68593c |
| 条目 | 共 12 项，ro.boot.dtbo_idx=11；选中条目 offset=5,053,795、460,783 bytes；SHA-256 ebef7c8ecdc14fe096e69d2c6d570edae681c48462c74e715e0fc8a9734022a6 |
| 根属性 | model=Qualcomm Technologies, Inc. xiaomi umi；compatible=qcom,kona-mtp / qcom,kona / qcom,mtp；board-id=<0x2b 0>、msm-id=<0x164 0x20001> |
| 交叉匹配 | 选中条目根 model/compatible/board-id 与同次 Android 运行树一致；Android 四项根属性与首次 MiSeed L3 保存的四项属性逐字节相同 |
| 结束检查 | adb unroot 后再次独立读到 uid=2000、sys.boot_completed=1、product=umi；采集过程中 boot ID 一致，未重启手机 |

本机镜像与先前 MIUI 29 条目参考不同：MIUI 参考 Umi 索引为 8，本机是 11，不再混用其索引或固件散列。本轮 raw DTBO、单条目、属性及长日志只留在忽略的 .work/root-dt-device-20261111/，完整散列见 evidence-hashes.json，不提交分区或原始镜像。

### 对首次 L3 与新隔离的离线重放

用前轮已经固定、编译和审查的 ABL LibUfdt host 程序，将本机实际选中条目 11 分别应用于首次 L3 的原始打包 DTB，以及新修复版 native-dsi-root-20261111/umi-csot.dtb：

1. 原始打包 DTB + 实际条目 11：根节点全部属性与首次 L3 最终 FDT 相同，整个 live /soc@0 子树逐路径逐属性完全相同（差异节点 0）。因此根身份覆盖、DPU 下游节点注入和 MMC 启用均已由当前实际选中条目重现，已经超出“参考包可能如此”的证据范围。
2. 新修复 DTB + 同一实际条目：validate_live 的五处隔离、完整 live soc、aliases 和 phandle 检查通过。根元数据仍按 ABL 行为复制，不写作 root 字节已恢复；未将主机重放的 RAM 占位值当真机 RAM 证据。
3. 本机实际根身份符合新 UBWC 诊断 gate 的全部约束；前轮真实取数函数的原版 -EINVAL / 修复版复用 sm8250_data 对照仍适用。此轮没有 RAM 启动修复镜像，不能把这些重放结果升级为原生显示 L3。

结果分别保存在 replay-old.dtb、replay-fixed.dtb、replay-summary.json。ABL 二进制本体仍未导出或锁定；当前结论是实际选中 DTBO + 固定参考算法完整重现了关注范围，不声称全 FDT 的 memory/chosen/固件动态写入都已复现。

🚦 本机 DTBO 只读采集、实际选择/根属性对照与普通 ADB 恢复为 L3；本机条目重放、新隔离为 L1；新 UBWC/原生显示仍只有此前 L1/L2。RISK-010 的“实际 DTBO 未取得”子项已关闭，剩余新镜像隔离/UBWC/点屏 L3、标准 UEFI 根交接与独立复核。仅采集器和测试有主机侧修改，本轮不改内核、镜像、GPIO、保护阈值或充电策略。后续 RAM 点屏需另有本轮明确启动/显示资源操作授权和回退方案。


## 修复隔离/UBWC 后的第二次原生 RAM L3（2026-11-11）

### 身份、授权、载荷与回退

用户本轮明确答复「授权 RAM 点屏验证」，批准指定 d7f01af4…d33849c1 镜像临时 RAM 启动、原生 DSI/1500 亮度验证、保存证据后返回 Android。只接电脑 USB 数据口，不接 PD/PPS 充电器；不刷写、不挂载存储、不改有线/无线充电配置，不发 charge-test、thermal 或寄存器控制。无线 RX/TX 未涉及。执行前材料/工具/离线回归均已齐备，未让手机等待现场构建。

| 项 | 本次证据 |
| --- | --- |
| 宿主 / 构建 | Windows 10.0.26200 x64、PowerShell 7.6.9、Python 3.14；WSL Ubuntu 24.04 x86_64、GCC 13.3.0 |
| 仓库 / 内核 | b73f19db3c7ce52209ea26a058f58f747f31157b 加当时未提交改动；Linux f45ed76d6791cf72ff1265ee93511175bbbc2f18（6.18.60） |
| 设备 / 原系统 | 唯一授权 umi/既有 CSOT；Android release=16、LineageOS 23.2-20261108-NIGHTLY-umi、boot_completed=1、uid=2000、DTBO 索引 11。Fastboot product=umi、unlocked=yes、is-userspace=no；真实序列号仅作定向核验，不存储 |
| RAM 载荷 | native-dsi-device-dtbo-20261111/boot.img，14,135,296 bytes；SHA-256 d7f01af45e66822790808203df0e5c46391c642547fc6f4c272cc920d33849c1 |
| 配置 / 输入锁 | resolved.config SHA-256 064d6dd908d67c83d2ded659cd6a2e0623748ecd3047a8bbc26369232f693978；build-source-lock.json SHA-256 70f6cb3f21bb5b483cf32f5ced87c81acd876e29e2f86a85d24ccd26f2d76284，启动前逐项输入核验通过 |
| 基准 DTB | 179,928 bytes；SHA-256 b254490cbaf3f46f4892e5754e752d2da4030a9c6e449b0187a4c416f482c6e8 |
| 回退 | rescue bootloader → 核验 Fastboot umi → fastboot reboot；若 USB/身份/额外 DT 变化异常立即停止并回退，不在现场修改驱动 |

实际顺序：对实际序列号定向 adb reboot bootloader；Fastboot 身份检查后 fastboot boot .work/native-dsi-device-dtbo-20261111/boot.img，退出 0（Sending 0.349s、Booting 0.101s）。Linux CDC 唯一端口 COM6、VID/PID=1d6b/0104，校验 MiSeed Umi/CSOT greeting 后执行：

```text
python -X utf8 tools/collect_display_evidence.py --port COM6 --reference .work/native-dsi-device-dtbo-20261111/umi-csot.dtb --output .work/native-dsi-root-l3-20261111/read-only
python -X utf8 tools/collect_display_evidence.py --port COM6 --reference .work/native-dsi-device-dtbo-20261111/umi-csot.dtb --output .work/native-dsi-root-l3-20261111/brightness-1500 --light-panel
python -X utf8 tools/collect_charge_log.py --port COM6 --output .work/native-dsi-root-l3-20261111/boot-charge.log
```

随后发送 rescue bootloader，Fastboot reboot 退出 0；独立 ADB 确认 umi、sys.boot_completed=1、uid=2000、release=16、LineageOS 版本不变。已告知用户可断开。保存日志 532 条，0 overwritten、0 storage errors；只读导出日志不作为新增充电功能验收。

### 实际结果与证据边界

- 用户本轮目视反馈黑屏。MDSS/DPU/DSI 分别绑定 msm-mdss/msm_dpu/msm_dsi；DRM msm 1.13.0 注册，DSI-1 connected/enabled/On、1080x2340，fbcon 分配 XR24 framebuffer，msmdrmfb 建立。这关闭“显示子系统未绑定”子项，没有证明面板已初始化。
- 日志明确出现 MiSeed ABL Umi root: using standard SM8250 UBWC data；受限身份 quirk 真机生效。最终 FDT 的完整 live soc、五处隔离、aliases/phandles 与 RAM 检查通过，无额外 DPU 节点、MMC 启用或 PMIC 注入。
- 严格 DT 审计仍 FAIL，仅 root compatible/model 两项变化；与已采集 DTBO #11 元数据相符。没有还原根字节，没有隐藏或豁免差异。标准 UEFI 根交接仍归 TD-006 / RISK-010。
- 首条 compression-mode 命令 type=0x07、data0=1、DMA len=4 返回 -110，CSOT prepare failed；此时 PPS 与 59 条面板 DCS 初始化尚未发送。
- 其前有 ARM SMMU context fault，SID=0x820/0xc20、cb=4、IOVA=0x9c79c500 至 0x9c80c800；随后 dsi_err_worker status=c。固定源码中 0xc 是 FIFO/MDP FIFO 下溢，错误 worker 会执行既有 dsi_sw_reset。故障地址落在 ABL framebuffer 0x9c000000 的已知区域，提示旧扫描交接干扰候选；日志没有 DSI TX IOVA/时间戳，尚不能建立与该次 DMA 的因果关系。
- 显式亮度 1500 的软件写入/读回成功，但 prepared=false 时回调没有发送 DCS，不能称为亮度已送达；更不能据此排除供电/reset。采集器的 PHY compatible 写成 7nm-8150，与实际 FDT 的 qcom,dsi-phy-7nm 不同，“PHY 缺失”提示是诊断错误，不能作为硬件未绑定证据。
- sys_fillrect/sys_imageblit 的 virtual-address 警告经源码确认只告警后继续执行，不是本次 compression-mode 超时的直接证据；refgen dummy regulator 和无 GPU 提示也未据此判成根因。

原始文件只在 .work/native-dsi-root-l3-20261111/，完整散列见 evidence-hashes.json。运行 FDT 551,520 bytes，SHA-256 4dbb5c49116461817b1783d8b771d8c53a7b588e55da97aa7972af5cac54e0ab；首次完整 dmesg 34,160 bytes、SHA-256 cfe429b8d573e90f57a03d78d11394facababa97aa416d1112b9ef0a3cb4f985；1500 后 dmesg SHA-256 6840e55f04e775ceb78ab8cf6e177ea159146ce0d668793e8b4c3f989c36a0f2；532 条日志 SHA-256 f0ccc66f3a33f63381b253d3929d1fd4e42d33e89a1d75f8e4d4608a19760941。Android 返回核验 JSON SHA-256 3d41dc3cbc5e5fc70515f3902956378ad648220443eaf11c81bff122fd2331f0。

🚦 RAM/USB、完整 live DT 隔离、UBWC 及 Android 回退为 L3 绿灯；原生面板点亮失败，严格根交接仍失败，不能标记原生显示验收完成。后续按用户建议对照固定 colorgarden 源与 6.12.94/6.18.60；修复/诊断准备统一见[研究续查](../research/2026-11-11-native-dsi-chip-boundaries.md#第二次黑屏后的静态兼容审查)。本次之后未再次启动手机，离线新载荷不继承这次 L3。


## 第三次原生 RAM L3：DSC、亮度命令与目视点屏通过（2026-11-11）

### 本轮授权、环境与载荷

用户明确请求「再次上机」，授权继续此前的原生 RAM 点屏流程。预先说明指定新镜像 9438724f…b9d4b380、先只读采集后 1500 亮度、保存后回 Android；不刷写、不挂载存储、不改有线/无线充电配置。手机仅连电脑 USB 数据口，无线 RX/TX 未涉及。材料在接入操作前核验就绪，未现场构建或修改产品源码。用户最初反馈「设备在安卓」发生于 RAM 启动之前，不当作 Linux 显示结果；实际运行完成后另行确认「出现文字或图像」。

| 项 | 本次输入与结果 |
| --- | --- |
| 主机 / 工具 | Windows 10.0.26200 x64、PowerShell 7.6.9、Python 3.14、pyserial 3.5、Google platform-tools 37.0.1 |
| 仓库 / 内核 | codex/native-dsi-dt-power，b73f19db3c7ce52209ea26a058f58f747f31157b 加未提交改动；Linux 6.18.60 / f45ed76d6791cf72ff1265ee93511175bbbc2f18，GCC 13.3.0 |
| 镜像 | .work/native-dsi-dma-diag-20261111/boot.img，14,135,296 bytes；SHA-256 9438724f260fbafaf00cc92d06896fbcb9e33224e0c1b2bce2004f10b9d4b380 |
| 配置 | resolved.config SHA-256 dc7ced5f2222b690a3c314c12734f1ca9be9af512f9bf7174d8e427baeda9c63；CSOT/DSI/DPU、PRINTK_TIME、LOG_BUF_SHIFT=20 核对通过 |
| 输入闭包核验 | build-source-lock.json SHA-256 42da9082e3237cf9902b7651bcbaa3fadf91cd7f60c772f28ce9672a7aeb9593，92 项 project inputs 及 host tools 逐项哈希一致；前轮 230 项离线测试与 L2 通过 |
| DTB | SHA-256 b254490cbaf3f46f4892e5754e752d2da4030a9c6e449b0187a4c416f482c6e8，与第二次 RAM 的板级 DTB 相同 |
| 设备 / 系统 | 唯一授权 Umi/既有 CSOT；Android 16、LineageOS 23.2-20261108-NIGHTLY-umi、dtbo_idx=11、boot_completed=1、uid=2000。真实序列号只在本次主机进程内定向及跨模式匹配，不归档 |
| 回退 / 停止条件 | 身份、额外 DT、传输/采集异常即停止；已知根元数据两项仍保留 FAIL，其他项必须通过才写亮度。finally 执行 rescue bootloader → 原序列号 Fastboot umi/unlocked/非 userspace → fastboot reboot → Android 启动完成/普通权限核验 |

### 实际操作与观察

主机编排 .work/native-dsi-dma-l3-runner-20261111.py 在本机运行（未入库），调用已验证的采集器与 SDK。adb -s <实际设备> reboot bootloader，核对 Fastboot product=umi/unlocked=yes/is-userspace=no 后，fastboot -s <实际设备> boot .work/native-dsi-dma-diag-20261111/boot.img；退出 0，Sending 0.351s、Booting 0.101s、总计 0.462s。无 flash、erase、format 或分区写入命令。

唯一 Linux COM6（VID/PID=1d6b/0104），greeting 身份核验通过。顺序调用 tools/collect_display_evidence.py --port COM6 --reference .work/native-dsi-dma-diag-20261111/umi-csot.dtb，分别保存到 .work/native-dsi-dma-l3-20261111/read-only 与 brightness-1500，第二次增加 --light-panel。采集器退出 1 的原因仅为严格根元数据差异，已独立验证没有新增问题；没有把退出码改为 0 或豁免根审计。随后 tools/collect_charge_log.py --port COM6 --output .work/native-dsi-dma-l3-20261111/boot-charge.log 退出 0，42 条、0 overwritten、0 storage errors；只读导出，不作为新增充电验证。

| 能力 / 时间 | 观察与等级 |
| --- | --- |
| UBWC / 显示绑定 | 1.986685s 命中受限 Umi 根 quirk；MDSS/DPU/DSI/PHY 分别绑定 msm-mdss/msm_dpu/msm_dsi/msm_dsi_phy。PHY compatible 修正后的真实驱动识别成功 |
| 原生初始化 | 2.550032s：CSOT prepared: DSC and 59 DCS commands complete；2.550915s：brightness=1 transfer_rc=0。没有本次 MiSeed DSI timeout/FIFO、dsi_err_worker、cmd dma tx failed 或 CSOT prepare failed |
| 原生模式与出字路径 | 2.586162s fbcon 切换、2.983790s msmdrmfb 建立；card1-DSI-1 connected/enabled/On、1080×2340，atomic state 显示 60Hz/235655kHz，XR24 fbcon framebuffer、pitch=4352。不是仅 simpledrm 节点存在 |
| 1500 亮度 | 7.985891s brightness=0 transfer_rc=0，7.997356s brightness=1500 transfer_rc=0；标准 sysfs brightness/actual_brightness 均 1500，max=2047。结合 prepared 与 DCS 回执，关闭此前“只有软件读回、未发命令”缺口；未测光学亮度标定 |
| 目视反馈 | 本轮运行后用户明确选择「出现文字或图像」；与原生初始化/模式及亮度命令证据共同构成本机 CSOT 点屏 L3 |
| 中断 / 实际时钟 | 1500 后快照 msm 中断 652、dsi_isr 263；clk_summary 的 dsi0_pll_bit_clk=498435718Hz、byte=62304464Hz、pixel=83072620Hz。不是把驱动 hs_rate=682MHz 字面赋值当实际时钟 |
| DT / RAM | 两轮 runtime FDT 相同，2413 nodes；完整 live soc、五处隔离、aliases/phandles 与 RAM 通过。root compatible/model 仍按 DTBO #11 覆写，两项以外无审计差异 |
| Android 回退 | rescue bootloader 成功，fastboot reboot 退出 0；独立 ADB 确认 product=umi、boot_completed=1、uid=2000、release=16、LineageOS 版本不变；已告知用户可断开 |

### 尚待闭合的交接问题

2.507464–2.507937s 仍记录 10 次 SMMU context fault：SID 0xc20/0x820、cb=4、IOVA 0x9c000100–0x9c0a0400，位于 ABL framebuffer 区间；本次其后面板初始化和 DMA 命令成功，未伴随前轮 FIFO 下溢/worker reset。它们不能单独被认定为必然导致点屏失败，也不能因本次成功就宣称旧扫描交接已经正确。

前轮首命令 DMA 超时没有在本次复现。DCS 包长误判修复已在本机后续 59 条初始化中通过；此次还增加时间戳/诊断与主机只读采集，不能把“首个超时根因已修”归于返回值修正，更不能排除时序相关性。RISK-010 继续追踪旧 framebuffer/SMMU 接管与前轮超时/FIFO 复位因果。sys_fillrect/sys_imageblit 仍有仅告警后继续绘制的提示，不影响本次已观测点屏。

根身份字节仍未还原，严格 FDT 总体结果 FAIL 保留；TD-006 的受限 UBWC quirk 仍为 ABL 诊断临时路径。UEFI 标准根交接、面板独立供电/PM、触控和 Debian 消费者尚未在本轮验收。不修改前轮黑屏/超时历史，不把新成功回填为旧镜像成功。

### 证据散列与验收

原始文件仅在忽略目录 .work/native-dsi-dma-l3-20261111/；完整清单 evidence-hashes.json。不提交原厂分区、镜像、序列号或长原始日志。

| 证据 | bytes / SHA-256 |
| --- | --- |
| runtime-fdt.dtb（两轮相同） | 551,520 / 34c0792c2e9f01ac2838214d62f68cc780806068843c6993fe31a18fb133f41a |
| read-only/dmesg.txt | 37,689 / 3e37943735809e6c28c9a56eeb225c65f3e47934c0177e0cacdaf8201b4470eb |
| brightness-1500/dmesg.txt | 38,747 / 0e5d037ad36c049f4b9876108a7d595f37d98eb6dd539504e8cd884f0dad4c53 |
| brightness-1500/display-after.txt | 78,645 / f7f1ad325f6a7e67e1ebd55bc12579ea0f82eff9366b8c0eefc5b69c3a44e68b |
| boot-charge.log | 10,569 / 61d0e7fefabe714371b5e9703fa9e56124748414858928482fff61ca7dabf92b |
| android-return.json | 126 / dc137f55883e84ccb83bd67ace731ff11b53aa968c2f96d6076abbc25f0013dd |

🚦 原生 DSC/59 命令、1080×2340@60 模式、1500 亮度命令与目视点屏、本机 DT 隔离/UBWC 和 Android 回退 L3 通过；标准根与 SMMU 交接仍开放。未改产品源码或构建物，本轮只更新事实记录与权威状态；文档按 L0 检查，不重复前轮 C/L2 测试。PR 98 文件红灯收尾、旧 8 项源码黄灯 TD-008 保持；独立专项复核与精确 HEAD CI 未执行，未提交/推送/合并。
