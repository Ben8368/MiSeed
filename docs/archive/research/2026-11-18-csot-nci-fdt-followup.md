# CSOT 90Hz、SN100T 与 FDT 验收跟进（2026-11-18）

基线 `e7f45e3` 加工作区诊断增量；Windows x64 / PowerShell 7.6，目标构建在 Ubuntu 24.04 WSL2 x86-64。仅 WP2 显示、WP10 NFC 和诊断采集；不改电源策略、原厂 DTBO/ABL 或真实分区。

## 依据与接口

用户共享对话给出 LOS timing@1 线索。直接读取固定 commit `71b13e62f057a649b77fe4062feb73ee72ad609c` 的 `dsi-panel-j2-mp-42-02-0b-dsc-cmd.dtsi`：60Hz 与 90Hz 水平时序一致；90Hz 使用 VFP/VSYNC/VBP `4/4/8`。两档均声明 682MHz，不能据此断言主线 PHY 实际链路时钟。MiSeed 旧 `B4/2F/26` 匹配 LOS 的 60Hz 表，现导入同面板 90Hz on-command 三项，保持选页/休眠退出位置、DSC、GPIO50 延时和单固定模式。来源许可/哈希见 [NOTICE](../../../kernel/display/NOTICE.md)。应用证据不等同读回扫描频率。

| 功能 | 固定 Linux 路径、接口与消费者 | 边界 |
| --- | --- | --- |
| 面板模式/时序 | DRM panel、MIPI DSI DCS、gpiod；既有 `xiaomi,umi-csot` binding；DRM/KMS 标准消费者 | 仅 CSOT，未添加私有 ABI 或动态切换；DCS 发送成功不是频率测量 |
| 运行时事件与帧 | 锁定 MSM DPU 的 `dpu_encoder_phys_cmd_te_rd_ptr_irq` 与 `dpu_enc_phys_cmd_pp_tx_done`；KMS、DRM vblank 和 tracefs | 回调 Hz、DPU 传输 FPS、光学扫描 Hz 分列；不用 debugfs/tracefs 作产品接口 |
| NFC | NFC/NCI、GENL `GET_DEVICE`/启停/轮询，AF_NFC ISO-DEP；I²C/IRQ/gpiod 与 `nxp,sn100t` binding | 不重放未知命令；读卡数据路径、PM、HCE、SE 分项验收。类注册与协议掩码不证明硬件已可用 |
| 启动交接 | 导出的 `/sys/firmware/fdt` 对打包 DTB；产品使用 EFI 配置表、EFI Stub | 严格根身份不绕过；报告子树与 RAM 的独立结果不为 FDT 整体验收放行 |

锁定 6.18.60 核对 `drm_mipi_dsi.h`、`dpu_encoder_phys_cmd.c`、`dpu_trace.h`、`net/nfc/nci/core.c`、`nci.h` 与 `i2c-qcom-geni.c`。NCI core 复用标准命令；GENI 的 `-ENXIO` 不足以推断 SN100T NACK 字节位置。暂不移植 NQ retry/私有 ioctl，也不把另款芯片 eSE 操作当作 SN100T 命令。后续若需 controller 通用诊断应由 I²C/GENI 层报告已知状态，不能用未证实的地址阶段假设扩大重试。

## 采样与判读

