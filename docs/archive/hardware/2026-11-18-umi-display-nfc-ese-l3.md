# Umi 显示、NFC 与 eSE L3 跟进（2026-11-18）

## 范围与设备恢复

- 仓库/PR：MiSeed PR #7，分支 `codex/fixed-pd-sampling`，采集时 HEAD `1ed7395e0c993dbf4457151d41fd79a563ab053d`。
- 硬件内核/DTB源码基线：`61c495defa2ef3efda239f43d7b1535e83418187`。一次性 RAM 镜像 SHA-256：`06de17094b86c59766eaa5025f9c58a770aae2e0b7c5cfa7fbeffe75c0de86bc`；只增加只读 DRM vblank 探针，不写持久分区。
- ADB 唯一设备报告 `product:lineage_umi model:Mi_10 device:umi`。主机经 ADB 重启到 Bootloader，Fastboot 检查 `product=umi`、`unlocked=yes`、`is-userspace=no` 后执行一次 `fastboot boot`；返回 Fastboot 后执行 `fastboot reboot`。最终 ADB 恢复为唯一 Umi 设备，未写分区。
- USB CDC 为 `VID_1D6B&PID_0104` / COM6。采集结束后设备已回 Android，未运行 NFC RF 或 eSE/APDU 命令。

## 显示实测

`display` 读到 DSI-1 连接活动 CRTC 0，驱动含 `msm_mdss`、`msm_dpu`、`msm_dsi`、DSI PHY 与 `panel-xiaomi-umi-csot`；活动模式报告 `1080x2340`、`90 Hz`、像素时钟 `235788 kHz`、总时序 `1112×2356`。内核记录面板完成 DSC 与 59 条 DCS 命令、backlight transfer `rc=0`。

只读 `DRM_IOCTL_WAIT_VBLANK` 采集返回 `events=91`、`sequence_gaps=0`、`span_ms=1502.621`、`avg_hz=59.895`、`min_hz=57.019`、`max_hz=62.823`、`errno=0`、`rc=0`。因此活动 KMS 模式元数据标为 90Hz，但 DRM vblank 实测约 60Hz，两项证据相互矛盾；**本次 90Hz 实际刷新率 L3 不通过，需调查模式编程、面板实际时序及 MSM vblank 计数来源后再验**。vblank 计数连续且调用成功，不表示刷新率通过。此前不同启动记录的流畅目视观察只作为当时的定性证据，不能覆盖本次量测。

证据目录（仅本地忽略，不提交长原始日志）：`.work/pr7-display-nfc-l3-20261118/evidence/`。

| 文件 | SHA-256 |
| --- | --- |
| `display-before.txt` | `430e6de3d7df4e760772f0c0d3ab34cc10f4aeb2e924fc0ae0413dbc0e86d424` |
| `display-vblank.txt` | `6bf51bac738aa2817a9e1bb8eb7fb55402083be7ec68c3914fdace0ce58f33bf` |
| `dmesg.txt` | `ffb158d6b0977e6f1b464bdeb67030c979dae3515c3eb729ec793f0282d6164a` |
| `runtime-fdt.dtb` | `2a16a3558399440ef3f253e25ce283cd9bfce46b4f40fc9a759de30a7d48c003` |

运行时 FDT 比较器另报告根节点 `compatible/model` 与打包参考 DTB 不同：运行时根属性为 `qcom,kona-mtp / Qualcomm Technologies, Inc. xiaomi umi`，参考为 `xiaomi,umi / Xiaomi Mi 10 (CSOT, MiSeed L2)`。该差异使采集器总结果标为 `FAIL`；Fastboot 产品身份和已绑定的 CSOT 面板仍可读。该 FDT 身份差异与 90Hz/vblank 矛盾分别记录，不将其解释为刷新率失败原因，也不忽略采集器的整体 FAIL。

## NFC / SN100T

本次没有对当前镜像发 `DEV_UP`、CORE_RESET、RF discovery 或读卡命令；只采集显示证据。2026-11-13 的记录证明较早版驱动曾通过 NCI reset/init、普通卡 target discovery、close/reopen，但本 PR 当前 `kernel/nfc/sn100x.c` 与当时源码并不相同：当前版本在 probe 设置 FIRM 低、open 时读回并设置共享 VEN 高，并对 CORE_RESET `-ENXIO` 做最多三次有界重试。因此旧记录**不能替代当前 HEAD 的 NFC L3 验收**。当前 NFC RF/transport L3 仍开放。

当前驱动也没有 SE/eSE discover、enable/disable、APDU 回调；SN100T NFC 与 eSE 共用 PMU/VEN，已取得资料没有给出可批准的主机复位时序或 eSE 会话前置条件。因此本轮未操作共享 VEN、未做 eSE/APDU，也不能验收 eSE。继续前需先实现并审查 SE 接口、取得 NXP SN100T 主机接口时序及 Umi 板级电源/复位依据，并明确隔离 eSE 会话的安全步骤。NDEF 读卡还需要 NFC Forum Type 4 测试标签及只读用户态消费者；NDEF、PM、HCE 仍未验。

## 结论

- 显示活动模式配置与驱动绑定：部分通过；90Hz 实际刷新率专项因实测约 59.9Hz 与模式元数据矛盾而未通过。
- NFC：旧驱动版本存在一次成功的 NCI/普通 target L3，但当前 PR 驱动有相关行为改动，不能复用为当前 HEAD 通过证据。
- eSE：当前未实现，且安全时序依据不足；未验收、未执行主动操作。
- 设备通过 Fastboot RAM 启动后已正常回 Android/ADB。没有持久分区写入。
