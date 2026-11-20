# Umi 无线 RX：PM8150B 电源轨修正与 RX1619 探测 L3

| 项 | 内容 |
| --- | --- |
| 日期 / 操作者 | 2026-11-20 / 维护者本机 |
| 授权范围 | 本轮明确授权 `fastboot boot` 单次 RAM 启动与插拔取证；不刷写、不写分区、不主动开充 |
| 设备 / 变体 | Umi CSOT（序列号脱敏）；PM8150B + RX1619 + SN100T |
| 原系统 / 底层固件 | 保持既有 Android，仅 RAM 引导后 `reboot` 或 `bootloader` 返回 |
| 仓库 / 输入身份 | 基线 `00d7a34` 加本轮工作树；Linux v6.18.60 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；`MISEED_RX_PROFILE=enable` |
| 载荷 | `boot.img` `a91fc2f6152d9d6148c96ba3785e4cd8d268d1c26e960c647c28500f55ecac5f`；`umi-csot.dtb` `fe30f3b041a8c0c10992e33f62c3faa7fe1aa2c1bbde430879ab49cdb9a36b6d` |
| 前置 / 回退 | 手机可随时 `reboot` 回 Android；异常即断开 USB 结束 |
| 证据级别 | 诊断镜像 L3：RX1619 驱动绑定，上座放电降低，有线 SMB5/BQ 保持禁充。普通模式双输入互锁、无线净充入和 power-good 待验 |

## 方法

`fastboot boot` 载入 RAM 镜像，`MISEED_RX_PROFILE` 依次为 `observe` 与 `enable`（探测时拉低 GPIO155）。两种镜像都启用了强制关闭有线 SMB5 充电和 BQ25970 泵的诊断配置。操作者拔线、上座、离座、插回；RAM 环形日志每 2 s 取一次 `power_supply` 快照，重连后经 CDC 导出。原始日志留在 `.work/`，不入库。

## 结果

**observe（`79eadb53` 镜像）**：22.9–52.2 s `TYPEC port=port0 partner=0 rc=0` 连续 30 s（真实拔线窗口）；22.9–27.0 s 电池 `-404..-423 mA`、3.927–3.931 V；29.1–48.0 s 上座期间电压升至 4.005–4.008 V、电流降至 `-2.4..-2.9 mA`；50.1 s 离座回落 `-400 mA`。全程 `pm8150b-charger online=0`、`chg_enable=0`、`miseed-power power_path=0x95`，诊断配置下有线 SMB5 充电保持关闭。

**enable 首轮**：`pin_config_group_set op failed for group 0`；`charger@1000: deferred probe pending: qcom-pm8150b-charger: RX interlock source`。旧镜像同时配置 GPIO1/GPIO7 的 `power-source=<1>`；失败报告是控制器的 group 0，不能单凭这条日志定论 GPIO7 的 subtype 或实际 VIN。pinctrl 失败连带 `rx1619` 未注册；锁定驱动按 subtype 限制 VIN 索引。

**enable 修正后（`a91fc2f6` 镜像）**：dmesg `<6>rx1619 16-0061: RX1619 probed successfully, initial pg=0`。19.0–23.1 s 电池 `-416..-434 mA`、3.957–3.963 V；25.2–41.7 s 上座电压 4.019–4.020 V、电流 `-0.4..-2.4 mA`；43.8 s 离座回落 `-411 mA`；45.9 s 插回后电量计读 `+50.3 mA`，同次采样 SMB5 仍为 `inhibit-charge`，BQ25970 为 `Not charging`。该读数保留为电量计观测。

## 复核与开放项

- `a91fc2f6` 的默认 pinctrl 拉高了 PM8150B GPIO1。锁定的 LOS Umi DT 将该脚描述为高有效的 `mi,wl-bq-enable`，LOS `qpnp-smb5.c` 初始化拉低，`smb5-lib.c` 在无线快充条件成立时拉高。MiSeed 已移除 GPIO1 声明和驱动动作。旧日志未采集该无线泵的实际工作状态，修复后的镜像待复测。[修复与离线验证](../reviews/2026-11-20-umi-rx-pinctrl-review.md)

- `power-good`（PM8150B GPIO7）上座期间仍读 0，未触发 IRQ；`supply=rx1619` 保持 `online=0`，`voltage_now`/`current_now` 因门控仍不可用。需核对 RX1619 的 power-good 输出条件与 GPIO 电气方向。
- 普通模式下 USB 与无线底座同时接入时，有线 SMB5/PPS 的退出与离座恢复待验。无线净充入、主动限流、温控及故障停机待验，TX 独立。

