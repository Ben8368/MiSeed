# 2026-11-13 Umi SN100T Linux NFC/NCI 真机 L3 尝试

## 环境与载荷

| 项 | 结果 |
| --- | --- |
| 日期 / 主机 | 2026-11-13；Windows 10.0.26200 x64 / PowerShell 7.6.9；构建与离线 L1 使用 WSL2 Ubuntu 24.04 x86_64 |
| 授权范围 | 用户请求 SN100T Linux NFC/NCI 真机 L3；只 RAM `fastboot boot`、标准 Linux NFC Generic Netlink `DEV_UP`；未刷写、未改分区、未操作 NFC/eSE 共享 VEN |
| 设备 / 原系统 | Xiaomi Mi 10 `umi`；Fastboot `product=umi`、`unlocked=yes`、`is-userspace=no`；序列号未留档；恢复后 Android `sys.boot_completed=1`，内核 `4.19.325-cip131-st15-perf-g71b13e62f057` |
| 仓库 / 输入 | MiSeed HEAD `7b57f5b3eccf44533ab3ebbb6bd874b6598a0444`，工作树含本轮未提交变更；Linux stable `f45ed76d6791cf72ff1265ee93511175bbbc2f18`（6.18.60）；AArch64 GCC 13.3.0 |
| 载荷 | `.work/nfc-l3-20261113/boot.img`，9,658,368 bytes，SHA-256 `033ecbcf84b7f96abdfb6bf3e8598698ea90a068695e259974618011b47534b8`；DTB SHA-256 `11f947134feec603629d4601dc078d548ef1cc6c19f10fff7a901305b82837ec`；source lock SHA-256 `614fcb1ef3c4a314c161e9c47c1eafb5be3ae4b27e7e697ba367313b19874bda` |
| 工具 | Android Platform-Tools 37.0.1；Windows pyserial 3.5；通过唯一 CDC `VID_1D6B&PID_0104` / `COM6` 操作 |

## 真机尝试 1

Fastboot 唯一设备身份与解锁状态核验通过。准确启动命令：

```powershell
fastboot boot .work/nfc-l3-20261113/boot.img
```

`Sending` 与 `Booting` 均返回 `OKAY`。USB CDC 控制台识别为 MiSeed 诊断环境。只读 `nfc-status` 确认 `1-0028 driver=sn100x`、DT compatible 匹配 1 个、`nfc0` 注册，协议掩码通过 Generic Netlink 读取为 `0x5e`。

持普通 NFC 卡执行 `nfc-l3`。标准 `NFC_CMD_DEV_UP` 在 NCI reset/init 前返回 `EIO (-5)`。内核 6.18.60 的 `nci_open_device()` 会将 transport `ops->open()` 的非零错误映射为 `-EIO`；若后续 NCI reset request 超时则是 `-ETIMEDOUT`。因此当前证据将故障限定在 transport open 的 GPIO/状态检查路径，无法再区分 FIRM 读取/状态、FIRM/VEN GPIO 设置或锁存状态。

按停止条件没有开始 RF 轮询，没有执行 GET_TARGET、APDU、NDEF、HCE、eSE 或固件操作；没有重试或电源循环。只读 `dmesg` 快照未见 SN100T/I²C 运行时错误细节，因为原驱动 open 错误路径没有阶段日志。串口会话摘要仅本地留于忽略目录 `.work/nfc-l3-20261113/nfc-l3-session.log`，未将含无关运行日志的原始快照加入仓库。

## 真机尝试 2：FIRM 正常模式修正

在诊断日志确认启动时 FIRM 为高后，按 binding 的正常模式定义与历史板级驱动线索，将标准 NCI open 调整为先设置 FIRM 低，再保持/设置共享 VEN 高；不发送固件下载命令，不将 VEN 拉低。更新版在 Linux v6.18.60 上重建，产物 `.work/nfc-firm-normal-20261113/boot.img`，9,658,368 bytes，SHA-256 `75a8510f1911de5cabad4ad4ff93eea32c7b40ae0b512fa1d9c6cffbc502940f`。

Fastboot 再次核对 `product=umi`、`unlocked=yes`、`is-userspace=no`，`fastboot boot` 返回 OKAY。`nfc-status` 确认 `1-0028 driver=sn100x` 与单个 `nfc0`。`nfc-l3` 的 `NFC_CMD_DEV_UP` 进入 NCI reset/init 等待后超时 `-ETIMEDOUT (-110)`；RF discovery 未启动。dmesg 有 `sn100x ... transport stopped: -6; VEN retained` 与 NCI completion timeout。当时尚无阶段日志，无法区分 I²C 写入或 IRQ 接收失败。

