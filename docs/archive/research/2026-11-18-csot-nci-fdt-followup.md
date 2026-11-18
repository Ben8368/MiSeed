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

参考 [Linux 上游提交 1275cbed80a7](https://github.com/torvalds/linux/commit/1275cbed80a740e4d56b6aff2745de9fce4818fa) 的 `drivers/i2c/busses/i2c-qcom-geni.c`（GPL-2.0；Praveen Talari、Naresh Maramaina；原始 GitHub patch SHA-256 `ce41de1af55eb9da33e88a14dc46ecdb51ffae08d10eb3bb6c34082b033c7beb`）。本地在锁定版本的 IRQ 清除前读取 `SE_GENI_M_GP_LENGTH`，0 记地址阶段，非零写记数据阶段，其余未知；日志不输出 payload。仅采用判定逻辑，不移植后续上游依赖、errno 或 DMA 恢复行为；保留 Linux I²C/NCI 原接口。删除 SN100T 的 CORE_RESET 重发，首次失败即锁存并保持 VEN。升级基线时复核并替换本地诊断增量。最终镜像的复验见[真机记录](../hardware/2026-11-18-umi-display-nfc-ese-l3.md#csot-90hz-命令与-geni-阶段取证复验)。
