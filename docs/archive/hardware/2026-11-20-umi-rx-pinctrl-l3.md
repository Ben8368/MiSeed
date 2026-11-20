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

**enable 首轮**：`pin_config_group_set op failed for group 0`；`charger@1000: deferred probe pending: qcom-pm8150b-charger: RX interlock source`。即 PMIC GPIO `power-source = <1>` 在 mainline `pinctrl-spmi-gpio` 中越界（PM8150B 为 LV 单轨，仅 `<0>` 合法），pinctrl 失败连带 `rx1619` 未注册。

**enable 修正后（`a91fc2f6` 镜像）**：dmesg `<6>rx1619 16-0061: RX1619 probed successfully, initial pg=0`。19.0–23.1 s 电池 `-416..-434 mA`、3.957–3.963 V；25.2–41.7 s 上座电压 4.019–4.020 V、电流 `-0.4..-2.4 mA`；43.8 s 离座回落 `-411 mA`；45.9 s 插回后电量计读 `+50.3 mA`，同次采样 SMB5 仍为 `inhibit-charge`，BQ25970 为 `Not charging`。该读数保留为电量计观测。

## 复核与开放项

- `a91fc2f6` 的默认 pinctrl 拉高了 PM8150B GPIO1。锁定的 LOS Umi DT 将该脚描述为高有效的 `mi,wl-bq-enable`，LOS `qpnp-smb5.c` 初始化拉低，`smb5-lib.c` 在无线快充条件成立时拉高。MiSeed 已移除 GPIO1 声明和驱动动作。旧日志未采集该无线泵的实际工作状态，修复后的镜像待复测。[修复与离线验证](../reviews/2026-11-20-umi-rx-pinctrl-review.md)

- `power-good`（PM8150B GPIO7）上座期间仍读 0，未触发 IRQ；`supply=rx1619` 保持 `online=0`，`voltage_now`/`current_now` 因门控仍不可用。需核对 RX1619 的 power-good 输出条件与 GPIO 电气方向。
- 普通模式下 USB 与无线底座同时接入时，有线 SMB5/PPS 的退出与离座恢复待验。无线净充入、主动限流、温控及故障停机待验，TX 独立。

见 [插拔记录](2026-11-19-umi-rx-nfc-ram-poll.md) 与本轮 dmesg 摘要。
