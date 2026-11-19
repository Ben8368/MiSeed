# Umi 显示、NFC 与 eSE L3 跟进（2026-11-18）

## 范围与设备恢复

- 仓库/PR：MiSeed PR #7，分支 `codex/fixed-pd-sampling`。首次采集 HEAD `1ed7395e0c993dbf4457151d41fd79a563ab053d`；128ms 候选提交 `4406af7efaece2477acab82b2b1bc50923bb8593`。
- 首次镜像沿用硬件内核/DTB基线 `61c495defa2ef3efda239f43d7b1535e83418187`，只加入只读探针；候选复测固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 并改变面板延时。均为一次性 RAM 启动，不写持久分区。
- ADB 唯一设备报告 `product:lineage_umi model:Mi_10 device:umi`。主机经 ADB 重启到 Bootloader，Fastboot 检查 `product=umi`、`unlocked=yes`、`is-userspace=no` 后执行一次 `fastboot boot`；返回 Fastboot 后执行 `fastboot reboot`。最终 ADB 恢复为唯一 Umi 设备，未写分区。
- USB CDC 为 `VID_1D6B&PID_0104` / COM6。采集结束后设备已回 Android，未运行 NFC RF 或 eSE/APDU 命令。

## 显示实测

`display` 读到 DSI-1 连接活动 CRTC 0，驱动含 `msm_mdss`、`msm_dpu`、`msm_dsi`、DSI PHY 与 `panel-xiaomi-umi-csot`；活动模式报告 `1080x2340`、`90 Hz`、像素时钟 `235788 kHz`、总时序 `1112×2356`。内核记录面板完成 DSC 与 59 条 DCS 命令、backlight transfer `rc=0`。

只读 DRM_IOCTL_WAIT_VBLANK 首次采集平均59.895Hz；GPIO50拉高前等待改为来源注明的约128ms后复测仍为59.916Hz。第二次诊断快照确认 gpio50 : out high func0 8mA pull up；vblank为 events=91、sequence_gaps=0、span_ms=1501.641、avg_hz=59.934、min_hz=57.102、max_hz=62.387、errno=0、rc=0。延时调整没有改变DRM vblank cadence。

