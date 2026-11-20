# Umi Linux NFC / eSE 适配

WP10 通过 Linux NFC/NCI 接入 Debian，旧 NQ ABI 退出归 TD-003。接口规则见 [ARCHITECTURE](ARCHITECTURE.md#linux-接口准则)。

## 硬件与已有接口

- 锁定的 LineageOS NQ-NCI（GPL-2.0-only，来源哈希见来源锁）使用私有 ioctl/read/write，未注册 Linux NFC/NCI；`1-0028`/字符设备存在不等于适配完成。
- 本机节点 `soc/i2c@984000/nq@28`，地址 `0x28`；Linux I2C1 接 GPIO4/5，IRQ111、VEN83、FIRM110、CLKREQ7。Android 活动 DT 的 QUP1 为 1 MHz，Linux 用标准 `clock-frequency` 匹配。
- [NXP 安全目标](https://www.commoncriteriaportal.org/nfs/ccpfiles/files/epfiles/NSCIB-CC-2400058-02-ST_lite_v4_3.pdf)确认 SN100_SE/NFC 共用片内 PMU；公开资料未给本机 VEN 复位时序。旧 NQ 的 VEN 脉冲不是可照搬的安全规范；新驱动不拉低共享 VEN，也不假定有独立 eSE 电源 GPIO。

## Linux 内核 transport

`kernel/nfc/sn100x.c` 使用锁定 Linux 6.18.60 的 NCI core；标准控制经 generic netlink、数据经 AF_NFC。`CONFIG_NET/NFC/NFC_NCI/NFC_SN100X` 与 `nxp,sn100t` binding 配套；不导出字符设备或私有 ioctl，不同时绑定旧 NQ 驱动。来源、差异与验证见[适配记录](archive/research/2026-11-10-sn100t-linux-nci.md)；本地实现不等于已上游合入。

- 使用独立 transport：通用 `nxp-nci` close 会拉低共享 VEN，且有固件下载流程。新驱动沿用上游 I²C 帧、skb、gpiod、IRQ、PM；NCI core 处理 reset/init 与 RF。
- probe/open 将 FIRM 保持正常模式低，VEN 不拉低，IRQ 仅在 open 后启用；close/错误/移除不做硬复位或固件下载。2026-11-13 真机 reset/init、RF target discovery、close/reopen 通过；最终成功会话采用 1 MHz 与有界重试，未采 dmesg，不能判断重试是否触发，见[记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)。
- 配对的 GENI 补丁将 FIFO 写地址 NACK 与数据或阶段不明的错误区分。首帧 CORE_RESET 遇地址 NACK 最多发送三次，RF_DEACTIVATE idle 最多发送两次，间隔 1–1.1 ms。其他写失败、部分传输和尝试耗尽均锁存并保留 VEN。close 核对 CORE_RESET 成功和适用的 NCI 2.x 通知。[11-19 真机记录](archive/hardware/2026-11-19-umi-nfc-init-retry.md)
- CLKREQ7 配置 `output-disable`，不是 ready 信号。probe 设 FIRM 前采 FIRM/VEN，不是波形。仅关闭且无故障可 suspend；其余返回 EBUSY，不声明 RF 保持、唤醒或 eSE。
- `sn100x` 不挂 SE 回调。WP10 先证实 Umi 的 NFCEE 身份与 APDU 通路，再解决 Linux core 的自动探测、关闭前 `CORE_RESET` 与 APDU 返回隔离；共享 VEN 保持高不等于 eSE 会话不受影响。见[回调及共享 VEN 审查](archive/research/2026-11-18-sn100t-ese-ven.md)。
- 真机须分项验证注册、reset/init、帧收发、IRQ、失败/恢复、close/reopen 与 PM；协议掩码是待验证范围，固件/读卡不能由注册推导。按 QUALITY 取得 L2、schema 与授权 L3。

## 用户空间阶段

当前诊断 initramfs 未集成 Debian；内核 L3 与 Debian 集成分别验收。

救援 `nfc-ndef` 通过 AF_NFC 只读核对指定 Type 4 测试文本，L2 通过，待测标签。MIFARE 已验目标枚举，Classic 扇区认证与读块尚未接入。[标签规格](archive/research/2026-11-18-csot-nci-fdt-followup.md#type-4-与独立专项)。Debian 权限与生命周期另验。

Android HAL/QTEE 仅作线索；固件另审版本、哈希和分发条件。

## 被动诊断

控制台 `nfc-status`（亦由 `status` 调用）按 `nxp,sn100t` 动态扫描 I²C/DT 元数据与 `/sys/class/nfc/nfcN` 注册信息，不依赖总线号，不发 ioctl/NCI/RF 命令。类目录计数包含全部控制器，不代表均属于 SN100T，更不能证明 IRQ、读卡或 eSE。

Linux/Debian 的 `python3 tools/nfc_inventory.py --timeout 3` 仅查 generic-netlink family 与 GET_DEVICE；空列表合法，畸形响应/权限/超时报错。不执行 DEV_UP、轮询或写卡；initramfs 用 `nfc-status`。

Windows `collect_nfc_evidence.py` 默认只采注册与 dmesg；绑定、地址、DT 或类枚举不一致即停。主动报告分阶段，结果行须唯一且紧邻帧尾。KMSG 用 `nfc_geni_readback.py`，波形 CSV 用 `nfc_first_address.py` [离线判读](archive/research/2026-11-18-csot-nci-fdt-followup.md#首笔波形与采集模式)。

## 有线、无线共存

- **有线 USB/PD/PPS**：独立于无线 RX/TX；USB 插线、PD/PPS 状态不能直接作为关闭 NFC RF 的条件。
- **无线接收 RX**：RX1619 power-good 触发 Linux NCI 停止射频。RX 工作期间拒绝新轮询、选卡和 RF 传输，离座恢复受理新请求，原应用可重新寻卡。共享 VEN 保持，既有 eSE 验收独立。 [实现与验收范围](archive/research/2026-11-19-umi-rx1619-nfc.md)
- **无线反充 TX**：独立验证射频共存策略，当前待研。

## 证据与开放项

[内核 L2 / 旧驱动 probe L3](archive/hardware/2026-11-09-umi-nfc-l2-ramboot.md)、[11-19 真机记录](archive/hardware/2026-11-19-umi-nfc-init-retry.md)、[新 SN100T NCI L3 尝试](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)、[Android 基线](archive/hardware/2026-11-10-umi-nfc-android-baseline.md)、[Linux 接口审计](archive/reviews/2026-11-10-linux-interface-audit.md)；当前设备能力见 [platforms](platforms.md)，未知能力归 RISK-011，旧 ABI 退出归 TD-003。
