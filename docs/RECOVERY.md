# MiSeed Recovery 行为契约

Recovery 是 Linux 上的独立救援用户空间，内核与启动接口归 [ARCHITECTURE](ARCHITECTURE.md)，功能范围归 [PRODUCT](PRODUCT.md)。
本文件规定后续实现必须遵守的运行行为；文中的入口和状态均为设计，不代表已有可启动镜像或安装器。

## 启动与诊断

- UEFI 提供正常系统与 Recovery 两种载荷选择；正常 rootfs 不可用时，Recovery 不依赖该 rootfs 的程序或库。
- 使用独立 initramfs 的 `/init` 启动救援进程；必要模块/固件随闭包提供，缺少依赖则报错并停止相关功能，不回落到安装。
- 初期只做设备信息、日志、显式启用的 USB 诊断/登录与救援 Shell；触摸 UI 后置。
- 默认不挂载内部文件系统、不自动 fsck、不启用 swap，不把真实块设备导出为 USB 存储。
- 日志先保留在 RAM，导出由操作者选择；脱敏后才进入证据归档。不预置通用 root 密码或无条件开放网络登录。
- Shell 是有权限的人工诊断入口，不能将它宣称为强制只读沙箱；不得用启动脚本自动执行磁盘修改。

Linux initramfs 的 `/init` 机制依据[内核文档](https://www.kernel.org/doc/html/latest/filesystems/ramfs-rootfs-initramfs.html)，按固定内核版本复核。

## 安装与恢复状态机

后续安装器采用以下单向准入；诊断功能不隐式进入写入阶段：

```text
诊断 → 离线验证包 → 生成安装计划 → 核对授权/设备/回退 → 执行 → 验证结果
                  任一失败 → 停止，保留诊断与失败记录
```

- 离线验证检查格式/大小限制、路径穿越、清单、哈希、签名与信任根、设备变体及版本策略；哈希正确不等于包可信。
- 计划列出目标分区身份、预期容量、镜像摘要、数据影响和恢复步骤；不由 `/dev/sdaN` 或当前槽位猜测目标。
- 写入阶段仅接受与已审核计划绑定的当轮明确授权，并重新核对目标未挂载、身份及容量；保护不全则拒绝。
- 更新中断、供电不足、存储错误或读回不符必须停止后续步骤，不自动换槽、重新分区、重锁 Bootloader 或重试危险写入。
- 备份完成不等于可以恢复；恢复需独立核对来源/目标身份、布局和校验值。事务/断电恢复设计及真实恢复演练通过前，不宣称可回滚。

MVP 不包含安装写入后端；首个包验证器只使用普通文件和合成输入。供电参数、设备布局与授权边界分别归 platforms 和 AGENTS。

## 真机实验前的备份准入

- 维护者无需个人数据备份，并报告官方 MiFlash 曾成功线刷、身份信息正常；采纳为官方恢复路径的既有实机证据，不要求重刷。MiSeed 新载荷失败的专项回退仅在获授权 L3 前评估。`userdata` 裸镜像不是通用备份；metadata encryption 密钥也可能在 `metadata`，不得独立恢复（[AOSP](https://source.android.com/docs/core/architecture/partitions)，[metadata encryption](https://source.android.com/docs/security/features/encryption/metadata)）。
- 维护者提供国行小米官方 Recovery ZIP 1.0.7、Fastboot ROM 1.0.3 和 MiFlash 工具（均在 `.work/`）。设备报告为国行 umi；Lineage fingerprint 的 `MIXM` 是当前软件标识，不能单独推定实物销售区域或否定该官方包。维护者报告刷 Lineage 前为官方 1.0.5，官方最新线刷包为 1.0.3。文件 SHA-256 已记录作本地完整性基线；无需再以第三方目录摘要作为官方来源判定。
- 已只读检查 Fastboot ROM 脚本：`flash_all_except_storage` 会写 `misc` 并更新 boot/ABL/XBL/modem/DTBO/VBMeta/system 等分区；维护者确认官方线刷工具包含此行为，且其设备曾成功线刷、身份信息未丢失，因此允许 `misc` 随官方完整刷机流程更新。`flash_all` 另会擦除 metadata、写入 userdata。维护者明确严禁重锁 Bootloader：`flash_all_lock` / `flash_all_lock.bat` 及任何会执行锁定的选项绝不可运行；所有恢复预案都必须维持 BL 解锁。ROM 包虽含 `persist.img`，审查到的 Fastboot 脚本目标没有 `persist` 或 modemst/fsg/fsc/mdm1m9kefs。
- Recovery ZIP 的可读 `updater-script` 明列 boot、ABL/XBL、modem、DTBO、VBMeta 和动态系统分区更新，未列出 `misc`、userdata、metadata 或身份/NV 分区；其 `update-binary` 未运行，Recovery 安装入口和最终行为未在设备上验证。故该 ZIP 是首选的官方恢复路径候选，仍须在后续单独审查入口和保护分区约束后再计划使用。
- 维护者明确禁止触碰设备身份/基带/校准分区：`modemst1`、`modemst2`、`fsg`、`fsc`、`persist`、`persistbak`、`mdm1m9kefs1/2/3`、`mdm1m9kefsc`。禁止对它们读取分区内容、备份、散列、擦除、写入或恢复；不得读取/改写 IMEI、Wi-Fi MAC、SN 等身份信息，也不得从其他设备复制。
- 本机 by-name 名称表还包含 `boot`、`recovery`、`dtbo`、`vbmeta*`、`super`、`userdata`、`abl*`、`xbl_*`、`aop*`、`tz*`、`hyp*`、`devcfg*` 等。该列表没有容量、槽位语义或恢复顺序。`dtbo` 等小米原厂镜像在 MiSeed 实验计划中保持不动；官方恢复流程按其自身脚本更新。`misc` 含 BCB 和厂商保留空间；维护者允许其随官方恢复流程更新，不独立读取、备份或替换。
- 未来如需评估启动实验，优先从维护者提供的官方 Recovery 1.0.7 ZIP 审查 `boot.img`，不从设备分区提取镜像；其 SHA-256 为 `71acc2eb62b74d47da4d7ea856b21075073d8f767c6e74ae5780446a2b6fef6d`。Fastboot 1.0.3 包中 `boot.img` 的 SHA-256 为 `e81aae005fe1a768ab721bb9b248c665c974bfb8ec66b6911558b3fd1e5cf768`，版本更旧且只可作为对照，不优先选用。本轮没有刷写授权，`boot`、`userdata` 及所有其他分区均不写入。`super` 不拆分或写入；不得把 `devinfo`、安全/防回滚或校准分区作为通用镜像回刷。

本机 `orange` 映射为 BL UNLOCKED。既有官方线刷经验作为恢复证据；`fastboot boot` 未验证，不作前提。MiSeed 载荷须先主机端核对上游契约，并备妥保持解锁、保护身份/NV 的官方恢复方案；绝不运行 lock 脚本/选项。设备操作需另行授权。

## 验收接口

按 [实施计划](implementation-plan.md) 分阶段交付，按 [QUALITY](QUALITY.md) 记录证据。至少覆盖：正常/Recovery 启动选择、rootfs 缺失仍能救援、依赖缺失、错误包/错误变体/未授权拒绝，以及授权后的中断和恢复反例。
仅有 Linux 直启 Shell 不算 UEFI → Recovery 验收；同一内核更换后，正常系统与 Recovery 都要重新验证。