不能把该ioctl cadence单独等同于面板物理刷新率：固定Linux 6.18.60的Umi DSI panel未启用video mode，SM8250 DPU使用command encoder；dpu_encoder_phys_cmd_te_rd_ptr_irq()在read-pointer/TE路径调用vblank callback（[锁定源码](https://github.com/torvalds/linux/blob/f45ed76d6791cf72ff1265ee93511175bbbc2f18/drivers/gpu/drm/msm/disp/dpu1/dpu_encoder_phys_cmd.c#L114-L123)）。KMS仍报1080x2340@90Hz，GPIO50为高，维护者此前也已目视确认滚动流畅。因此该ioctl是command-mode vblank/TE cadence证据，不是面板自刷新光学测量；当前显示模式、rate-select GPIO和目视验收通过，物理90Hz精确值仍需独立光学测量才能量化，不能据59.9Hz单项判为面板实际只有60Hz。

诊断改进：`display-vblank` 现在将数值标为 `event_*_hz`，并注明这是DRM command-mode callback cadence，可能来自TE/read-pointer路径，不是光学面板扫描测量；避免将约59.9Hz日志误报为面板刷新率。该诊断仍不替代外部光学测量。

证据目录（仅本地忽略，不提交长原始日志）：`.work/pr7-display-nfc-l3-20261118/evidence/`、`.work/pr7-90hz-fix-l3-20261118/evidence/`及`.work/pr7-90hz-gpio-l3-20261118/evidence/`。

| 文件 | SHA-256 |
| --- | --- |
| `display-before.txt` | `430e6de3d7df4e760772f0c0d3ab34cc10f4aeb2e924fc0ae0413dbc0e86d424` |
| `display-vblank.txt` | `6bf51bac738aa2817a9e1bb8eb7fb55402083be7ec68c3914fdace0ce58f33bf` |
| `dmesg.txt` | `ffb158d6b0977e6f1b464bdeb67030c979dae3515c3eb729ec793f0282d6164a` |
| `runtime-fdt.dtb` | `2a16a3558399440ef3f253e25ce283cd9bfce46b4f40fc9a759de30a7d48c003` |

GPIO诊断文件哈希：`display-before.txt` `bd0640a5cf37722f78be0582501e98e4890fb4ac28c6d119a3904675f6693ec8`；`display-vblank.txt` `1f9b8a76588f19c43ab9fa2e35ef3b2811ce6e9a9315622586a171cacb96583c`；`dmesg.txt` `1eda94bad2e50d946ac8f89bfac3ccb775fd643afd027143cc0ecfad014b3505`；runtime DTB `43fedba2cca794337cdf54da343b0fb5547e016f72edff8723fe600833463cb8`。

运行时 FDT 比较器另报告根节点 `compatible/model` 与打包参考 DTB 不同：运行时根属性为 `qcom,kona-mtp / Qualcomm Technologies, Inc. xiaomi umi`，参考为 `xiaomi,umi / Xiaomi Mi 10 (CSOT, MiSeed L2)`。该差异使采集器总结果标为 `FAIL`；Fastboot 产品身份和已绑定的 CSOT 面板仍可读。该 FDT 身份差异与 90Hz/vblank 矛盾分别记录，不将其解释为刷新率失败原因，也不忽略采集器的整体 FAIL。

## NFC / SN100T

2026-11-18 首轮显示专测没有对镜像发 `DEV_UP`、CORE_RESET、RF discovery 或读卡命令。源码复核发现当时 `kernel/nfc/sn100x.c` 与成功 L3 提交 `05c4b7448de5686450c3b22bdd462958bd63476e` 完全一致；因此 2026-11-13 记录中的 reset/init、target discovery、close/reopen 曾可复用为该驱动版本的历史 L3 证据。前文将其判为更早版而不适用是错误，现予更正。但 2026-11-18 本记录后续的本镜像复验出现 NCI 写 NACK，当前版本的重复性尚未通过，历史成功不能覆盖本次失败。

2026-11-13 的完整记录保留了前几次 `-EIO`、`-ETIMEDOUT` 与 `-ENXIO` 失败，以及 QUP1 改为1MHz并加入有界 CORE_RESET 重试后的最终通过。最终组合下 reset/init、RF target、close/reopen 已通过，但成功日志未采集 dmesg，无法确认重试是否触发，也未隔离频率与重试各自的作用；所以记录为“最终 L3 通过、早期传输故障根因未单独归因”，不把早期失败隐藏，也不将其推成当前必现故障。

当前驱动也没有 SE/eSE discover、enable/disable、APDU 回调；SN100T NFC 与 eSE 共用 PMU/VEN，已取得资料没有给出可批准的主机复位时序或 eSE 会话前置条件。因此本轮未操作共享 VEN、未做 eSE/APDU，也不能验收 eSE。继续前需先实现并审查 SE 接口、取得 NXP SN100T 主机接口时序及 Umi 板级电源/复位依据，并明确隔离 eSE 会话的安全步骤。NDEF 读卡还需要 NFC Forum Type 4 测试标签及只读用户态消费者；NDEF、PM、HCE 仍未验。

诊断改进：`nfc-l3` 明确输出该探针只覆盖 NCI reset/init、RF target discovery 与 clean reopen；NDEF、PM、HCE、eSE 均不在其测试范围，避免把基础 transport L3 误读成完整 NFC/eSE 验收。

## 2026-11-18 更新诊断镜像复验

- 镜像为一次性 `fastboot boot` RAM 启动：boot.img SHA-256 `10a844576eb16aa66e97bab9f5ac75dcd25739ae2107908664274452ef507591`；Linux 6.18.60 内核 `7a29245df56400fd2c3938632a0637bc27dffd56a4b31bef80f27769d0dbc17e`；DTB `8be099adb994ba2f7e44ebfa3c72568896a36b55f72474fe9789bf80647a0573`；更新后的 ARM64 rescue 用户态 SHA-256 `38ed8360daa2b7870c8298598b756de82654f8388375841ecd86b2e304dbfab0`。
- 设备 `umi` / `ec04f57f`，CDC COM6。显示重采集：KMS `1080x2340@90Hz`，GPIO50 为 `out high func0 8mA pull up`；91 个事件无序号跳跃，`event_avg_hz=59.921`、min `58.819`、max `60.845`、span `1501.982ms`。探针按新格式标明这是 DRM command-mode callback cadence，不是光学面板扫描。维护者确认滚动仍然流畅。显示模式/GPIO/目视的本机 L3 范围通过；光学精确 Hz 未量化。采集器总状态 `FAIL` 是 runtime FDT 根 `compatible/model` 与参考 DTB 不同，见 `evidence/runtime-fdt.txt`。
- NFC 被动身份确认仍为 `1-0028 / sn100x / nfc0`。主动探针运行超过其主机脚本 36 秒提示符界限；主机脚本未保存部分 NCI 输出。之后只读 dmesg 捕获到：open 前 `FIRM=0 VEN=1`，VEN 保持高；首次 CORE_RESET 在两次 standby retry 后写入被接受；约 12 秒 RF 测试窗口后出现 `I2C NCI write failed (4 bytes, 1 attempts): -6`，随后 `transport stopped: -6; VEN retained`，NCI request completion 超时且因故障锁存拒绝 reopen。故本次 reset/init/RF/close-reopen 整体验收为未通过；日志无法证明发现了 target，也无法从当前传输日志确定失败帧 opcode、具体 NACK 字节阶段或总线波形。禁止据 4 字节长度猜成 CORE_RESET，亦不重复发 NFC 命令。
- 本次 `-ENXIO` 已重新出现；在取得 GENI/I²C 事务阶段或外部总线波形前停止进一步 NFC L3 重试，不拉低共享 VEN、不进行电源循环。NDEF、PM、HCE/eSE 未测试，eSE 仍无驱动回调。
- 主机经救援控制台请求 bootloader 后以 `fastboot reboot` 返回 Android；ADB `sys.boot_completed=1`、产品 `umi`、Android NFC `mState=on`。USB/AC powered 为 false，电池 79%、31°C。未写入任何持久分区。

## 结论

- 显示配置/驱动/GPIO50高电平及维护者目视验收通过；DRM command-mode vblank/TE cadence实测约59.9Hz，不作为独立光学刷新率测量。物理自刷新精确频率未独立量化。
- NFC/NCI：11-13 有一组历史 reset/init、target、close/reopen 成功记录；11-18 当前镜像复验遇到 4 字节 NCI 写 `-ENXIO`、故障锁存与 reopen 失败，当前 L3 重复性未通过。NACK 阶段/波形未明，停止重试并禁用 VEN 复位；本次无可保存 target 数。NDEF、PM、HCE 与 eSE 未验。
- eSE：当前未实现，且安全时序依据不足；未验收、未执行主动操作。
- 设备通过 Fastboot RAM 启动后已正常回 Android/ADB。没有持久分区写入。

## CSOT 90Hz 命令与 GENI 阶段取证复验

- 新镜像仅执行一次授权 Fastboot RAM 启动，boot.img SHA-256 `5bdb40418432a4b5dcf41cb4b1feaaebe34379965d451e166e87f644616edd91`，Linux Image `520a179344af2375358cbf9ed81db9cd1694d7931994f2813aeaa241a270277d`，构建锁 `aca0530a425faeb51f36a0e81cadea3679d64980eb267d1d6e530f49e09736a3`，DTB 与前述同哈希。原生 ARM64 L2、定向 DT/schema 与四项反例通过；WSL 回归运行 299 项、4 项跳过。原始证据仅留本地 `.work/csot-geni-final-20261118/`。
- 显示：KMS active mode `1080x2340@90`，像素时钟 235788 kHz，GPIO50 高；内核记录 CSOT 90Hz DCS 发送成功。DRM command-mode vblank 在 1001.714 ms 内采到 91 个事件、0 跳号、均值 **89.846Hz**（前一镜像 59.921Hz）。配置与 90Hz 级回调通过；本次未采集内容 FPS 或独立 TE/光学扫描，不能用回调代替物理扫描频率。
- NFC：被动枚举 `1-0028 / sn100x / nfc0` 通过；唯一主动探针在 `DEV_UP` 的首发 `CORE_RESET` 失败。GPIO 读回 `FIRM=0 VEN=1`，随后 `984000.i2c` 日志 `bus=1 addr=0x28 dir=write phase=address gp_length=0 irq=0x409`；SN100T 记录 `opcode=0x0000 len=4 ret=-6`，锁存且保留 VEN。上层 `-110` 是等待响应超时；没有重发、进入 RF 阶段或执行 reopen。地址阶段未应答不等于芯片收到并拒绝 opcode，也不能倒推上一镜像的失败阶段。下步查首发时 NFCC 就绪条件和 SCL/SDA 波形，不做盲重试或 VEN 复位。NDEF、PM、HCE、eSE 未验。
- 采集：两套传输帧及日志均完整。FDT 单独仍 `root_identity=FAIL`：打包 `xiaomi,umi / qcom,sm8250`，运行时 `qcom,kona-mtp / qcom,kona / qcom,mtp`，`model` 也变化；`live_hardware`、`ram`、`display_vblank` 均 PASS。按 TD-006 跟进 EFI DT 交接，不降低检查要求。
- 从救援控制台返回 Bootloader 后执行一次 `fastboot reboot`，Android ADB 恢复，`sys.boot_completed=1` 且产品仍为 `umi`。未刷写持久分区。

## CLKREQ 输入配置与 probe 交接采样（被动 L3）

- 工作树基线 `fe9c0ba`。锁定 NQ 驱动在 probe 对 CLKREQ 调用 `gpio_direction_input`；旧打包 DTB 的 GPIO7 组仅有 mux、drive-strength 和 bias。新 DT 增加 TLMM 支持的 `output-disable`，不更改 SCL 频率、VEN、FIRM 或发送策略。probe 在默认 pinctrl 后、设 FIRM 前采两路 GPIO；未将读值作为 ready 门槛。
- Windows x64 / PowerShell 7.6.9 + Ubuntu 24.04 WSL2。`tools/test_host.ps1` 退出 0，Windows/WSL 各 299 项、分别跳过 74/4 项。`MISEED_NATIVE_DSI=1 bash tools/build_kernel_l2.sh`、`tools/validate_kernel_dt.py --source … --build … --dtb <打包 DTB>` 均退出 0，定向 schema 的四项反例无跳过；本地以旧/新 DTB 对 GPIO7 配置作负/正对照。Linux 6.18.60、AArch64 GCC 13.3.0。boot.img SHA-256 `debca46e825c6d8f8586b7039c93bdf32534b3cc0521a80536737d1dc4625a68`，DTB `b4c82ce6324f497111a13d3dcd1b7ae9afe3d824fe7fa6516450a16663d8d9b5`。
- 核验唯一 Umi、已解锁、非 userspace Fastboot，仅 RAM 启动一次。`nfc-status` 确认 `1-0028 / sn100x / nfc0`；运行时 NFC 默认 pinctrl 指向含 `gpio7 / output-disable` 的配置。dmesg 在 2.044963 s 采得 FIRM/VEN=1/1，2.055267 s 驱动报告已设 FIRM 低且未改 VEN。这不是复位沿或 NFCC 内部模式的测量，不能证明下载模式或 NACK 根因。未就绪外部同步波形，本轮未发 `DEV_UP`、CORE_RESET、RF 或 eSE 命令，主动通信未验。
- FDT：`live_hardware`、`ram` 通过，`root_identity` 仍因 `compatible`、`model` 失败。采集及回退记录在忽略目录 `.work/sn100-clkreq-20261118/`。正常重启回 Android，产品 `umi`、ADB `sys.boot_completed=1`；未刷写。

## Type 4 探针镜像：被动 L3 复验

- `boot.img` SHA-256 `2c77622af2e80c7b861dbe708ce5b8c373dbe4a4df18370689af156b25abd991`，随包来源锁与仓库一致，DTB 与上一节相同。核对唯一 Umi 和 bootloader 身份后执行一次 `fastboot boot`；内核为 ARM64 Linux 6.18.60。
- `nfc-status` 确认 `1-0028 / sn100x / nfc0`；运行时 GPIO7 默认组含 `output-disable`。内核在 2.046627 s 采得 FIRM/VEN=1/1，2.056592 s 报告 FIRM 已设低、VEN 未动。这是软件采样，不是 NFCC 就绪或首发应答证据。
- 控制台列出 `nfc-ndef`，未执行。未确认同步波形与测试标签就绪，故未发 `CORE_RESET`，首发 ACK/NACK 未测；空协议负例、RF、Type 4 NDEF 读取、关闭重开、休眠恢复、HCE、eSE 均未验。前次地址 NACK 不是本镜像的测量结果。
- NFC 状态、运行时 DTB、dmesg 三帧采集完整；FDT `live_hardware`、`ram` 通过，`root_identity` 因 `compatible`、`model` 不符仍为 `FAIL`。已恢复 Android，ADB 确认 `umi` 且 `sys.boot_completed=1`，未刷写。原始证据与回退记录在 `.work/sn100-probe-stop-20261118/`。

## 双手机 MIFARE 发现：初始化前置复验

- Android 对照由用户提供：Umi 上 NFC Tools 发现另一部手机卡包的空白卡，显示 ISO 14443-3A / MIFARE Classic 1K。不收录截图或卡序列号。项目侧仅验初始化与发现，无外部分析仪，不测 NDEF、扇区或 eSE。
- `fe9c0ba` 加本地变更，Windows x64 / PowerShell 7.6.9、Ubuntu 24.04 WSL2、ARM64 GCC 13.3.0。新增救援 `nfc-discover`，一次 DEV_UP，成功后才用 MIFARE 掩码轮询，结束清理但不重开。`MISEED_NATIVE_DSI=1 bash tools/build_kernel_l2.sh`、WSL `python3 -m unittest discover -s tests`（305 项、4 跳过）、离线哈希预检均退出 0。boot SHA-256 `d7c023f7086e95877aee1356811a89646695285076b1c355f624d673586b06e4`，DTB 与上节相同，未改 NFC transport 或供电配置。
- 用户授权一次临时启动，已确认唯一 Umi、已解锁、非 userspace Fastboot。`1-0028 / sn100x / nfc0` 注册通过。7.050637 s open 回读 FIRM=0、VEN=1；7.071015 s GENI 报 `0x28/W phase=address gp_length=0 irq=0x409`；CORE_RESET 写入 `ret=-6`，首错锁存、保留 VEN。`stage=dev_up up=0 polling=0 rc=-110`。**初始化失败，未执行目标发现**，不是轮询后无卡。GPIO7 已修正的镜像仍复现同类首发失败；第九拍和芯片内部就绪状态未测。不重试、不写卡。
- 完整记录在 `.work/umi-nfc-two-phone-20261118/evidence/`，运行时 FDT 硬件子树与 RAM 通过，既知根 compatible/model 差异仍 FAIL。`run.py --ram-boot-once` 按探针失败退出 1，回退成功：Android `umi`、ADB 在线、`sys.boot_completed=1`；未刷写。

## Android 活动 DT：NFC 时钟与供电属性只读对照

- 已授权临时 `adb root`；唯一设备 `umi`、Android 启动完成。普通 shell 先前因权限不足未能判断。本次确认 UID 0，成功列举活动 DT 的 `/soc/i2c@984000` 和 `nq@28`，校验子节点兼容 `qcom,nq-nci`、地址 `0x28` 后读取指定属性。原始记录 `.work/nfc-handoff-compare-20261118/android-live-clock-dt.txt`，SHA-256 `ac4f0de8a5a7a2dc151cd590573329e4abaa85514095f55be281263ba453eb97`。
- 父控制器 `qcom,clk-freq-out=1000000`，`clock-names` 为 `se-clk`、`m-ahb`、`s-ahb`；这些是控制器时钟，不是 NFC 芯片的参考时钟。子节点有 GPIO7 CLKREQ、`nfc_active`/`nfc_suspend`，但未列出 `qcom,clk-src`、`clocks`、`clock-names`，也未列出 `qcom,nq-vdd-1p8-` 的 supply/voltage/current 三项。这回答的是运行时 DT 有无显式声明，不证明物理参考时钟、供电或首笔应答状态。无依据照搬一个芯片时钟或电源节点。
- 未发 NFC 命令，未读写总线或 GPIO，未切换 NFC、重启或刷写。`adb unroot` 后确认 UID 2000、`product=umi`、`sys.boot_completed=1`。
