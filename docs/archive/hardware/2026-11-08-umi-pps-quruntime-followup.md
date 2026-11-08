# 2026-11-08 Umi PPS QUP/RPMh 真机续查

| 项 | 本轮证据 |
| --- | --- |
| 日期 / 操作者 | 2026-11-08，Codex；宿主 Darwin 25.6.0 / arm64 / zsh |
| 授权范围 | 用户要求启动最新镜像；仅 Fastboot RAM 启动与救援串口只读查询，不刷写分区、不主动控制充电 |
| 设备 / 变体 | 同一 Umi 设备；CSOT 参考配置，未重新测面板/触控 ID |
| 仓库 / 输入身份 | 分支 `codex/umi-pd-pps-fastcharge`，输入 `893fd22`；草稿 PR #3 |
| 载荷 | `.work/boot-l2-arm64/boot.img`，SHA-256 `e2ac23c4aefe85aa4a612a18fdc75873a1211f34103f0833d87df8832f88c377`；仅 `fastboot boot` RAM 启动 |
| 实际命令 | `.work/host-tools/platform-tools/fastboot devices -l`；`.work/host-tools/platform-tools/fastboot boot .work/boot-l2-arm64/boot.img`；救援串口只读执行 `status`、`pps-status`、`charge`、`logs` |
| 启动结果 | Fastboot 识别唯一设备；`Sending 'boot.img'` 与 `Booting` 均返回 `OKAY`。随后救援 CDC 串口枚举，设备持续接受诊断命令 |
| QUP / I2C / BQ | 已编译 DTB 中 QUPv3 `qupv3_id_2` 与 I2C15/BQ 节点为 `okay`；设备侧 `/sys/bus/i2c/devices` 仍为空。deferred-probe 明确报告 `884000.i2c platform: supplier 18200000.rsc:power-controller not ready`；BQ25970 未枚举或绑定，`pps-status` 返回 controller unavailable |
| Type-C / PD / PPS | `/sys/class/typec` 有 `port0` 与 `port0-partner`，UDC `a600000.usb` 为 configured。PMIC Type-C 在 boot_ms 3706、4718 各报告一次 `Initiating hard-reset` / `isr: tx_sig`。充电器 supply 多次为 `Unknown [SDP] DCP CDP`；样本约 4.48 V / 0.596 A，未报告 PD_PPS/APDO 或 PPS 合约 |
| 其他遥测 | SOC 约 57%、BMS 温度约 34–35°C；日志中的电池原始电流约 -2.3 A，未把符号解释为净充入。后续日志持续至 boot_ms 192734；未见 PPS 控制器 |
| 后续构建 / L3 | 为只读 `status` 增加 RPMh RSC 与 RPMh power-domain provider 绑定信息；L2 镜像 `108f44749261d7290767a1e2f93f701d89043641dbbaab905cdb46191f38e1ed` 已再次 `fastboot boot` 成功。`pps-status` 显示 RPMh RSC 绑定 `rpmh`，`18200000.rsc:power-controller` 为 unbound；I2C 列表仍空，deferred 原因仍指向该 provider，PPS 控制器不可用。随后 USB 串口消失，未获得该镜像完整 `charge` 快照 |
| RPMh provider 错误 / L3 | 镜像 `2e5ddc0fbd7e5760102989a0bad791d8e4991d48a0ebe606124af08ead3e087a` RAM 启动后，内核明确记录 `qcom-rpmhpd ... probe ... failed with error -95`；provider unbound，I2C15 deferred，PPS controller unavailable。固定 Linux v6.18.60 源码中 `rpmhpd_probe()` 调用 genpd provider 注册，该路径读取 OPP 表；`CONFIG_PM_OPP` 关闭时相应 OF OPP stub 返回 `-EOPNOTSUPP`（数值 -95）。据此锁定配置根因。新修复在源码暂存阶段为 `QCOM_RPMHPD` 补选 `PM_OPP`；ARM64 L2 镜像 `9e0654310b385835f535e925f5fb6f9152e84416519ae9c030c2f1a032acfcea` 已 RAM 启动：Fastboot `product=umi`、Sending/Booting 均 `OKAY`；RPMh RSC 与 power-domain provider 均绑定，I2C15 已枚举，客户端 `15-0066` 绑定 `umi-bq25970-pps`，原 `-95`/deferred 阻塞解除。`pps-status` 为 `state=1 error=0 requested_mv=0 requested_ma=0`（驱动枚举 `PPS_WAIT_PLUG`）；此轮没有 PPS 请求。PM8150B SMB5 supply 快照为 `Unknown SDP DCP [CDP]`、约 4.58 V / 0.506 A，UDC configured。用户拔掉电脑 USB 接充电器后仍观察到固定 PD，未见 PPS |
| 后续属性诊断 | 增加了 rescue `status` 对 `/sys/class/power_supply` 固定白名单属性的只读输出；ARM64 L2 镜像 SHA-256 `8f01d7393e8895d5f38dfc36465150a42cca764935442f8f640f3a3f1e1955c8`，尚未 RAM 启动。它用于确认 TCPM 自身 `tcpm-source-psy-*` 的 `usb_type/online` 和 PDO 电压电流，而非把 SMB5 APSD 的 SDP/DCP/CDP 当作 TCPM 合约 |
| 证据级别 | 四份指定哈希镜像有 RAM 启动 L3，其中 `9e065431…acfcea` 已闭合 RPMh provider 与 BQ25970 的运行时绑定；修复的配置门槛生效。仍无设备侧 PPS 请求/合约证据，用户当前实际显示仍为 PD 固定档。保护/温控完整闭环与长期稳定性也未验收 |
| 未覆盖 / 后续 | 设备重新连到主机并进入 Fastboot 后 RAM 启动 `8f01d739…1955c8`，只读核实 TCPM source power_supply 类型/在线值；按此确认是源未宣告 APDO、TCPC/TCPM 没完成 PD，还是 PPS 属性/策略路径未对接，再修正并再次真机测试。GPIO8 VBUS-disable 行为仍未接入 SMB5 |

充电头侧的“优先 PPS”或屏显 PD 档位不代表设备端已经请求或取得 PPS。原始串口长日志未归档。
