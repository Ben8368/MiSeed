# 2026-11-08 Umi PPS 诊断镜像 RAM 启动

| 项 | 本轮证据 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08，Codex；宿主 Darwin 25.6.0 / arm64 / zsh |
| 授权范围 | 用户明确要求启动最新镜像；仅 Fastboot RAM 启动和救援串口只读查询，不刷写分区、不主动控制充电 |
| 设备 / 变体 | Fastboot 设备 `ec04f57f`，产品 `umi`；项目按 Umi/CSOT 参考配置构建，本轮未重新测面板/触控 ID |
| 仓库 / 输入身份 | `codex/umi-pd-pps-fastcharge`；代码与诊断镜像对应 `a5717e9`；PR 状态记录后续文档提交至 `18c8a65` |
| 载荷 | `.work/boot-l2-arm64/boot.img`，7,806,976 字节，SHA-256 `f13d367585f96486e665219fd956d35447cc20e7a1df8ace167e87241c34c313`；L2 ARM64 镜像，仅 RAM 启动 |
| 前置 / 回退 | Fastboot 识别唯一 `umi` 设备；使用 `fastboot boot` 临时启动。未执行分区写入；本轮结束时设备留在救援镜像，未擅自重启 Android |
| 实际命令 | `.work/host-tools/platform-tools/fastboot devices -l`；`.work/host-tools/platform-tools/fastboot -s ec04f57f boot .work/boot-l2-arm64/boot.img`；在 `/dev/cu.usbmodem101` 只读执行 `status`、`pps-status`、`charge`、`logs` |
| 启动结果 | Fastboot `Sending 'boot.img'`、`Booting` 均返回 `OKAY`；救援 CDC 串口重新枚举并提供命令提示符 |
| Type-C / I2C | `status` 显示 Type-C 类设备 `port0` 与 `port0-partner`；`/sys/bus/i2c/devices` 列表为空，尚无 I2C 适配器或 BQ25970 客户端可见；`pps-status` 返回 controller unavailable or ambiguous |
| 充电遥测 | `charge` 中 PM8150B USB supply 为 `type=USB`、`usb_type=Unknown [SDP] DCP CDP`，`voltage_now≈4.52 V`、`current_now≈0.546 A`；样本显示 USB online，非 PD/PPS 合约证据。电池 SOC 约 62%，温度 35 °C，电池净电流约 -2.1 A，未将其解释为净充入 |
| 内核日志 | 早期启动日志在 boot_ms 3796、4808 记录 PMIC Type-C `Initiating hard-reset` / `isr: tx_sig`；设备随后继续运行并接受串口查询，未据此推断实际掉电 |
| 外部证据 | 用户照片中充电器屏显 PD、5.2 V、2.8 A（约 14.6 W），只证明充电器显示 PD 档位；不能替代设备内核协商状态 |
| 证据级别 | L3：仅覆盖此哈希镜像 RAM 启动、CDC 救援接口、Type-C sysfs 项可见及只读电源遥测。不能据此宣称设备侧 PD 合约、PPS、充电泵、保护或长期稳定性通过 |
| 未覆盖 / 后续 | 定位 I2C15/GENI 控制器未注册与 Type-C hard-reset 原因；I2C15/BQ 绑定后再验证 PD/PPS、保护/温控、停止恢复、拔插与独立专项审查。GPIO8 VBUS-disable 行为仍未接入 |