见 [插拔记录](2026-11-19-umi-rx-nfc-ram-poll.md) 与本轮 dmesg 摘要。

## 收尾版 observe 复验

2026-11-20，维护者授权一次 `fastboot boot`，配合拔线、上座、离座及 USB 重连。Umi/CSOT，启动前 Android 16、电量 66%、26.7°C；Windows x64 / PowerShell 7.6.9。镜像为 `29fd8b4` 加[收尾工作树](../reviews/2026-11-20-pr8-coderabbit.md#l3-接入前修订)，锁定 Linux v6.18.60，`observe` 镜像 SHA-256 `32c7e40fa2000e3656e1ae32c88ff218fa9dd15abb0ae7280e792c3a7bc7654d`。未刷写、未操作 GPIO155 或 GPIO1。使用准备包的 `preflight.py --ram-boot-once observe --label rx-observe-base` 和 `--capture observe --baseline rx-observe-base --label rx-observe-end`，均退出 0。运行时 FDT 硬件子树和 RAM 通过，ABL 根身份仍为已知 `FAIL`。

- RX1619 在 `16-0061` 绑定；初始 `pg=0`。全程 43 组快照均为 `ONLINE=0`，VOUT/IOUT 不可读，无 power-good 边沿记录。绑定和 USB 重连导出通过，PG/IRQ 与 RX 遥测未通过。
- 拔线后与上座操作对应的 48.2–67.2 s，电量计为 `Discharging`、−3.42 至 −0.49 mA；离座区间约 −449 至 −404 mA。上座时 PMIC 读到 `power_path=0x95`、TCPC `CC=0/VBUS_DETECT=1`，同期 TCPM partner 缺席。该结果未证电池净充入；上座窗口不能用 `VBUS_DETECT` 单独判断外部有线接入。当时的共存探针要求 VBUS 未检出。后续离线修订的分阶段判据见[收尾复核](../reviews/2026-11-20-pr8-coderabbit.md#基于-observe-实测的诊断修订)。
- SMB5 `chg_enable=0` 的 86 组读数一致，BQ25970 记录了停泵读回。最终日志同一 boot ID、452 条连续记录、零存储错误，无读取错误或丢记录。原始文件保存在 `.work/pr8-l3-ready-20261120/evidence/rx-observe-end/`，`charge.log` SHA-256 `97114e9be978edc5cecfdb7c860f76620df986db61f0cc37351fb7ef4e791d83`。保全后正常重启，ADB 核对 `umi`、`sys.boot_completed=1`、UID 2000。

未进入 `coex`、双输入或有线充电实验。先复核 GPIO7 的实际输出和 TCPC VBUS 检测归属，继续保持无线控制隔离。

## GPIO7 原始电平与 IRQ 复验

2026-11-20，维护者要求再次上机，约定离座/上座/离座流程，操作者报告 USB 已接回，最初反馈上座时底座灯常亮，随后更正为测试时底座可能未接电；无独立供电确认。Umi/CSOT，起始 Android 16、电量 69%、27.5°C；Windows x64 / PowerShell 7.6.9。使用 `29fd8b4` 加[诊断修订工作树](../reviews/2026-11-20-pr8-coderabbit.md#诊断修订的-l2-与验收包)，锁定 Linux v6.18.60；`observe` 镜像 SHA-256 `b62e56398d1fce45f319c8339031135b08e3c43c893ba3f8b3934dbae37d2343`。仅 RAM 启动一次，未刷写或操作 GPIO155/GPIO1。

- `preflight.py --check`、`coex/run.py --check`、`preflight.py --ram-boot-once observe --label pg-base` 和 `preflight.py --capture observe --baseline pg-base --label pg-end` 均退出 0。运行时 FDT 硬件子树及 RAM 通过，ABL 根身份仍为已知 `FAIL`。最终导出与基线同一启动，连续 1,038 条、零覆盖和存储错误，无 `LOSS` / `ERROR` 记录。日志保留在 `.work/pr8-pg-followup-20261120/evidence/pg-end/`，`charge.log` SHA-256 `ab3dee2d752faddfad1003041752e3a5d7c456cdcab44bf0619bfdcadf8ab2f4`。
- RX1619 在 `16-0061` 绑定。200 条 GPIO7 诊断均为 `raw=0 pg=0 active_low=0 irq_count=0`，无采样错误或 IRQ 边沿；80 组 RX 快照均 `ONLINE=0`，VOUT/IOUT 不可用。159 组有线 SMB5 `chg_enable=0`，BQ25970 有停泵读回且 80 组为 `Not charging`。
- 启动后 94.65–159.36 s 的 62 条 TCPC 样本均为 `0x02`（CC/VBUS 均未检出），同期 TCPM partner 缺席；其中 30 组电池电流约 −469 至 −409 mA、30 条 GPIO7 日志仍为低电平。接回后重新读到 `0x2b`；未重现前次上座伴随的 `0x22` 与放电降至近零。底座可能未供电，故将本轮作为无已确认无线输入的基线保留，不据此定位 GPIO7 故障。
- 结果：RAM 启动、诊断导出、有线禁充、Android 回退通过；无线 PG/IRQ 与输出遥测因底座供电前置未确认，未形成有效验收，不记产品失败；未启动 `coex`。采集后通过已识别 CDC 执行 `reboot`，ADB 确认 `umi`、`sys.boot_completed=1`、UID 2000。下次先用同一底座在 Android 下只读确认无线充电可复现，再做有上/离座时间与底座指示灯记录的 `observe`；PG 上升、下降及 IRQ 未确认前不进入共存验收。

## 确认底座供电后的 observe 复验

2026-11-20，操作者先在同一底座、无手机 USB 接入的 Android 下确认无线充电，随后离座并接回 ADB，配合再次拔线、上座、离座及回接。Umi/CSOT，ADB 确认 Android 完成启动、电量 69%、28.7°C；使用同一宿主、工作树及 `observe` 镜像（SHA-256 `b62e56398d1fce45f319c8339031135b08e3c43c893ba3f8b3934dbae37d2343`）。仅 RAM 启动一次，未刷写、未操作 GPIO155/GPIO1。

- 两项离线预检、`preflight.py --ram-boot-once observe --label pg-powered-base`、`preflight.py --capture observe --baseline pg-powered-base --label pg-powered-end` 均退出 0。运行时 FDT 硬件子树及 RAM 通过，ABL 根身份仍为已知 `FAIL`。同一启动连续 1,245 条 RAM 记录、零覆盖和存储错误，无 `LOSS` / `ERROR`。原始证据在 `.work/pr8-pg-followup-20261120/evidence/pg-powered-end/`，`charge.log` SHA-256 `882dfae126befde1b0ddb744e61d17305d618e01d3e98e96f23ee5ea19471d2b`。
- TCPC 依次出现离座 `0x02`（115.575–136.692 s）、上座 `0x22`（137.751–169.393 s，31 条，CC 未连接/VBUS 检出）、离座 `0x02`（170.448–182.034 s）；上述三段 TCPM partner 缺席，USB 重连后恢复 `0x2b` 与 partner。上座窗口 15 组电池电流约 −3.906 至 −1.464 mA，离座稳定段约 −449 至 −409 mA；无线底座分担系统负载，但电池仍报告放电，未证净充入。上座 `power_path=0x95` 是 PMIC 读回，不单独作为外部有线接入证据。有线 SMB5 的 183 条 `chg_enable=0` 一致，BQ25970 确认停泵，91 组快照 `Not charging`。
- RX1619 在 `16-0061` 绑定。全程 279 条 GPIO7 诊断（上座窗口 56 条）均 `raw=0 pg=0 active_low=0 irq_count=0`，无 GPIO 读错或 IRQ 边沿；92 组 RX 快照（其中上座 15 组）均 `ONLINE=0`，VOUT/IOUT 不可用。**有无线供电迹象时仍重现 PG/IRQ 与遥测未通过**；不能据此区分板级 GPIO7 电气/映射和 RX1619 的 PG 输出条件。
- 未启动 `coex`。采集后已识别 CDC 执行 `reboot`，ADB 确认 `umi`、`sys.boot_completed=1`、UID 2000。下一步使用[只读定位修订](../reviews/2026-11-20-pr8-coderabbit.md#上座有功率迹象但-pg-仍低定位修订)，对照 GPIO7 运行时配置及 RX1619 单寄存器读回；不以 TCPC VBUS 替代 PG，不试改极性或启用 GPIO155/GPIO1。