按回退流程经 CDC 请求 Bootloader，Fastboot 身份核验通过并 `fastboot reboot`。ADB 重新识别设备为 Umi，内核为 `4.19.325-cip131-st15-perf-g71b13e62f057`。随后按维护者要求从 ADB 切回 Bootloader；随后第三次复测并恢复 Android，详见尝试 3。此次未访问卡内容、未发 NDEF/APDU/HCE/eSE 操作，未写持久分区。原始串口快照仅保存在忽略目录 `.work/nfc-firm-normal-20261113/nfc-l3-session.log`。

## 真机尝试 3：FIRM 提前至 probe

第二次尝试的阶段日志显示 FIRM 直到 `DEV_UP` 才设低，且 open 前 GPIO 读回 FIRM/VEN 为 1/1。为让正常模式选择更早生效，改为 probe 获取 GPIO 后立即将 FIRM 设低，VEN 保持不动；同时补齐 CORE_RESET I²C 写入与 RX 帧头/负载分阶段日志。Linux 6.18.60 ARM64 L2 产物 `.work/nfc-firm-probe-20261113/boot.img`，9,658,368 bytes，SHA-256 `30d64ba326154371719be4d0efced319b142eec670f35ce1dad046cc76522103`。

唯一 Fastboot Umi 身份核验通过，RAM 启动返回 OKAY；probe 日志确认 FIRM 已在 probe 设低。`nfc-l3` 时 open 前读回 FIRM/VEN=0/1，设置后仍为 0/1；CORE_RESET 的 4 字节 I²C 写入直接返回 `-ENXIO (-6)`，dmesg 明确记录 `I2C NCI write failed (4 bytes): -6`。因此 NCI 命令未被 I²C 设备 ACK；不是 RX 帧头/负载解析失败，RF 没有开始。GPIO读回不是 NFCC 实际内部启动状态的证明。

此尝试仍未拉低共享 VEN，也未触碰 eSE、固件、卡数据或持久分区。经 Fastboot reboot 后 Android 恢复：产品 `umi`、`sys.boot_completed=1`、内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。当前 NFC L3 的下一步是取得可验证的 NFCC 供电/复位时序与共享 VEN/eSE 安全依据；依据缺失时不再做 VEN 脉冲。原始会话仅在忽略目录 `.work/nfc-firm-probe-20261113/nfc-l3-session.log`。

## 供电/复位时序依据复核

本节仅记录已有资料和源码中的可追溯事实；不构成新的 GPIO 操作授权或 eSE 安全时序批准。

