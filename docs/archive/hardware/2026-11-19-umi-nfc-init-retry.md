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

2026-11-19，按用户本轮授权，使用同一 SHA-256 的 boot 镜像在小米 10（Umi/CSOT）上再做一次 RAM 启动。用户将空白 MIFARE Classic 测试卡贴在手机背面。

1. 在 Windows PowerShell 上运行 `python -X utf8 .work/umi-classic-discovery-20261119/run.py --check`，核对镜像、构建来源锁和平台工具。运行同一脚本的 `--ram-boot-once` 后，Bootloader 接受镜像，救援控制台导出 `dtb` 和 `nfc-status`。FDT 硬件子树和 RAM 通过，根 `model` 与 `compatible` 仍为已知差异。
2. 救援控制台执行一次 `nfc-discover`，通过 Linux NFC Generic Netlink 初始化、轮询 MIFARE 并枚举目标。结果为 `stage=target up=1 polling=1 targets=1 protocols=4 rc=0 cleanup=0`，确认发现 1 张 MIFARE 卡，收尾和关闭成功。
3. `dmesg` 记录前两次 `CORE_RESET` 写均为 `0x28/W phase=address gp_length=0`，返回 `-ENXIO`。第三次写入完整 4 字节，收到 NCI 2.x 复位响应和通知。关闭时成功收到另一组复位响应和通知。打开时读回 `FIRM=0 VEN=1`。外部波形未采。
4. 救援控制台进入 Bootloader，Fastboot 核对设备后正常重启。ADB 复核 `ro.product.device=umi` 和 `sys.boot_completed=1`。

本次验证范围为初始化、RF 寻卡和关闭。卡内扇区、NDEF 和关闭后重开仍待验证。

本地证据位于 `.work/umi-classic-discovery-20261119/`。`evidence/evidence.json` 报告 `PASS scoped MIFARE discovery`，`android-restored.json` 记录 Android 恢复。复算 `evidence/discovery.txt` 的 SHA-256 为 `1b546ac70934b5f51a9a6fdce5ee769a1127b59d47a67b12039def5bb979f04c`，`evidence/dmesg.txt` 为 `7eac5dd0156b2c4dfd60c8144d76492ca9dce0e8fe294ba8af061c772c17999d`。

## 寻卡与重开复验

2026-11-19，在维护者授权下，对同一 Umi/CSOT 做两次独立 RAM 启动。第一轮使用上节原镜像及多协议 `nfc-l3`，第二轮由维护者确认贴好空白 MIFARE Classic 1K 卡，救援探针仅轮询 MIFARE。第二轮只重编静态 ARM64 救援 init 并重打包，内核、DTB、配置与原镜像一致。每轮分别运行相应目录下的 `run.py --check` 和 `run.py --ram-boot-once`，证据包括 USB 救援控制台结果、KMSG、运行时 FDT 和 Android 恢复。

| 会话 | boot SHA-256 | KMSG SHA-256 |
| --- | --- | --- |
| `.work/umi-nfc-reopen-20261119/` | `5293d6c8eb036a24ee5860b7e706959f25218bba8a7947725163b3516b5ba709` | `41e337f2258f3e9abe066e06411526b89c4d54fef623dc8b36778f2d7662d0a7` |
| `.work/umi-nfc-mifare-reopen-20261119/` | `49328d1f64cd48f111f8c6b0750e1f6afdc91ecc1fc02c10e024ddafcd523204` | `b9a9369bbfaf2a8c4f262f4ecde84f0baa1b4c5e262c81eee3c1f636cfbe5462` |

- 两轮首次 `CORE_RESET` 均先返回 `-ENXIO`，第二次写通并收到 NCI 2.x 复位响应与通知。`DEV_UP` 和空协议拒绝反例均通过。
- 两轮轮询结束时均未发现目标。清理阶段 GENI 对 `0x28/W` 报地址 NACK，失败命令为 `RF_DEACTIVATE`（`0x0106`），transport 锁存 `-ENXIO` 并保留 VEN。两轮均未进入重开路径，探针最终返回 `-ENODATA`，采集脚本退出 1。锁定内核的 `nci_stop_poll()` 不向 Generic Netlink 传递 NCI 命令失败，因此以 KMSG 判定停轮询的硬件结果。
- 运行时 FDT 的硬件子树和 RAM 均通过，根 `model` 和 `compatible` 保持已知差异。两轮均经 Bootloader 正常返回 Android，ADB 确认 `umi` 和 `sys.boot_completed=1`。

锁定内核的 `nfc_targets_found()` 会清除轮询状态。上节寻卡通过时的收尾证据覆盖了关闭；新发现的故障出现在未发现目标、需要发送 `RF_DEACTIVATE` 的路径。两次地址阶段证据来自 GENI，RF 场及外部波形均未采。

### 独立关闭重开

保留同一内核、DTB 与配置，仅将本地救援 init 的 `nfc-l3` 调整为跳过 RF，执行 `DEV_UP → DEV_DOWN → DEV_UP → DEV_DOWN`。变体 boot SHA-256 为 `a2d53ae6609b9fcda6ff64ca024e4661cc53b562108fe4c832b069b31d58f60c`。第一次运行的原始控制台帧返回 0，通用寻卡校验误拒了无 RF 的成功帧。依据保存的回包修正本地采集器，离线验证完整帧及截断反例后，以同一变体补采。

补采返回 `rc=0`。KMSG 记录两次 open 均读回 `FIRM=0 VEN=1`，两次打开、两次关闭各收到一组 NCI 2.x `CORE_RESET_RSP/NTF`。首笔地址 NACK 后第二次写通，KMSG 无 transport 锁存。两个会话均确认返回 Android。此会话通过独立关闭重开，RF 后的关闭重开待验。证据见 `.work/umi-nfc-cycle-kmsg-20261119/evidence/`，KMSG SHA-256 为 `1c65cda43b87069dc1b19b898b7ea7b165b4d7caaffda918064f9d547a8bab80`。

### 其他验证项

- WSL 运行 `python3 -X utf8 -m unittest discover -s tests -p 'test_nfc*.py'` 通过 33 项，含 transport PM 的关闭态和忙态反例。定向 `test_type4_fixture.py` 通过本机 seqpacket 内容验证。
- 现场只有空白 MIFARE Classic 1K，现有 `nfc-ndef` 探针需要带已知文本的 Type 4 标签。Classic 扇区读取和真机 NDEF 均未执行。
- 当前 resolved config 关闭 `CONFIG_SUSPEND`。真机 PM 需要可休眠的配置、独立唤醒及回退。HCE 需要 ISO-DEP 主机监听路由和外部读卡器，eSE 需要经证实的 NFCEE 路由及测试 applet。两项当前均未执行。Debian 集成按维护者要求暂缓。