- 当前 KMS 配置：`modetest -M msm -c -e` 或 rescue `display`。内核 dmesg 的 `CSOT prepared` 记录配置、90Hz DCS 已发送及 GPIO 读回；`display-vblank` 记录指定 CRTC 的回调 cadence。
- 内容 FPS：在 Linux 上用 tracefs 单独实例采集 `events/dpu/dpu_enc_phys_cmd_pp_tx_done`，选择 `mono` 时钟；在已有变化画面负载的开始/结束向该实例 `trace_marker` 写入 `miseed_fps_begin`/`miseed_fps_end`，间隔 1–30 秒，关闭该实例 tracing 后读取 `trace`。用 `python3 tools/report_dpu_fps.py <trace.txt> --encoder <DRM对象ID> --pp <日志PP索引>` 输出 `dpu_tx_fps`；不将不同 encoder/PP 的完成数相加。零值只代表所选对象窗口内无匹配事件，先核对 ID 与负载。采集完禁用事件、移除该实例，不操作全局 tracer。
- 物理面板 Hz：测量已确认对应本面板的外部 TE 线或做独立光学采样；用可观察的刷新同步变化量化约 11.11ms，避免把 OLED PWM 或应用帧率当扫描。旧 `59.921Hz` 只作回调基线。当前没有可信的厂商寄存器读回定义，不新增任意 DCS 访问。
- NFC：先准备 GENI 阶段证据/波形和非支付测试标签，再以 `python tools/collect_nfc_evidence.py --port COMn --output .work/<新目录> --run-rf` 采集一次授权基础复验。核对唯一 SN100T 绑定和控制器；open 核验 FIRM/VEN；失败时记录 opcode 或消息类型，不记录载荷、UID；超时保留 `.partial`，完成帧后采 dmesg。比较 opcode、I²C 错误时点、IRQ 及波形。出现锁存停止，不拉低共享 VEN，不自动重跑。AF_NFC 只读 ISO-DEP/NDEF、PM、HCE/SE 另验。
- FDT：旧镜像重放 `root_identity=FAIL`，`live_hardware=PASS`，`ram=PASS`，`display_vblank=PASS`，`physical_panel_hz=NOT_MEASURED`。保留总体非零退出与完整根属性差异。生产修复由 WP3/WP4 按 TD-006 完成 EFI DT 交接并复验；不借由更改参考、白名单或 Linux 伪造根让此项变绿。

## 本地 L2 产物

`MISEED_NATIVE_DSI=1` 使用固定 Linux 6.18.60 与 AArch64 GCC 13.3.0 完成原生镜像，确认 CSOT、NFC/NCI、DPU TX tracepoint 已链接。输出仅在本地忽略目录 `.work/csot-nfc-mainline-l2-20261118/`。

| 产物 | SHA-256 |
| --- | --- |
| `boot.img` | `ee36e16f58432990beb398b2f4f2fb4ee05248270d49f8b04ab36a69609dfe23` |
| `umi-csot.dtb` | `8be099adb994ba2f7e44ebfa3c72568896a36b55f72474fe9789bf80647a0573` |
| `build-source-lock.json` | `a6b0e0520703668472a9578ba7b9f4742ab75b15cb77ad1d41191991af102bd8` |

未连接设备，未刷写。该镜像尚无 L3；真实刷新率、NFC 失败阶段与产品路径根身份仍未关闭。

## GENI 诊断增量

### Type 4 与独立专项

