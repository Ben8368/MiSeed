# Umi 有线固定 PD 稳定 L3（PR #4 HEAD）

| 项 | 内容 |
| --- | --- |
| 日期 / 操作者 | 2026-11-10；维护者授权，Cursor 执行构建/启动/导出 |
| 授权范围 | RAM `fastboot boot`（不刷写）；拔插充电器与接回诊断口；只读导出。未授权合并发布外的其他写操作 |
| 设备 / 变体 | Xiaomi Mi 10 `umi` / CSOT；不记录序列号 |
| 仓库 / 输入身份 | 分支 `codex/nfc-wireless-charging`，验证镜像对应提交 `75e0e38c2dd3a6a85e3335d05f30c98caccb4366`（源码职责拆分后、导出 pacing 修复前） |
| 载荷 | `.work/pr4-pd-nfc-l3-20261110/boot.img`（本轮复制为 `pr4-pd-only-l3-20261110/boot.img`），9,617,408 bytes，SHA-256 `aa5a46831df1373cd4cc0fafea7689af374ce273630a2c8d0451a4094bd1b7f7` |
| 启动 | 唯一 Fastboot：`product=umi` `unlocked=yes` `is-userspace=no`；`fastboot boot` Sending/Booting OKAY；诊断口 COM6（VID 1d6b / PID 0104） |
| 证据级别 | 有线**固定 PD / SMB5** L3；非 PPS（源端仅固定 PDO）；非保护注入/温控标度专项 |

## 结果摘要

- 同一 `boot_id=c9db0037-ba57-4e8e-b049-6df8535a40f8`。
- 短测：约 36 s 充电器窗口，TCPM 协商 **5 V / 3 A**；峰值净充约 2.3–2.4 A；温度 32–33°C；无充电中 `fault` / `bus_low` / `bus_high` / `-ENOTCONN`。
- 长测：充电器窗口约 **182–802 s（≈10.3 分钟）**；SOC **81→89**；温度 **33–35°C**；中段净充约 **1.9–2.0 A**（早约 2.3 A → 晚约 1.5 A）；中段 `enabled=1` 且无负电流。
- 接入瞬间 PPS `error=-11`（EAGAIN）回落、`owned=0`，随后固定 PD 持续充电；与无 APDO 源端行为一致，不记为充电中故障。
- 维护者宣告：该 Umi/CSOT 条件下，本镜像有线**固定 PD**充电稳定。

## 导出与后续修复

- 长测一次主机导出出现序号缺口（设备侧 `storage_errors=0`），属已知 Windows CDC 丢行；不据此否定充电结论。
- 同日修复：主机专用读线程 + 8 MiB RX 缓冲；救援 `charge-log` 每 4 行 pacing/`fflush`。
- 同日复验：镜像 SHA-256 `b4026d673294be758320dc556147720dc6b519259f29d07c103af7d9594d44e9`（含 pacing）RAM 启动后全量导出 `first=0 next=1102 count=1102`、`gap_count=0`、`storage_errors=0`，`boot_id=f4152e76-3eb8-4891-8a63-729511bc7352`。不回溯改写上文充电结论的镜像身份。

## 未覆盖

- PPS/APDO 路径、保护注入、温控标度精度、旧 BQ latch/`-ENOTCONN` 根因、Debian 消费者、无线 RX/TX。

原始长日志仅保留在忽略目录 `.work/pr4-pd-only-l3-20261110/` 与 `.work/pr4-pd-export-fix-20261110/l3/`，不入库。
