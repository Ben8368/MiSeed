# Umi SN100T 首次初始化有限重试 L3

2026-11-19，在 Umi 上 RAM 启动一次，只发一次 NFC Generic Netlink `DEV_UP`。首次 `CORE_RESET` 遇到写地址 NACK，一次重试后收到 NCI 2.x 响应和通知，`DEV_UP` 返回 0。

| 内核时间 (s) | 观测 |
| --- | --- |
| 7.047660 | open 读回 FIRM=0、VEN=1 |
| 7.073248 | `984000.i2c` 报 `0x28/W phase=address gp_length=0 irq=0x409` |
| 7.095243 | 第一次 4 字节写返回 `-ENXIO` |
| 7.106939 | 第二次写返回 4 |
| 7.124653 / 7.135230 | 先收到成功的 CORE_RESET_RSP，再收到对应 CORE_RESET_NTF |

控制器读回 `clk_sel=0x0 m_clk_cfg=0x11 scl_counters=0x202012`，匹配 Linux 19.2 MHz 时钟源表。主机采集模式 `init-retry`，无外部波形。日志时间包含输出及调度开销，不能推导芯片唤醒时间。该会话只验证首笔恢复和初始化。

## 镜像与复核

- 基线 `5040ecf` 加本地变更。Windows x64、PowerShell 7.6.9、WSL2 Ubuntu 24.04、AArch64 GCC 13.3.0。控制器使用[既有 GP_LENGTH 阶段判定的来源](../research/2026-11-18-csot-nci-fdt-followup.md)，本地把数据和含混的写 NACK 返回为 `-EIO`。首帧 reset 总计至多发送三次，后续错误仍锁存。DTB、内核配置、救援 init 与 11-18 寄存器诊断镜像哈希一致。
- `build_kernel_l2.sh` 原生显示配置构建通过。`validate_kernel_dt.py` 的板级与打包 DTB 校验通过，四个 DT 反例全部执行。WSL `unittest discover -s tests` 共 313 项，4 项跳过。boot SHA-256 `5293d6c8eb036a24ee5860b7e706959f25218bba8a7947725163b3516b5ba709`，打包内核 SHA-256 `878b21d0cb73e19fb52eff1696b47ba94518dfe5772a5b88f4372b3845837d7f`。
- 运行时 FDT 硬件子树和 RAM 核对通过，已知根 `model`、`compatible` 差异仍 FAIL。USB 救援控制台采集完毕，经 bootloader 正常返回 Android，已核对 `umi` 与 `sys.boot_completed=1`。
- 本地证据目录 `.work/nfc-reset-retry-20261119/l2/first-evidence/` 保存 `evidence.json`、`nfc-first.txt` 与 `dmesg.txt`。KMSG 导出 SHA-256 `d80672811c847ea84c990995464fb61415c52ecd4e087c9aaa612a3388362649`。

## 同镜像 MIFARE Classic 寻卡 L3

2026-11-19，按用户本轮授权，使用同一 SHA-256 的 boot 镜像在小米 10（Umi/CSOT）上再做一次 RAM 启动。维护者事后确认，测试目标由另一部手机模拟，界面显示为 MIFARE Classic 1K。

1. 在 Windows PowerShell 上运行 `python -X utf8 .work/umi-classic-discovery-20261119/run.py --check`，核对镜像、构建来源锁和平台工具。运行同一脚本的 `--ram-boot-once` 后，Bootloader 接受镜像，救援控制台导出 `dtb` 和 `nfc-status`。FDT 硬件子树和 RAM 通过，根 `model` 与 `compatible` 仍为已知差异。
2. 救援控制台执行一次 `nfc-discover`，通过 Linux NFC Generic Netlink 初始化、轮询 MIFARE 并枚举目标。结果为 `stage=target up=1 polling=1 targets=1 protocols=4 rc=0 cleanup=0`，确认发现 1 个 MIFARE 模拟目标，收尾和关闭成功。
3. `dmesg` 记录前两次 `CORE_RESET` 写均为 `0x28/W phase=address gp_length=0`，返回 `-ENXIO`。第三次写入完整 4 字节，收到 NCI 2.x 复位响应和通知。关闭时成功收到另一组复位响应和通知。打开时读回 `FIRM=0 VEN=1`。外部波形未采。
4. 救援控制台进入 Bootloader，Fastboot 核对设备后正常重启。ADB 复核 `ro.product.device=umi` 和 `sys.boot_completed=1`。