| 证据 | 可确认内容 | 不能据此确认 |
| --- | --- | --- |
| 本机 Android 只读基线（2026-11-10，root 后只读采集） | 活动 DT 节点 `soc/i2c@984000/nq@28`，I²C 地址 `0x28`；GPIO IRQ=111、VEN=83、FIRM=110、CLKREQ=7。当前 L3 回到 Android 后 shell 可见 `umi`、内核 `4.19.325-cip131-st15-perf-g71b13e62f057`、NFC enabled。 | NFC enabled 不代表 RF 正在活动或 eSE 无会话；本轮 shell 权限不足以读取活动 DT，也没有查询 eSE 会话状态。Android kernel 的运行版本与锁定的历史 NQ 驱动不是同一份源码身份凭证。 |
| [NXP SN100 Series Security Target Lite Rev. 4.3](https://www.commoncriteriaportal.org/nfs/ccpfiles/files/epfiles/NSCIB-CC-2400058-02-ST_lite_v4_3.pdf)（2024-11-12） | SN100_SE 与 SN100_NFC 共用 SN100_PMU；eSE 运行依赖由 NFC 子系统软件控制的共享 PMU。 | 该安全目标不是 SN100T 主机接口数据手册，没有规定 VEN 脉宽、上电等待、复位保持时间或主机恢复步骤。 |
| [NXP SN100T Security Target Lite Rev. 1.0](https://www.commoncriteriaportal.org/files/epfiles/SecurityTarget-Lite_SN100T_v1.1_20190418.pdf)（2019） | 该评估对象是 SN100.C48 上的 JCOP5.1 eSE；文件描述 eSE 经 System Mailbox 连到片内 NFC 控制器，也描述 SPI wired interface / APDUCard Gate。由此可确认可继续评估 Linux 上的 NFC-controller-mediated eSE 通路。 | 评估范围不含 NFC 控制器，文档未出现 VEN 信号，也未给主机 VEN 上电、复位或 eSE 会话恢复时序；认证对象不能证明 Umi 主板级供电策略。 |
| 来源锁中的 [LineageOS Umi NQ 驱动](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/6fcac56423a078c7bd180e8d2c626c7d66bf1677/drivers/nfc/nq-nci.c#L447-L449)（revision `6fcac56423a078c7bd180e8d2c626c7d66bf1677`，`nq-nci.c` SHA-256 `fcd4e82f6ab56da558015d03b382cf322fd902493c60f11dfad2f101dd9daed4`） | 驱动注释明确 VEN 高时 NFCC/eSE 均供电、低时均掉电。正常 `NFC_POWER_ON` 先置 FIRM 低并等待 10–10.1ms，再置 VEN 高并等待 10–10.1ms；eSE power-on 若需拉高 VEN 则等待 1–1.1ms。`nfcc_hw_check` 仍会将 VEN 拉低约 10ms、再拉高约 10ms，之后发 CORE_RESET，等待约 60ms 读响应，再等约 30ms 读通知。 | 这些是软件实现延时，不是 NXP min/max 规格；旧 probe/reset 脉冲会中断 eSE 供电。当前 Linux open 中 VEN-high 后 10–15ms 等待与 LOS 上电等待同量级，但不能据此推导完整主机时序已安全或已足以解释 `-ENXIO`。 |
| [锁定的 Umi pinctrl DTS](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/6fcac56423a078c7bd180e8d2c626c7d66bf1677/arch/arm64/boot/dts/vendor/qcom/umi-pinctrl.dtsi) | GPIO83/GPIO110 配为 NFC active-state GPIO、2mA、上拉；匹配本机 Android runtime DT 的 VEN/FIRM 映射与已记录基线相符。 | pinctrl bias/drive 设置不表示 VEN/FIRM 的实时输出值、NFCC 内部电源状态或有效启动时序。不同旧 overlay 出现过不一致 VEN GPIO，不能用未匹配 overlay 替代活动 runtime DT。 |

本轮对 NXP 官方站点及公开评估材料的检索未找到 SN100T NFC 主机接口 VEN 时序数据表。安全目标将 SN100x_SE 产品数据表列为随产品提供的文档，但公开版本没有包含该主机时序；NXP 产品页也只有系列级介绍。不能用 PN7160/PN7220 等其他芯片的数据表填补此空缺。

**结论：**引用资料补足了 SN100T eSE 的系统邮箱/SPI 通信拓扑，并与仓库锁定的 Umi LOS 驱动共同支持“保留 VEN、正常模式 FIRM 低”的 Linux 设计方向。Linux NFC/NCI 主线还提供可选 SE discover/enable/disable/APDU 回调及 NFC generic-netlink SE 命令，但当前 `sn100x` 驱动只实现 open/close/send，没有实现这些 SE 回调。资料仍没有给出可批准执行的 SN100T 主机复位规范，也没有证明低 VEN 对 eSE 会话安全。Linux 继续维持 VEN，不做低脉冲/电源循环。要闭合时序依据，需要 NXP SN100T NFC 主机接口数据手册/应用指南或小米 Umi NFC 硬件设计/认证材料，并确认 eSE 并发会话前置条件。

## 2026-11-13 root 只读诊断与 CORE_RESET NACK 处理

维护者授权本会话 `adb root`。设备身份为 `umi`，Android 内核 `4.19.325-cip131-st15-perf-g71b13e62f057`；`adb root` 成功，root shell `uid=0`。只读活动 DT 与 sysfs 确认 NFC 节点为 `/soc/i2c@984000/nq@28`、兼容 `qcom,nq-nci`、Android adapter `i2c-0` 地址 `0x28`，运行时 GPIO 为 FIRM=110、VEN=83、IRQ=111、CLKREQ=7；NFC 服务 `mState=on` 且 `nfc.initialized=true`。没有读写 GPIO/I²C、切换 NFC 状态或访问 eSE。

本地锁定 Linux 6.18.60 的 `drivers/i2c/busses/i2c-qcom-geni.c` 将 GENI NACK 映射为 `-ENXIO`，错误注释为从设备未响应；因此已知失败位于 I²C 传输层，具体 NACK 字节阶段/物理电平仍未知。主线 SM8250 DTS 将 `i2c@984000` 对应 QUP1，GPIO4/5，default pinctrl `qup_i2c1_default`；Umi Linux L3 已注册 `1-0028` 并发送标准 4 字节 CORE_RESET。此前 Linux 启动日志显示该 controller 因未指定频率而默认 100 kHz。

设备重新连接后再次授权 `adb root`，root 状态已确认。活动 Android DT 的 `/soc/i2c@984000` 有 `qcom,clk-freq-out=0x000f4240`，即 1 MHz；`compatible=qcom,i2c-geni`、`status=okay`，NQ 节点仍绑定 `0-0028`。锁定的 Linux GENI 驱动读取标准 `clock-frequency`，支持表包含 1 MHz Fast-mode Plus，因此 `kernel/umi-csot.dts` 已添加 `clock-frequency = <1000000>`，匹配本机 Android 活动配置。Android 侧未发现导出的相关 GPIO，debugfs 未挂载，`/proc/interrupts` 的 GPIO111 计数为 80，当前 `dmesg` 未留下 NFC/QUP 错误；无法只读确认 VEN/CLKREQ 实际电平或总线波形。频率差异是新发现的配置偏差与优先修复项，尚未证明它就是 NACK 根因。

锁定的 Umi LOS NQ 源码 `nqx_standby_write()` 对 CORE_RESET 传输错误最多尝试 3 次，重试间隔 `usleep_range(1000, 1100)`，注释指出用于离开 standby。Linux `sn100x_send()` 仅对 CORE_RESET 的 `-ENXIO` 最多尝试 3 次；其他 NCI 命令不重放，也不拉低共享 VEN。随后新增的真机 L2/L3 结果见下节。

## 第四次真机验证：QUP1 频率匹配与待机恢复重试

**L2：**Windows 10.0.26200 x64 / PowerShell 7.6.9，WSL2 Ubuntu 24.04；Linux stable `f45ed76d6791cf72ff1265ee93511175bbbc2f18`（6.18.60），AArch64 GCC 13.3.0。`tools/build_kernel_l2.sh` 退出码 0，构建内含的 6 项 ABL isolation 回归通过。产物 `.work/nfc-nack-final-20261113/boot.img` SHA-256 `87dd5fd013be543b5ce7a66fe4dfde1e041ab38095aa5c5cc04d0c8849a86c31`；DTB SHA-256 `7ff57c3243b22b11d00c624d71e1c1abf9b2571724c7ed37cb4a6b4b82734c6a`；来源锁 SHA-256 `5a79459b95bf26e4deeda22dbf328f91f2da36cf077c53af9df3603cd34e9a8c`。最终 DTB 用 `fdtget -t u ... /soc@0/geniqup@9c0000/i2c@984000 clock-frequency` 读回 `1000000`。

**L3：**Fastboot 再确认 `product=umi`、`unlocked=yes`、`is-userspace=no`；仅 `fastboot boot` RAM 启动上述镜像，Sending/Booting 均 OKAY。唯一 USB CDC `VID_1D6B&PID_0104` 为 COM6。`nfc_l3_console.py` 只读身份确认后执行 `nfc-status`、标准 NFC Generic Netlink `nfc-l3`：`1-0028 driver=sn100x`、`nfc0`；`DEV_UP/reset-init` 通过，空协议请求按预期返回 `EINVAL`，12 秒 RF discovery 找到 1 个 target（protocol 4，未记录 UID），`DEV_DOWN/CORE_RESET`、clean reopen 与 final close 均通过。没有读 NDEF、发送 APDU、测试 HCE/eSE/固件或拉低共享 VEN。成功时主机脚本未采集 dmesg，因此不能判断 CORE_RESET 是首次写入成功还是触发了 standby 重试；频率匹配与重试对结果的单独贡献未隔离。组合变更下此前的 `-ENXIO` 未复现。

回退使用串口 `bootloader` 进入 Fastboot，核对 `umi`/已解锁后 `fastboot reboot` 成功；ADB 恢复，Android `sys.boot_completed=1`，原内核 `4.19.325-cip131-st15-perf-g71b13e62f057`。未写入持久分区。

## 回退与后续

串口请求 Bootloader 后，Fastboot 再次确认 `product=umi`、`unlocked=yes`、`is-userspace=no`；执行 `fastboot reboot` 成功。ADB 唯一设备恢复，`sys.boot_completed=1`，产品为 `umi`，原 Android 内核恢复。持久分区未写入。

本记录证明四次既有尝试与注册，并记录第四次在 QUP1 1MHz + 有界 CORE_RESET retry 组合下通过 reset/init、RF target discovery 和 close/reopen 的内核 NFC/NCI L3 范围。原始 NACK 根因未单独归因。普通卡仅用于 RF discovery，未读写卡数据；只读 NDEF、PM、HCE/eSE 仍无通过证据。Debian 用户空间集成属后续阶段，不是本次内核 L3 门槛。后续若再遇 NACK，采集控制器阶段/总线波形并停止，不以 VEN 电源循环试错。
