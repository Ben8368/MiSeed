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
- GENI 将 NACK 映射为 `-ENXIO`。仅幂等 CORE_RESET 的该错误最多重试 3 次、间隔约 1 ms；其余命令/错误不重放。短传输、I²C 错误或 RX 分配失败会禁用 IRQ 并锁存故障，不做电源循环。L2、reset/init、RF、close/reopen L3 通过；未从成功日志确认重试是否触发或各变更的独立作用。close 等待 CORE_RESET 成功及 NCI 2.x 通知，否则锁存超时。
- PM 仅允许已关闭且无故障时 suspend；活动/故障返回 EBUSY。不声明 RF 保持、唤醒或 eSE 可用；CLKREQ7 不声明为主机输出。
- 锁定 Linux NCI core / NFC UAPI 提供可选的 SE discover、enable/disable 与 APDU I/O 回调/命令；SN100T 公开评估资料描述 eSE 经片内 NFC 控制器 System Mailbox 通信，也有 SPI wired interface。当前 `sn100x` 只实现 NCI open/close/send，没有 eSE 回调；需以芯片行为、访问控制和生命周期证据另行实现、验证，不能从 API 存在推导 eSE 可用。
- 真机须分项验证注册、reset/init、帧收发、IRQ、失败/恢复、close/reopen 与 PM；协议掩码是待验证范围，固件/读卡不能由注册推导。按 QUALITY 取得 L2、schema 与授权 L3。

## 用户空间阶段

本次内核 NFC/NCI L3 不以 Debian 集成为门槛；Debian 用户空间属于后续阶段。当前静态诊断 initramfs 是 Linux 用户空间，但未集成 Debian；后续集成需处理服务、库、权限与应用。

后续用户空间阶段再固定消费 generic netlink/AF_NFC 的软件与依赖，验证发现、轮询、只读 NDEF/ISO-DEP；Debian 集成验证权限、服务生命周期和应用使用。不能假定任意 libnfc 都支持此控制器。HCE、eSE/OMAPI、支付能力另行评估，不能由 NCI 注册或读卡推导。

2026-11-10 Android AIDL HAL、`nfc_nci_nxp_snxxx.so`、`libsn100u_fw.so`、QTEE/eSE 记录仅为硬件/协议/固件依赖线索，不能直接作为 Debian 可用闭包。任何必需固件须另审来源、版本、哈希及分发条件；不为 NFC 引入 Android Binder/Bionic/VINTF 基座。

## 被动诊断

控制台 `nfc-status`（亦由 `status` 调用）按 `nxp,sn100t` 动态扫描 I²C/DT 元数据与 `/sys/class/nfc/nfcN` 注册信息，不依赖总线号，不发 ioctl/NCI/RF 命令。类目录计数包含全部控制器，不代表均属于 SN100T，更不能证明 IRQ、读卡或 eSE。

Linux/Debian 可运行 `python3 tools/nfc_inventory.py --timeout 3`：仅查询 generic-netlink 的 family 与 GET_DEVICE，输出设备索引、名称、协议与 powered；空列表合法，family 缺失、不支持、权限、超时、中断/畸形响应均报错。它不执行 DEV_UP、轮询或写卡，缺少 Python 的静态 initramfs 继续用上述被动诊断。此工具是最小标准消费者，非 Debian 服务集成证明。

## 有线、无线共存

- **有线 USB/PD/PPS**：独立于无线 RX/TX；USB 插线、PD/PPS 状态不能直接作为关闭 NFC RF 的条件。
- **无线接收 RX / 无线反充 TX**：当前未移植对应驱动；分别建模状态后才设计 NFC 共存策略。策略通过已验证的 Linux NFC RF 接口协调，不让无线驱动直接拉低共享 VEN。
- 只有证据确认后才可共享状态与策略；退出异常和恢复用户设置单独验收，不能据对话推断所有无线场景必须关闭 NFC。

## 证据与开放项

[内核 L2 / 旧驱动 probe L3](archive/hardware/2026-11-09-umi-nfc-l2-ramboot.md)、[新 SN100T NCI L3 尝试](archive/hardware/2026-11-13-umi-sn100t-nci-l3-attempt.md)、[Android 基线](archive/hardware/2026-11-10-umi-nfc-android-baseline.md)、[Linux 接口审计](archive/reviews/2026-11-10-linux-interface-audit.md)；当前设备能力见 [platforms](platforms.md)，未知能力归 RISK-011，旧 ABI 退出归 TD-003。
