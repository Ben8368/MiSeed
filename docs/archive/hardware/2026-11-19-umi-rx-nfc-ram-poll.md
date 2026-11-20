# Umi RX/NFC RAM 真机验证

2026-11-19 19:20（上海时间），维护者授权一次临时 RAM 启动。目标为既有 Umi/CSOT，Windows 主机启动镜像 SHA-256 `c3ebf7ba8c63f6d8a7c11337b0754a3aaa38e1ee354660d898a7d77cc479e69b`。ADB 列表另有连接，执行前通过唯一物理 USB 传输和已知设备身份核对本机。

- Bootloader 接受 `fastboot boot`。救援控制台返回运行时 FDT，live hardware 和 RAM 检查通过。根 `model` / `compatible` 仍为 [RISK-010](../../RISK_REGISTER.md) 的已知差异。
- `RX_NFC_ARM_RESULT mode=poll rc=0 cleanup=0`，轮询探针就绪。USB 断开等待窗口为 45 秒，计时在主机发出拔线提示前已开始。到期时 USB 仍连接，维护者随后确认尚未拔线。
- 主机走正常重启回退，ADB 复核 Android 启动完成且设备为 `umi`。本轮在无线采样前结束，RX 上座、NFC 射频暂停／恢复、PMIC 选路及电池电流仍待验。

原始记录保存在 `.work/umi-rx-nfc-l3-20261119/attempt-poll/`。`report.json` SHA-256 `6c5082bdb8f655b84b2ae1e8d0ce89ca3531ce76de7ff9d42afea77e7721f046`。`arm.txt` SHA-256 `b220d29aa345d59984b384e999b685566728caad6139473654b91d317812e501`。

后续主持脚本先完成 FDT 和 NFC 身份核对，再等待维护者明确就绪，之后才发送探针命令并提示拔线。该顺序已用 Windows 离线反例核对，等待超时时只有身份采集命令。原镜像通过 `run.py --check`，本轮授权已用完。

## 第二轮拔线、上座与离座

沿用同一镜像和机型，单次授权 RAM 启动，维护者确认实际完成拔线、放上通电底座及移除。主机观察到 CDC 断开后重连。探针在启动后 `6.335 s` 成功 armed，`51.728 s` 返回 `stage=result mode=poll at=detach rc=-110 cleanup=0`。物理拔线期间，内核 UDC 的 `state` 一直保留 `configured`。该状态不能单独用于确认小米 10 的物理断线。收尾版须同时见 TCPM partner 缺席、近期 PM8150B TCPC CC 未连接且 VBUS 未检出；结果行后等至少一个诊断采样周期再导出。以下旧镜像不具备这组收尾证据，不重判为通过。

- 从原始串口文件恢复 401 条连续记录，零存储错误。38 组 RX 样本的 `online` 均为 `0`，输出遥测不可读。NCI 停射频、上座拒绝与离座恢复未进入验证阶段。
- 与上座操作吻合的区间，`27.758–42.265 s` 的 PMIC 读数出现 `USBIN_RT=0x18`、`POWER_PATH_STATUS=0x03`。USBIN/DCIN 选择位均未置位。虽然 `chg_enable` 在 `28.789 s` 读到 `1`，该窗口的 7 组电池样本仍为 `Discharging`，电流 `−0.449` 至 `−0.404 A`、28.0 °C。实际输入与接收器 power-good 原因待证，详见 [RISK-013](../../RISK_REGISTER.md)。
- Windows 采集器把终端提示符和 `CHARGE_LOG_BEGIN` 同一行的完整导出误判为失败，旧主持脚本随即走重启收尾。ADB 已确认 Android 正常恢复。保留原始串口 `charge.log.partial`（SHA-256 `a74411f3f3d54194201813f5e541eb9b48315494862a6e7d60b4719a3d0c21a4`），以及经帧和序号复核的 `charge-recovered.log`（SHA-256 `8f9ec67b847bbc14fabca62ed4134b861f0849645966dc27cf774b33ab43e022`），均在 `.work/umi-rx-nfc-l3-20261119/attempt-poll-retry-01/`。

主机侧已修复同一行提示符、完整帧与断线并发的处理。主持脚本等待 CDC 稳定，保留最多四次采集及其残帧，完整 RAM 日志和 KMSG 校验保存之后才自动重启。采集失败保留 RAM。已用本轮原始文件复现及校验，并用模拟覆盖先导出失败、日志完成前重试和证据不足推迟重启。Windows 专项 21 项通过，WSL 采集与 RX/NFC 专项共 30 项通过。