- ARM64 `MISEED_NATIVE_DSI=1 bash tools/build_kernel_l2.sh`（Linux 6.18.60，GCC 13.3.0）产物 SHA-256 `ee0ec6235888e4e3691920539b06d411f054844f40d723183ff162f999d4c38e`。新镜像未启动。`nfc-l3` 仍只做发现；采集器显式 `--run-rf --read-fixture` 才使用 `nfc-ndef`。同次 DEV_UP 后以 Linux AF_NFC/SOCK_SEQPACKET 读取唯一 ISO-DEP 测试目标，不访问 eSE 或记录 UID、APDU、内容。生产读卡函数已由本机 Linux seqpacket 模拟标签验证；这不是 RF 实测。
- 测试材料：预先准备非支付、非门禁 Type 4 v2.0 标签，15 字节 CC、一个标准文件控制 TLV、文件 E104 可自由读，单条 NFC Forum Text 记录，UTF-8、语言 `en`、文本 `MiSeed`（13 字节 NDEF）。探针只 SELECT、READ BINARY，无写入或回退命令；换卡、残缺响应、内容不符、协议歧义不通过。开始前确认同步波形，单次检测第九拍应答、DEV_UP、内容匹配、关闭重开并分项记录；失败即停。回退经救援控制台 bootloader、核身份、返回 Android。
- PM：先证明同镜像首发与标签读取；单独准备与 USB 断连无关的唤醒源、超时回退，再测试关闭态 suspend/resume 和唤醒后同标签复读。活动/故障状态的 EBUSY 是保护分支，不算成功休眠；不使能 RF wake。
- HCE：固定内核 `net/nfc/nci/core.c:nci_set_listen_parameters` 配置 NFC-DEP，不能把该路径或读卡器模式当作 ISO-DEP HCE。需证实到主机的 listen 路由、主机测试应用和独立外部读卡器的非支付测试往返；不借用 eSE 应用。
- eSE：`nci_discover_se`、`nci_enable_se`、`nci_disable_se`、`nci_se_io` 缺回调时可返回 0；空列表或单个返回码不通过。先取得匹配 Umi 变体/固件的 NFCEE 身份、HCI 管道或其他受支持传输、会话占用/释放及超时规则；确认不复位 VEN 的正常通路后才实现回调。若确需复位，再取得 NXP 主机指南、小米板级电源时序和 eSE 空闲确认方式并单独评审。验收以真实 SE 枚举、授权非支付测试 applet 一次响应、超时/取消只完成一次且不干扰其他会话为准。公开 [SN100T 安全目标 §1.2](https://www.commoncriteriaportal.org/files/epfiles/SecurityTarget-Lite_SN100T_v1.1_20190418.pdf) 描述片内 mailbox/HCI 和 SPI，未规定本板 VEN 时序。

### 首笔波形与采集模式

后续探针修正：固定 Linux 6.18.60 `net/nfc/core.c:nfc_start_poll` 对 initiator/target 协议掩码均为空返回 `-EINVAL`。只接受此负例；超时、权限错误不继续发起真实轮询，执行已有关闭清理。主机报告在完整帧中分别识别 reset/init、空协议、发现、测试文本匹配及关闭重开；已证实的阶段不受后续错误覆盖。重放前次 `DEV_UP=-110` 只判 reset/init 失败，无 RF 证据。上述为模拟/重放及目标 L2，未新增 L3；`MISEED_NATIVE_DSI=1 bash tools/build_kernel_l2.sh` 退出 0，新镜像 SHA-256 `2c77622af2e80c7b861dbe708ce5b8c373dbe4a4df18370689af156b25abd991`。

Windows `python tools/collect_nfc_evidence.py --port COMn --output .work/<新目录>` 只发 `nfc-status` 和 `dmesg`；成功仅指被动注册与日志采集。原主动验收须显式加 `--run-rf`：先确认板级测点、电平域、同步采集、明确授权和非支付测试标签；失败锁存停止，不自动重跑。未采波形时继续被动模式。

从逻辑分析仪导出同一时间基准的 CSV，截取目标探针之前的空闲至首笔地址第九拍下降沿。首行 `time_ns,scl,sda,firm,ven,irq`（可追加 `,clkreq`），时间为严格递增的整数纳秒，其余列为 0/1。`python tools/nfc_first_address.py capture.csv` 仅解码第一个 START，不跳过别的事务找匹配项，不记录载荷。输出首样本、START 与第九拍上升沿的控制线状态、7 位地址、方向与 ACK/NACK。`control_to_start` 和 `control_after_start_to_ninth` 分别统计（首样本，START] 和（START，第九拍上升沿] 内各控制线的采样翻转次数与最近翻转时间（纳秒，无翻转为 null）；未接入的 CLKREQ 不输出。两端电平相同不能排除期间有脉冲，首样本前状态未知。退出 0 表示已解码 `0x28/W`（含 NACK），1 是其他地址/方向，2 是无效或不完整。判读基于 [NXP I²C 规范 UM10204](https://www.nxp.com/docs/en/user-guide/UM10204.pdf) 的地址和应答位定义；数字采样不能证明模拟边沿质量、芯片内部模式或时间窗内无漏边。与同次 GENI 日志对齐后再归因。

参考 [Linux 上游提交 1275cbed80a7](https://github.com/torvalds/linux/commit/1275cbed80a740e4d56b6aff2745de9fce4818fa) 的 `drivers/i2c/busses/i2c-qcom-geni.c`（GPL-2.0；Praveen Talari、Naresh Maramaina；原始 GitHub patch SHA-256 `ce41de1af55eb9da33e88a14dc46ecdb51ffae08d10eb3bb6c34082b033c7beb`）。本地在锁定版本的 IRQ 清除前读取 `SE_GENI_M_GP_LENGTH`，0 记地址阶段，非零写记数据阶段，其余未知；日志不输出 payload。仅采用判定逻辑，不移植后续上游依赖、errno 或 DMA 恢复行为；保留 Linux I²C/NCI 原接口。删除 SN100T 的 CORE_RESET 重发，首次失败即锁存并保持 VEN。升级基线时复核并替换本地诊断增量。最终镜像的复验见[真机记录](../hardware/2026-11-18-umi-display-nfc-ese-l3.md#csot-90hz-命令与-geni-阶段取证复验)。

### 单次 DEV_UP 专项准备

- 救援 `nfc-first` 确认唯一 NFC Netlink 设备后至多发一次 `DEV_UP`，不发 `DEV_DOWN`（避免额外 CORE_RESET）、RF、APDU 或 reopen；成功后采日志、回 Android。首笔和 RF 探针不可在同次 RAM 启动混用。本轮不改 transport、DT 或有线/无线供电配置。
- 主机 `collect_nfc_evidence.py --first-address --waveform-armed` 先验 `nfc-status`，再采 `nfc-first`、`dmesg`。无完整结果不续发探针。开关表示操作者已确认，不是软件检测；`DEV_UP=0` 只说明初始化成功，第九拍以外部波形为准。首笔不需标签，RF/NDEF 另验。
- 真机前置：板级资料已确认测点、电平域和探头配置，分析仪在启动前就绪，以同一时基被动采 SCL/SDA、FIRM/VEN、IRQ。不外加驱动或试 VEN 脉冲。波形未就绪、身份不符或运行时硬件 DT/内存核验失败均停止；已知 FDT 根身份差异保留独立 FAIL。保留首笔前空闲至第九拍下降沿的样本和采集配置。
- `.work/sn100-first-only-20261118/run_first.py --check` 离线核对镜像、配置、DT、来源锁与主机工具哈希。分析仪就绪才运行 `--ram-boot-once --waveform-armed`，核验唯一 Umi、已解锁且非 userspace Fastboot，仅临时 `fastboot boot`。结束或采集失败后经救援控制台进 Bootloader，再 `fastboot reboot` 回 Android，核验 `umi` 与 `sys.boot_completed=1`；若控制台失联则停止命令并人工正常重启，不再启动镜像、不刷写。
- Windows x64 / PowerShell 7.6.9 + Ubuntu 24.04 WSL2 / GCC 13.3.0。全量回归 305 项中 4 项跳过，无失败；定向 C 测试覆盖成功、超时、地址错误、重复调用及 RF 互斥。ARM64 Linux 6.18.60 L2 和定向 schema（4 个 DT 反例，无跳过）通过。`boot.img` SHA-256 `db9ad029a2382be9de30f708a7ccff5ec4aa4dfaa79a41040676cfaf40cb3de8`；DTB `b4c82ce6324f497111a13d3dcd1b7ae9afe3d824fe7fa6516450a16663d8d9b5`，与前次被动 L3 一致。本镜像未启动，首笔 ACK/NACK 待 L3。

### 同一首笔的 GENI 时序对照

WP10 / RISK-011：对照 Android 运行内核版本后缀对应的 LineageOS `71b13e62f057a649b77fe4062feb73ee72ad609c` 与本机 Linux stable `f45ed76d6791cf72ff1265ee93511175bbbc2f18`。只用下游源码作硬件线索，不执行下游代码、不新增产品 ABI。

| 首发条件 | 对照与决定 |
| --- | --- |
| 引脚和时钟 | [Android 公共板级节点](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/arch/arm64/boot/dts/vendor/qcom/xiaomi-sm8250-common.dtsi) 的 `nq@28` 与已采活动节点的地址及四路 GPIO 相符；源码节点没有 `qcom,clk-src` 或 `ref_clk`。`NFC_POWER_ON` 中的显式时钟投票受属性控制。不能从驱动含 `clk_get` 推断本板漏开了一路时钟，也不能据此证明物理参考时钟有效。 |
| 启用顺序 | [相应 NQ 驱动](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/nfc/nq-nci.c) 与来源锁中 NQ C 文件 SHA-256 完全相同。区分 `nfcc_hw_check` 的 VEN 翻转与重试、普通 `nfc_write` 的写重试，以及 `NFC_POWER_ON` 的 FIRM 低 → 等待 → VEN 高 → 等待。MiSeed 在 probe 已设 FIRM 低，open 再确认 FIRM/VEN；没有证据把这些下游操作或延时移植为必需条件。 |
| 总线时序 | 同为 1 MHz，[Android GENI](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/i2c/busses/i2c-qcom-geni.c) 的 `(div,high,low,cycle)` 是 `(1,3,9,18)`；[固定 Linux GENI](https://github.com/gregkh/linux/blob/f45ed76d6791cf72ff1265ee93511175bbbc2f18/drivers/i2c/busses/i2c-qcom-geni.c) 的 19.2 MHz 表是 `(1,2,8,18)`，32 MHz 表是 `(2,3,5,15)`。这些是表项，不是两台系统上测得的 SCL 波形。既有 Linux 成功和失败镜像都不能仅凭标称频率归因。 |

单变量取证：保留原发送策略和板级 DT，仅在 GENI 已报 NACK 的 IRQ 分支同一日志中读 `clk_sel`、`m_clk_cfg`、`scl_counters`；不读 FIFO，不改变恢复。对比 `m_clk_cfg` / `scl_counters`：按源码编码，Linux 两表预期分别为 `0x11/0x202012`、`0x21/0x30140f`；Android 表对应 `0x11/0x302412`，不是 Android 实测值。选择器是索引，不是 Hz，寄存器也不是第九拍采样。

一次 `nfc-first`，不做 RF 或 reopen。若 `0x28/W` 第九拍线上为 ACK 而 GENI 报地址 NACK，优先复核采样对齐、SCL 高低时间及控制器配置；若线上也为 NACK，则对照已知正常 Android 波形和 VEN/FIRM/CLKREQ 交接，不能只凭 NACK 区分总线电气条件与 NFCC 未就绪。若该次成功，保存首笔 ACK 与 DEV_UP，不追加命令；无波形或采集不全均不做物理归因。不拉低共享 VEN、不调全局 GENI 时序表。

离线准备（无新增 L3）：Windows x64 / PowerShell 7.6.9，Ubuntu 24.04 WSL2 / AArch64 GCC 13.3.0。`MISEED_NATIVE_DSI=1 bash tools/build_kernel_l2.sh`、`tools/validate_kernel_dt.py --source … --build … --dtb <打包 DTB>`、WSL `python3 -m unittest discover -s tests` 退出 0（309 项、4 跳过；定向 DT 四项反例无跳过）。`.work/nfc-handoff-compare-20261118/l2/boot.img` SHA-256 `9b2cde1b13d28c2e08ed47f6af011c9808f26d2b3550423c879291ec8dd313eb`；从 boot 头定位的内核载荷 `73954acd9098caceda0e4b35b95a587199c103c4b30165e6dbb3ad7eacc6f429` 含新读回日志。DTB、resolved.config、rescue-init 哈希与双手机失败镜像相同。同目录 `run_first.py --check` 离线退出 0；真机前另核对同步测量条件与本轮授权。仅 RAM 启动，确认设备身份后执行一次 DEV_UP，保全日志后经 bootloader 重启，核对 Android 产品及启动完成。

#### 无分析仪：离线核对，不触发探针

[Android 活动 DT 只读记录](../hardware/2026-11-18-umi-display-nfc-ese-l3.md#android-活动-dtnfc-时钟与供电属性只读对照)将三个时钟列在 I²C 控制器，而非 `nq@28`。离线解析 11-13 成功镜像的打包 DTB、11-18 双手机失败会话的运行时 FDT，以及 GENI 读回镜像的打包 DTB：三份均在 `i2c@984000` 声明 1 MHz、`se`（GCC `WRAP0_S1_CLK`）；两个 AHB 时钟在父级 `geniqup@9c0000`，不是缺失。三路均解析到 `clock-controller@100000`，SCL/SDA 均为 GPIO4/5、`qup1`。应按各自 binding 的消费者和时钟名对照，不跨两棵树比较 phandle 数值。DT 声明不证明时钟已开启。

`python tools/nfc_geni_readback.py <dmesg.txt>` 只读完整 KMSG 导出，逐笔列出 `984000.i2c` 对 `0x28/W` 的地址 NACK，不按帧长猜 opcode。退出 0 表示至少一笔有寄存器读回，1 表示没有这种读回（包括旧日志），2 表示输入无效。匹配的是配置寄存器对，不是实测频率、物理第九拍或根因。11-18 双手机日志属于旧格式；缺字段不能用源码默认值填补。