本次验证覆盖初始化、RF 发现模拟目标和关闭。实体卡读取、NDEF 和关闭后重开未在此会话验证。

本地证据位于 `.work/umi-classic-discovery-20261119/`。`evidence/evidence.json` 报告 `PASS scoped MIFARE discovery`，`android-restored.json` 记录 Android 恢复。复算 `evidence/discovery.txt` 的 SHA-256 为 `1b546ac70934b5f51a9a6fdce5ee769a1127b59d47a67b12039def5bb979f04c`，`evidence/dmesg.txt` 为 `7eac5dd0156b2c4dfd60c8144d76492ca9dce0e8fe294ba8af061c772c17999d`。

## 寻卡与重开复验

2026-11-19，在维护者授权下，对同一 Umi/CSOT 做两次独立 RAM 启动。第一轮使用上节原镜像及多协议 `nfc-l3`，第二轮维护者确认模拟手机已就位，救援探针仅轮询 MIFARE。第二轮只重编静态 ARM64 救援 init 并重打包，内核、DTB、配置与原镜像一致。每轮分别运行相应目录下的 `run.py --check` 和 `run.py --ram-boot-once`，证据包括 USB 救援控制台结果、KMSG、运行时 FDT 和 Android 恢复。

| 会话 | boot SHA-256 | KMSG SHA-256 |
| --- | --- | --- |
| `.work/umi-nfc-reopen-20261119/` | `5293d6c8eb036a24ee5860b7e706959f25218bba8a7947725163b3516b5ba709` | `41e337f2258f3e9abe066e06411526b89c4d54fef623dc8b36778f2d7662d0a7` |
| `.work/umi-nfc-mifare-reopen-20261119/` | `49328d1f64cd48f111f8c6b0750e1f6afdc91ecc1fc02c10e024ddafcd523204` | `b9a9369bbfaf2a8c4f262f4ecde84f0baa1b4c5e262c81eee3c1f636cfbe5462` |

- 两轮首次 `CORE_RESET` 均先返回 `-ENXIO`，第二次写通并收到 NCI 2.x 复位响应与通知。`DEV_UP` 和空协议拒绝反例均通过。
- 两轮轮询结束时均未发现目标。清理阶段 GENI 对 `0x28/W` 报地址 NACK，失败命令为 `RF_DEACTIVATE`（`0x0106`），transport 锁存 `-ENXIO` 并保留 VEN。两轮均未进入重开路径，探针最终返回 `-ENODATA`，采集脚本退出 1。锁定内核的 `nci_stop_poll()` 不向 Generic Netlink 传递 NCI 命令失败，因此以 KMSG 判定停轮询的硬件结果。
- 运行时 FDT 的硬件子树和 RAM 均通过，根 `model` 和 `compatible` 保持已知差异。两轮均经 Bootloader 正常返回 Android，ADB 确认 `umi` 和 `sys.boot_completed=1`。

锁定内核的 `nfc_targets_found()` 会清除轮询状态。上节寻卡通过时的收尾证据覆盖了关闭；新发现的故障出现在未发现目标、需要发送 `RF_DEACTIVATE` 的路径。两次地址阶段证据来自 GENI，RF 场及外部波形均未采。

维护者补充，另一部手机在断开后会停止模拟。两轮 12 秒轮询均未同步记录模拟的启停时间，因此窗口中的目标可用性未知，未发现目标的结果无法判定读卡端 RF 是否回归。模拟停止可能使流程进入实际发送 `RF_DEACTIVATE` 的清理路径。日志确认了小米 10 本地 I²C 地址 NACK，原因待查。复验时在救援控制台就绪后启用模拟，确认它持续到轮询结束，或使用已知正常的实体测试标签。

