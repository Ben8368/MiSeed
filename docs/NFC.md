# Umi Linux NFC / eSE 适配

RQ-14 / WP10：目标是通过 Linux NFC/NCI 接入 Debian。RQ-13 的 Android NQ 字符设备方案仅保留历史 bring-up 证据，旧 ABI 退出验收归 TD-003。统一规则见 [ARCHITECTURE](ARCHITECTURE.md#linux-接口准则)。

## 硬件与已有接口

- 原 NQ-NCI 来自 LineageOS `android_kernel_xiaomi_sm8250` revision `6fcac56423a078c7bd180e8d2c626c7d66bf1677`，GPL-2.0-only，归属及输入哈希在来源锁中；迁移时继续保留。
- 实机节点 `soc/i2c@984000/nq@28`，地址 `0x28`；Android 总线号为 0，MiSeed 固定主线树为 I2C1，不能硬编码跨系统总线号。GPIO4/5 为 I²C，IRQ111、VEN83、FIRM110、CLKREQ7 来自匹配本机接线。
- 既有 L3 确认 `1-0028` 绑定 `nq-nci` 与 `/dev/nq-nci` 存在。该驱动使用私有 ioctl/read/write，未注册 Linux NFC/NCI；probe/node 不是完整内核适配完成的判据。
- NFCC 与 eSE 共用 VEN；旧 NQ 驱动 probe 会切换 VEN 并发送 reset/version，open/close 也有副作用。新 transport 不复用这些入口；不得虚构独立 eSE 电源 GPIO。

## Linux 内核 transport

`kernel/nfc/sn100x.c` 使用锁定 Linux 6.18.60 的 NCI core；标准控制经 generic netlink、数据经 AF_NFC。`CONFIG_NET/NFC/NFC_NCI/NFC_SN100X` 与 `nxp,sn100t` binding 配套；不导出字符设备或私有 ioctl，不同时绑定旧 NQ 驱动。来源、差异与验证见[适配记录](archive/research/2026-11-10-sn100t-linux-nci.md)；本地实现不等于已上游合入。

- 独立 transport 的理由：固定 `nxp-nci` 通用驱动 close 会拉低 VEN，且带独立固件下载流程；SN100T 的共享 VEN 不适用。沿用上游 I²C 三字节头/长度帧模型与内核 skb、gpiod、IRQ、PM 接口，NCI core 负责 reset/init 与 RF 协议。
- probe 仅获取 VEN/FIRM，IRQ 注册后保持禁用；open 拒绝继承的 FIRM 高电平，设置正常模式并使能 VEN/IRQ。close、错误、移除均不拉低共享 VEN；不执行硬复位或固件下载。
- 短读、短写、I²C 错误和 RX 内存不足均关闭 IRQ 并锁存故障；不做未验证的电源循环恢复。close 必须观察对应 CORE_RESET 成功，NCI 2.x 还需请求触发的通知，否则锁存超时。
- PM 仅允许已关闭且无故障的 NFC 进入 suspend；活动/故障状态返回 EBUSY。不声明 RF 保持、唤醒或 eSE 可用。板级仅定义 default pinctrl，CLKREQ7 不声明为主机输出。
- 真机须分项验证注册、reset/init、帧收发、IRQ、失败/恢复、close/reopen 与 PM；协议掩码是待验证范围，固件/读卡不能由注册推导。按 QUALITY 取得 L2、schema 与授权 L3。

## Debian 用户空间

Debian 是 MVP 的正常用户空间；当前静态诊断 initramfs 也是 Linux 用户空间，但未集成 Debian。用户空间包括服务、库、权限与应用，不仅是装上发行版；内核须先提供可消费的标准 NFC 接口。

先固定能消费 generic netlink/AF_NFC 的 Linux 软件或最小验证程序及依赖，分别验证设备发现、轮询、只读 NDEF/ISO-DEP；Debian 集成再验证权限、服务生命周期和应用使用。不能默认任意名为 libnfc 的库就支持该控制器/transport。HCE、eSE/OMAPI、支付能力另行评估，不能由 NCI 注册或读卡成功推导。

2026-11-10 Android AIDL HAL、`nfc_nci_nxp_snxxx.so`、`libsn100u_fw.so`、QTEE/eSE 记录仅为硬件/协议/固件依赖线索，不能直接作为 Debian 可用闭包。任何必需固件须另审来源、版本、哈希及分发条件；不为 NFC 引入 Android Binder/Bionic/VINTF 基座。

## 被动诊断

控制台 `nfc-status`（亦由 `status` 调用）按 `nxp,sn100t` 动态扫描 I²C/DT 元数据与 `/sys/class/nfc/nfcN` 注册信息，不依赖总线号，不发 ioctl/NCI/RF 命令。类目录计数包含全部控制器，不代表均属于 SN100T，更不能证明 IRQ、读卡或 eSE。

Linux/Debian 可运行 `python3 tools/nfc_inventory.py --timeout 3`：仅查询 generic-netlink 的 family 与 GET_DEVICE，输出设备索引、名称、协议与 powered；空列表合法，family 缺失、不支持、权限、超时、中断/畸形响应均报错。它不执行 DEV_UP、轮询或写卡，缺少 Python 的静态 initramfs 继续用上述被动诊断。此工具是最小标准消费者，非 Debian 服务集成证明。

## 有线、无线共存

- **有线 USB/PD/PPS**：独立于无线 RX/TX；USB 插线、PD/PPS 状态不能直接作为关闭 NFC RF 的条件。
- **无线接收 RX / 无线反充 TX**：当前未移植对应驱动；分别建模状态后才设计 NFC 共存策略。策略通过已验证的 Linux NFC RF 接口协调，不让无线驱动直接拉低共享 VEN。
- 只有证据确认后才可共享状态与策略；退出异常和恢复用户设置单独验收，不能据对话推断所有无线场景必须关闭 NFC。

## 证据与开放项

[内核 L2 / probe L3](archive/hardware/2026-11-09-umi-nfc-l2-ramboot.md)、[Android 基线](archive/hardware/2026-11-10-umi-nfc-android-baseline.md)、[Linux 接口审计](archive/reviews/2026-11-10-linux-interface-audit.md)；当前设备能力见 [platforms](platforms.md)，未知能力归 RISK-011，旧 ABI 退出归 TD-003。
