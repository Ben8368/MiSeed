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