### 独立关闭重开

保留同一内核、DTB 与配置，仅将本地救援 init 的 `nfc-l3` 调整为跳过 RF，执行 `DEV_UP → DEV_DOWN → DEV_UP → DEV_DOWN`。变体 boot SHA-256 为 `a2d53ae6609b9fcda6ff64ca024e4661cc53b562108fe4c832b069b31d58f60c`。第一次运行的原始控制台帧返回 0，通用寻卡校验误拒了无 RF 的成功帧。依据保存的回包修正本地采集器，离线验证完整帧及截断反例后，以同一变体补采。

补采返回 `rc=0`。KMSG 记录两次 open 均读回 `FIRM=0 VEN=1`，两次打开、两次关闭各收到一组 NCI 2.x `CORE_RESET_RSP/NTF`。首笔地址 NACK 后第二次写通，KMSG 无 transport 锁存。两个会话均确认返回 Android。此会话通过独立关闭重开，RF 后的关闭重开待验。证据见 `.work/umi-nfc-cycle-kmsg-20261119/evidence/`，KMSG SHA-256 为 `1c65cda43b87069dc1b19b898b7ea7b165b4d7caaffda918064f9d547a8bab80`。

### 其他验证项

- WSL 运行 `python3 -X utf8 -m unittest discover -s tests -p 'test_nfc*.py'` 通过 33 项，含 transport PM 的关闭态和忙态反例。定向 `test_type4_fixture.py` 通过本机 seqpacket 内容验证。
- 现场使用另一部手机模拟 MIFARE 目标。现有 `nfc-ndef` 探针需要带已知文本的 Type 4 标签。Classic 扇区读取和真机 NDEF 均未执行。
- 当前 resolved config 关闭 `CONFIG_SUSPEND`。真机 PM 需要可休眠的配置、独立唤醒及回退。HCE 需要 ISO-DEP 主机监听路由和外部读卡器，eSE 需要经证实的 NFCEE 路由及测试 applet。两项当前均未执行。Debian 集成按维护者要求暂缓。

## 实体卡寻卡与重开通过

2026-11-19，维护者换用实体 NFC 卡，并在 12 秒轮询期间将卡持续贴在 Umi/CSOT 背面。使用原镜像做一次授权 RAM 启动，boot SHA-256 为 `5293d6c8eb036a24ee5860b7e706959f25218bba8a7947725163b3516b5ba709`。Windows PowerShell 执行 `python -X utf8 .work/umi-nfc-physical-card-20261119/run.py --check` 与同脚本的 `--ram-boot-once`。运行时 FDT 硬件子树和 RAM 通过，根身份仍为已知差异。

- `nfc-l3` 通过初始化、空协议拒绝、多协议轮询、关闭、重开和最终关闭，`rc=0`。Linux 枚举 1 个实体目标，协议掩码 `4`（`NFC_PROTO_MIFARE_MASK`）。本次未执行卡内容读取。
- 首次 `CORE_RESET` 报 `0x28/W phase=address gp_length=0`，第一次返回 `-ENXIO`，第二次写入 4 字节。两次打开均读回 `FIRM=0 VEN=1`，打开、关闭、重开和最终关闭共确认四组 NCI 2.x `CORE_RESET_RSP/NTF`，未出现传输锁存。
- 这次通过了发现目标后的关闭重开。无目标的停轮询地址 NACK 仍按上节记录跟进。测试结束后返回 Android，ADB 确认 `umi` 与 `sys.boot_completed=1`。

本地证据在 `.work/umi-nfc-physical-card-20261119/evidence/`。`nfc-l3.txt` SHA-256 为 `f5a1abd8908041aef381b1d4ebe4a9847a20d25ef1b84043a58b5c1702ecdba1`，`dmesg.txt` 为 `6a15e0642d6efc1456bf70c2b9074778461b9cf90bb9d986679fbfbc84565eba`。

## 无卡停止轮询地址 NACK 恢复

