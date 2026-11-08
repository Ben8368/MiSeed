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
| RPMh provider 错误 / L3 | 镜像 `2e5ddc0fbd7e5760102989a0bad791d8e4991d48a0ebe606124af08ead3e087a` RAM 启动后，内核明确记录 `qcom-rpmhpd ... probe ... failed with error -95`；provider unbound，I2C15 deferred，PPS controller unavailable。固定 Linux v6.18.60 源码中 `rpmhpd_probe()` 调用 genpd provider 注册，该路径读取 OPP 表；`CONFIG_PM_OPP` 关闭时相应 OF OPP stub 返回 `-EOPNOTSUPP`（数值 -95）。据此锁定配置根因。新修复在源码暂存阶段为 `QCOM_RPMHPD` 补选 `PM_OPP`；ARM64 L2 已通过，新镜像 `9e0654310b385835f535e925f5fb6f9152e84416519ae9c030c2f1a032acfcea` 尚未 RAM 启动。检查时 Fastboot 无设备枚举，未进行启动 |
| 证据级别 | 前三份指定哈希镜像均有 RAM 启动 L3。`-95` 与 PM_OPP stub 对应，支持配置根因；修复镜像目前只有 L2，不能推断真机 provider/I2C/BQ/PPS 已恢复。用户观察仍是 PD 固定档、未见 PPS；充电保护与长期稳定性也未验收 |
| 未覆盖 / 后续 | Fastboot 设备重新枚举后 RAM 启动 `9e065431…acfcea`，只读核实 provider 是否绑定、I2C15 deferred 是否解除及 BQ25970/PPS 状态；再验证设备侧 PD/APDO/PPS、保护与停止恢复。GPIO8 VBUS-disable 行为仍未接入 SMB5 |

充电头侧的“优先 PPS”或屏显 PD 档位不代表设备端已经请求或取得 PPS。原始串口长日志未归档。
