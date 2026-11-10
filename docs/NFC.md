# Umi Linux NFC / eSE 适配

RQ-14 / WP10：目标是通过 Linux NFC/NCI 接入 Debian。RQ-13 的 Android NQ 字符设备方案保留为历史 bring-up 证据及 TD-003，不再作为产品用户空间路线。统一规则见 [ARCHITECTURE](ARCHITECTURE.md#linux-接口准则)。

## 硬件与已有接口

- 原 NQ-NCI 来自 LineageOS `android_kernel_xiaomi_sm8250` revision `6fcac56423a078c7bd180e8d2c626c7d66bf1677`，GPL-2.0-only，归属及输入哈希在来源锁中；迁移时继续保留。
- 实机节点 `soc/i2c@984000/nq@28`，地址 `0x28`；Android 总线号为 0，MiSeed 固定主线树为 I2C1，不能硬编码跨系统总线号。GPIO4/5 为 I²C，IRQ111、VEN83、FIRM110、CLKREQ7 来自匹配本机接线。
- 既有 L3 确认 `1-0028` 绑定 `nq-nci` 与 `/dev/nq-nci` 存在。该驱动使用私有 ioctl/read/write，未注册 Linux NFC/NCI；probe/node 不是完整内核适配完成的判据。
- NFCC 与 eSE 共用 VEN；probe 会切换 VEN 并发送 reset/version 命令，open/close 也会影响 IRQ/FIRM，不能把打开节点当成无副作用查询。不得虚构独立 eSE 电源 GPIO。

## Linux 内核迁移

1. 对照锁定 Linux 的 `drivers/nfc/nxp-nci/`、NFC/NCI core 与 `net/nfc/nxp,nci.yaml`，确认 SN100T 的 NCI 版本、I²C 帧、IRQ、启动/固件流程和共享 VEN。上游 binding 未显式列 SN100T，不能仅改 compatible 便认定支持。
2. 证据支持时扩展上游驱动；否则在 NFC/NCI 子系统实现可审查的 transport，使用适用的 gpiod、IRQ、regulator、PM/wakeup 与固件加载接口，补对应 binding。不得并行绑定旧 NQ 驱动争用控制器。
3. 分项验证 NCI 注册、reset/init、帧收发、IRQ、超时/错误、关停/恢复及休眠唤醒；按 QUALITY 取得 L2、DT schema 与授权 L3。保护共享 VEN，禁止附带 eSE 操作或固件升级。
4. Linux 标准路径成立且回归通过后，移除旧私有 ABI 构建依赖和诊断绑定，偿还 TD-003；迁移期间不将其标记为 Debian 功能入口。

## Debian 用户空间

Debian 是 MVP 的正常用户空间；当前静态诊断 initramfs 也是 Linux 用户空间，但未集成 Debian。用户空间包括服务、库、权限与应用，不仅是装上发行版；内核须先提供可消费的标准 NFC 接口。

先固定能消费 generic netlink/AF_NFC 的 Linux 软件或最小验证程序及依赖，分别验证设备发现、轮询、只读 NDEF/ISO-DEP；Debian 集成再验证权限、服务生命周期和应用使用。不能默认任意名为 libnfc 的库就支持该控制器/transport。HCE、eSE/OMAPI、支付能力另行评估，不能由 NCI 注册或读卡成功推导。

2026-11-10 Android AIDL HAL、`nfc_nci_nxp_snxxx.so`、`libsn100u_fw.so`、QTEE/eSE 记录仅为硬件/协议/固件依赖线索，不能直接作为 Debian 可用闭包。任何必需固件须另审来源、版本、哈希及分发条件；不为 NFC 引入 Android Binder/Bionic/VINTF 基座。

## 被动诊断

控制台 `nfc-status`（亦由 `status` 调用）动态扫描 I²C 的 `qcom,nq-nci` compatible，打印驱动绑定、DT 地址/引脚分配及节点类型。只读元数据，不打开 NFC 字符设备、不发 ioctl/NCI/RF 命令；不可证明传输、IRQ、唤醒、读卡、HCE 或 eSE。这是迁移期诊断，标准 NCI 驱动接入后须更新发现方式；构建与回归证据见本轮审计。

## 有线、无线共存

- **有线 USB/PD/PPS**：独立于无线 RX/TX；USB 插线、PD/PPS 状态不能直接作为关闭 NFC RF 的条件。
- **无线接收 RX / 无线反充 TX**：当前未移植对应驱动；分别建模状态后才设计 NFC 共存策略。策略通过已验证的 Linux NFC RF 接口协调，不让无线驱动直接拉低共享 VEN。
- 只有证据确认后才可共享状态与策略；退出异常和恢复用户设置单独验收，不能据对话推断所有无线场景必须关闭 NFC。

## 证据与开放项

[内核 L2 / probe L3](archive/hardware/2026-11-09-umi-nfc-l2-ramboot.md)、[Android 基线](archive/hardware/2026-11-10-umi-nfc-android-baseline.md)、[Linux 接口审计](archive/reviews/2026-11-10-linux-interface-audit.md)；当前设备能力见 [platforms](platforms.md)，未知能力归 RISK-011，旧 ABI 退出归 TD-003。
