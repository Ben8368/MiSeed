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

本次没有对当前镜像发 `DEV_UP`、CORE_RESET、RF discovery 或读卡命令；只采集显示证据。复核后发现当前 `kernel/nfc/sn100x.c` 与成功 L3 提交 `05c4b7448de5686450c3b22bdd462958bd63476e` 完全一致（`git diff 05c4b74..当前HEAD -- kernel/nfc/sn100x.c` 为空）。因此 2026-11-13 记录中的 NCI reset/init、普通卡 target discovery、close/reopen 可复用为当前驱动版本的 NFC/NCI RF L3 证据；前文将其判为更早版而不适用是错误，现予更正。当前 L3 仍不包括 NDEF、PM、HCE 或 eSE。

当前驱动也没有 SE/eSE discover、enable/disable、APDU 回调；SN100T NFC 与 eSE 共用 PMU/VEN，已取得资料没有给出可批准的主机复位时序或 eSE 会话前置条件。因此本轮未操作共享 VEN、未做 eSE/APDU，也不能验收 eSE。继续前需先实现并审查 SE 接口、取得 NXP SN100T 主机接口时序及 Umi 板级电源/复位依据，并明确隔离 eSE 会话的安全步骤。NDEF 读卡还需要 NFC Forum Type 4 测试标签及只读用户态消费者；NDEF、PM、HCE 仍未验。

## 结论

- 显示配置/驱动/GPIO50高电平及维护者目视验收通过；DRM command-mode vblank/TE cadence实测约59.9Hz，不作为独立光学刷新率测量。物理自刷新精确频率未独立量化。
- NFC/NCI：当前驱动与成功 L3 提交 `05c4b74` 的 transport 源码一致；reset/init、普通 target discovery、close/reopen 通过记录可复用。NDEF、PM、HCE 与 eSE 不在该证据范围。
- eSE：当前未实现，且安全时序依据不足；未验收、未执行主动操作。
- 设备通过 Fastboot RAM 启动后已正常回 Android/ADB。没有持久分区写入。