2026-11-19，维护者移开卡及模拟设备，在同一 Umi/CSOT 上授权两次独立 RAM 启动。使用同一 ARM64 救援程序、DTB、resolved config 和 12 秒 MIFARE 轮询，先验证原内核，再验证基于 `56b96f9` 的本地候选内核。Linux NFC Generic Netlink 发出启停请求，并用 AF_NFC SOCK_RAW 监视停轮询的命令及响应状态，记录计数和退出码。监视只在停轮询前开启，无帧载荷导出。

| 内核 | boot SHA-256 | NFC 结果 | KMSG SHA-256 |
| --- | --- | --- | --- |
| 原内核，`.work/umi-nfc-idle-20261119/` | `1193c01141c4ecc8b3281992b5bb80971022b236ba18ee13e649888152a4e06b` | `targets=0 tx=1 rsp_ok=0 rc=-42`，GENI 地址 NACK，`0x0106` 锁存 | `b8ac466d7365a7847744e91f2904c60d839f22d88d0eb64a9bd603817c5290c2` |
| 候选内核，`.work/umi-nfc-idle-retry-20261119/` | `7c0bdb32d45ab60bff1fcd9132f314f476eacbf558bf1467b7a591a8b1123f32` | `targets=0 tx=1 rsp_ok=1 rc=0`，关闭重开及最终关闭通过 | `88dca06b95d713c245ae99dca60fdc9d9bb3ec08e7569a8e86e2622b7cfd8857` |

- 候选首笔停止轮询写报 `0x28/W phase=address gp_length=0`，返回 `-ENXIO`。第二次事务写入完整 4 字节，监视器收到对应的成功响应。NCI 层只下发一条命令，第二次事务发生在 I²C transport 内。两次打开及两次关闭均有 NCI 2.x 复位响应和通知。地址未应答的物理原因仍待波形核实。
- 仅对配对的 GENI 驱动区分出的地址 NACK 重发一次完整 `RF_DEACTIVATE idle`，保留首帧复位预算。合成回归覆盖成功恢复、预算耗尽、数据错误、短写、其他模式、分片和未配对适配器。空场监视反例覆盖缺响应、失败状态、错设备和顺序错误。
- Windows PowerShell 对两组产物各执行 `run.py --check` 与 `run.py --ram-boot-once`。原内核按预期退出 1，候选退出 0，两次均自动恢复 Android。运行时 FDT 子树和 RAM 通过，根身份保持已知差异。
- WSL Ubuntu 24.04、AArch64 GCC 13.3.0 完成 `MISEED_NATIVE_DSI=1 bash tools/build_kernel_l2.sh`。内核 SHA-256 `acdccb0d16d15aa8465e837cc6d5f3e9bf8ee08586b22aedae5056168fa61ef0`，重打包时用原探针的救援 initramfs。WSL `python3 -X utf8 -m unittest discover -s tests` 通过，313 项、4 跳过。实体卡成功会话见上节。
- PM 单独用锁定内核 `olddefconfig` 解析了休眠与 PM8150 RTC 唤醒候选配置，关闭 RTC 时钟自动读写。该候选尚未编译或验证唤醒。实体卡会话报告 MIFARE，现有 NDEF 验收使用带已知文本的 Type 4 测试标签。

### 卡内数据读取准备

- `.work/umi-nfc-type4-read-20261119/run.py --check` 核验了停轮询修复版内核配原救援 init 的镜像和来源锁。boot SHA-256 为 `58692cf3963618e2d7af36fc57c4abbe0426c3ec42ff14f54143505f0a5bbf6b`。探针只接受 ISO-DEP A/B 测试目标，匹配 `MiSeed` 文本后检查关闭重开。WSL `test_type4_fixture.py` 的内容和错误反例通过。
- 维护者确认现场只有上次报告为 MIFARE 的实体卡。固定内核把 NCI T2T 目标归入 MIFARE 掩码。当前探针只实现 Type 4 读取，Classic 扇区认证与读块需另行适配。本次无匹配测试目标，未启动镜像或执行真机内容读取。
