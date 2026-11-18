# Umi Linux NFC / eSE 适配

RQ-14 / WP10：目标是通过 Linux NFC/NCI 接入 Debian。RQ-13 的 Android NQ 字符设备方案仅保留历史 bring-up 证据，旧 ABI 退出验收归 TD-003。统一规则见 [ARCHITECTURE](ARCHITECTURE.md#linux-接口准则)。

## 硬件与已有接口

- 锁定的 LineageOS NQ-NCI（GPL-2.0-only，来源哈希见来源锁）使用私有 ioctl/read/write，未注册 Linux NFC/NCI；`1-0028`/字符设备存在不等于适配完成。
- 本机节点 `soc/i2c@984000/nq@28`，地址 `0x28`；Linux I2C1 接 GPIO4/5，IRQ111、VEN83、FIRM110、CLKREQ7。Android 活动 DT 的 QUP1 为 1 MHz，Linux 用标准 `clock-frequency` 匹配。
- [NXP 安全目标](https://www.commoncriteriaportal.org/nfs/ccpfiles/files/epfiles/NSCIB-CC-2400058-02-ST_lite_v4_3.pdf)确认 SN100_SE/NFC 共用片内 PMU；公开资料未给本机 VEN 复位时序。旧 NQ 的 VEN 脉冲不是可照搬的安全规范；新驱动不拉低共享 VEN，也不假定有独立 eSE 电源 GPIO。

## Linux 内核 transport

`kernel/nfc/sn100x.c` 使用锁定 Linux 6.18.60 的 NCI core；标准控制经 generic netlink、数据经 AF_NFC。`CONFIG_NET/NFC/NFC_NCI/NFC_SN100X` 与 `nxp,sn100t` binding 配套；不导出字符设备或私有 ioctl，不同时绑定旧 NQ 驱动。来源、差异与验证见[适配记录](archive/research/2026-11-10-sn100t-linux-nci.md)；本地实现不等于已上游合入。

- 使用独立 transport：通用 `nxp-nci` close 会拉低共享 VEN，且有固件下载流程。新驱动沿用上游 I²C 帧、skb、gpiod、IRQ、PM；NCI core 处理 reset/init 与 RF。
- probe/open 将 FIRM 保持正常模式低，VEN 不拉低，IRQ 仅在 open 后启用；close/错误/移除不做硬复位或固件下载。2026-11-13 真机 reset/init、RF target discovery、close/reopen 通过；QUP1 1 MHz 与 CORE_RESET 重试组合下 `-ENXIO` 未复现，单一根因未区分，见[记录](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)。
- 固定版本的 GENI 仍将 NACK 映射为 `-ENXIO`；诊断补充控制器阶段和 NCI opcode 日志。首次传输失败即禁用 IRQ 并锁存，不重发 CORE_RESET 或其他 NCI 帧，不做电源循环。close 核对 CORE_RESET 成功和适用的 NCI 2.x 通知，否则锁存超时。[本次 L3](archive/hardware/2026-11-18-umi-display-nfc-ese-l3.md#csot-90hz-命令与-geni-阶段取证复验) 确认首发 CORE_RESET 遇到地址阶段 NACK，尚未进入 RF。
- CLKREQ7 配置 `output-disable`，不是 ready 信号。probe 设 FIRM 前采 FIRM/VEN，不是波形。仅关闭且无故障可 suspend；其余返回 EBUSY，不声明 RF 保持、唤醒或 eSE。
- `sn100x` 不挂 SE 回调。WP10 先证实 Umi 的 NFCEE 身份与 APDU 通路，再解决 Linux core 的自动探测、关闭前 `CORE_RESET` 与 APDU 返回隔离；共享 VEN 保持高不等于 eSE 会话不受影响。见[回调及共享 VEN 审查](archive/research/2026-11-18-sn100t-ese-ven.md)。
- 真机须分项验证注册、reset/init、帧收发、IRQ、失败/恢复、close/reopen 与 PM；协议掩码是待验证范围，固件/读卡不能由注册推导。按 QUALITY 取得 L2、schema 与授权 L3。

## 用户空间阶段

当前诊断 initramfs 未集成 Debian；内核 L3 与 Debian 集成分别验收。

救援 `nfc-ndef` 通过 AF_NFC 只读核对指定 Type 4 测试文本，L2 通过、真卡未验。[标签规格与 PM/HCE/eSE 门槛](archive/research/2026-11-18-csot-nci-fdt-followup.md#type-4-与独立专项)。Debian 权限与生命周期另验。

Android HAL/QTEE 仅作线索；固件另审版本、哈希和分发条件。

## 被动诊断

控制台 `nfc-status`（亦由 `status` 调用）按 `nxp,sn100t` 动态扫描 I²C/DT 元数据与 `/sys/class/nfc/nfcN` 注册信息，不依赖总线号，不发 ioctl/NCI/RF 命令。类目录计数包含全部控制器，不代表均属于 SN100T，更不能证明 IRQ、读卡或 eSE。

Linux/Debian 的 `python3 tools/nfc_inventory.py --timeout 3` 仅查 generic-netlink family 与 GET_DEVICE；空列表合法，畸形响应/权限/超时报错。不执行 DEV_UP、轮询或写卡；initramfs 用 `nfc-status`。

Windows `collect_nfc_evidence.py` 默认只采注册和 dmesg；主动报告分列 reset/init、空协议、发现、读卡、关闭重开，缺证不算通过。首笔 CSV 用 `nfc_first_address.py` [离线判读](archive/research/2026-11-18-csot-nci-fdt-followup.md#首笔波形与采集模式)。

## 有线、无线共存

- **有线 USB/PD/PPS**：独立于无线 RX/TX；USB 插线、PD/PPS 状态不能直接作为关闭 NFC RF 的条件。
- **无线接收 RX / 无线反充 TX**：当前未移植对应驱动；分别建模状态后才设计 NFC 共存策略。策略通过已验证的 Linux NFC RF 接口协调，不让无线驱动直接拉低共享 VEN。
- 只有证据确认后才可共享状态与策略；退出异常和恢复用户设置单独验收，不能据对话推断所有无线场景必须关闭 NFC。

## 证据与开放项

[内核 L2 / 旧驱动 probe L3](archive/hardware/2026-11-09-umi-nfc-l2-ramboot.md)、[新 SN100T NCI L3 尝试](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)、[Android 基线](archive/hardware/2026-11-10-umi-nfc-android-baseline.md)、[Linux 接口审计](archive/reviews/2026-11-10-linux-interface-audit.md)；当前设备能力见 [platforms](platforms.md)，未知能力归 RISK-011，旧 ABI 退出归 TD-003。
