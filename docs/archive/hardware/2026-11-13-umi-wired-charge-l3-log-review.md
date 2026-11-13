# Umi 有线充电日志 L3 复核

| 项 | 记录 |
|---|---|
| 日期 / 操作者 | 2026-11-13；Codex 协助维护者复核 |
| 授权范围 | 用户授权最新提交源码的单次 RAM 启动与有线 L3；本节日志导出只读，不执行充电控制或持久存储写入 |
| 设备 / 接口 | Umi/CSOT USB diagnostic CDC，Windows `COM6`（VID_1D6B/PID_0104）；设备身份由采集脚本 banner 确认 |
| 仓库 / 输入身份 | 分支 `codex/wired-six-stage-power`；首轮日志未含镜像哈希，下节延长导出关联到精确 HEAD 与 RAM 镜像 |
| 采集工具 | Python 3.14.2；pyserial 3.5；仓库 `tools/collect_charge_log.py` |
| 命令 | `python tools/collect_charge_log.py --port COM6 --output local/wired-charge-l3-20261113.log --timeout 180`；退出码 0 |
| 载荷 | RAM 日志导出到忽略目录 `local/`，不入库；10,972,665 bytes；SHA-256 `E69F34C3B29B28178A6CACC696C67F69D1CC58BF2C47BC7EBD683C87F712389E` |
| 设备日志元数据 | boot_id `0ac0b7e7-1069-4d1c-9dd1-0bb1f868d423`；seq 0–34111 连续，共 34,112 条；boot_ms 1,483–6,184,366（约 103 分钟）；overwritten=0、storage_errors=0 |
| 证据级别 | 本次接电充电场景 L3 通过；范围为精确镜像、SOC 2%→80% 并在 80% 旁路在线运行，不外推到其他充电器档位或保护故障注入 |

## 实测结果

- `qcom-battery` 2,929 个样本：SOC 2%→80%，电池温度 29.0–35.0°C。`current_now` 是电池侧电流（−518,066 至 +2,764,647 µA），不能当作 USB 输入电流。
- 80% 时出现 `enabled=0 input=1 present=1 soc=80 full_bypass=1`，后续 586 条对应内核记录仍为 `full_bypass=1`；支持确认本次达到 80% 停止电池充电并保持输入旁路。
- 导出序号无缺口；日志头报告 0 覆盖、0 存储错误；无带 `ERROR` 或 `LOSS` 标签的记录。采集只读，无主动保护/故障注入。
- `bq25970-charger` 共 2,928 个 power_supply 样本；`voltage_now`、`current_now` 和 `input_current_limit` 均 unavailable。`usb_type=[C] PD PD_PPS` 表示可见 USB 类型/能力，不能单独证明当前协商合同。

## 验收判定与缺口

| 项目 | 判定 | 证据 / 后续关闭条件 |
|---|---|---|
| 接电充电场景（本次实际用法） | 通过 | 精确绑定的 simpledrm 镜像从 SOC 2% 充至 80%；输入在线，80%自动切旁路且保持在线 |
| SOC 2–10% 固定5V/≤2A | 通过（已观测范围） | SOC 2–10%；输入限额≤2A，USB输入实测最高1.985A。设备从SOC2%开始，未覆盖0–1% |
| SOC 11–65% PPS | 通过（已观测运行） | APDO/PPS遥测和实际IBUS/VBUS可见，请求≤3A；SOC65%无错误进入`PPS_DONE`并释放所有权，随后固定5V继续充电；PPS实测输入峰值14.61W，不宣称30W |
| SOC 71–79% 固定5V/≤2A | 通过（本次日志范围） | 合约/USB读数4.902–4.990V，输入限额1.9–2.0A，实测1.069–1.986A |
| SOC 80% 停止电池充电并旁路 | 通过（本次日志范围） | 在线旁路517.9秒；`enabled=0`、`full_bypass=1`，电池净电流中位−2.4mA；不证明电气隔离 |
| 80%旁路期间电池净耗电 | 通过（本次在线旁路） | 旁路在线517.9秒，250组电池净电流中位−2.4mA（范围−4.9至−0.5mA）；符合本次外部供电旁路时电池基本不耗电的实测。未据此声称电气隔离 |
| 80%→≤71%→5V复充回差 | 本次场景不要求 | 充电器保持接入且旁路稳定，电池净电流接近零，没有触发自然回差；不把人为耗电/等电量下降作为本次接电场景的验收前置条件。断开外部输入后的回差策略若需验收，另作为独立场景 |
| 无APDO固定PD 9V/≤2A及无9V的5V/≤3A回退 | 未验证 | 当前PPS源有APDO；没有用固定档位、无APDO的PD源 |
| 固定5V温度趋势降额/恢复 | 方向通过；温标未验 | BMS温度上升时限额2.0→1.6A，下降时从1.9→2.0A；不证明温标准确或硬件触发 |
| 硬件保护、传感器故障、热暂停恢复、watchdog失败路径 | 未验证 | 未做过温/故障注入；相关保护与独立复核仍开放 |

**本次接电充电场景 L3 验收通过。** 精确关联的镜像在 SOC 2%→80% 完成低 SOC 固定 5V、PPS 请求限额与干净退出、PPS 后固定 5V 续充、SOC 71–79% 固定 5V、温度趋势限流/恢复方向，以及 SOC 80% 停止电池充电并在线旁路；旁路期间电池净电流接近零。验收对象是本次 Umi/CSOT、该镜像和实际连接的充电器/运行条件。无线 RX/TX、其他充电器能力档位和未注入的保护故障不在本次结论内。

## 2026-11-13 同一 RAM boot 延长导出与精确镜像关联

维护者随后接回数据线。未重启或发送充电控制；执行 `python tools/collect_charge_log.py --port COM6 --output .work/wired-l3-head-20261113/round1-charge.log` 导出完整 RAM 日志，并在同一 COM6 会话只读采集 `version`、`mounts`、`status`、`pps-status`、`thermal` 与 TCPM ring。日志 boot_id 与上节相同，延长至 6,943.680 秒；USB CDC 最后重新枚举为 `configured`。导出 SHA-256：`DEEF5681EAB482D725793C3026412C334CD89FA950F6961152F65B84950EA833`，只保存在忽略目录 `.work/wired-l3-head-20261113/round1-charge.log`。

本段设备是同一 RAM boot：准备目录记录当前仓库 HEAD `05c4b7448de5686450c3b22bdd462958bd63476e`、分支 `codex/wired-six-stage-power`；Fastboot 唯一设备核验 `product=umi/unlocked=yes/is-userspace=no`，只执行一次 `fastboot boot`，Sending/Booting 均 OKAY、退出 0。启动载荷为 simpledrm `boot.img`，SHA-256 `87dd5fd013be543b5ce7a66fe4dfde1e041ab38095aa5c5cc04d0c8849a86c31`；构建来源锁 SHA-256 `4afe73b96f2fd63ab8fa28dc97d5d747f1ddcddb61b21838de2c654b9e798906`。控制台报告 Linux 6.18.60，挂载仅 RAM rootfs、devtmpfs、proc、sysfs、debugfs、configfs。未刷写或访问真实存储。

延长快照头部为 `first=0 next=count=36986 storage_errors=0 ram_only=1`，序号 0–36985 连续、未覆盖。累计 uptime 6,943.680 秒（约 1 小时 55 分 44 秒）；起始 SOC 2%，最高 80%，电池温度 29–35°C。主机重接后 USB CDC/SDP 读数只作数据线阶段记录，不作为充电器 PD/PPS 证据。

- **SOC 2–10% 固定 5 V：** 记录覆盖 SOC 2→10%。SMB5 输入限额随 BMS 温度上升由 2.0 A 逐步降至 1.6 A；该档在线 USB 样本输入电流最高 1.985 A。设备从 SOC 2% 开始，0–1% 未覆盖。
- **SOC 11–65% PPS：** `miseed-pps state=3` 持续约 56 分 10 秒；PPS 请求 1.95–3.00 A，实测 IBUS 1.341–1.694 A、VBUS 8.131–8.903 V；同条 VBUS×IBUS 功率中位 13.56 W、峰值 14.61 W。策略目标虽短暂到 30 W，但虚拟热温升至约 38°C 后 18 W `steady` 目标占绝大多数样本；不能宣称实测 30 W。电池温度为 33–35°C，虚拟温度/策略目标不能用作板级温标校准。
- **PPS 提前干净退出：** SOC 65% 时记录 `state=4 reason=0 stop=0 pd_stop=0 owned=0`，随后由 SMB5 固定 5 V 继续充至 70%，PPS 未继续覆盖 66–70%。这是无报错、停止与所有权释放成功的 `PPS_DONE` 状态；日志没有标记具体内部完成分支，故本记录不把退出原因单独归因。
- **SOC 71–79% 固定 5 V：** 该段全部电池样本为 Charging。USB 读数 4.902–4.990 V、1.069–1.986 A；SMB5 限额为 1.9–2.0 A。BMS 温度从 35°C 降至 34°C 时，限额从 1.9 A 恢复到 2.0 A；与低 SOC 升温时 2.0→1.6 A 的降额方向一致，支持本次有线温度趋势反馈行为。此结果不证明温标准确或真实硬件热保护阈值。
- **SOC 80% 旁路：** 首次到 80% 后 `enabled=0 input=1 full_bypass=1`，在充电器保持在线时持续约 517.9 秒。250 组配对样本中电池净电流 −4.9 至 −0.5 mA（中位 −2.4 mA）；固定 5 V 输入约 5.012–5.046 V、0.329–0.586 A，SMB5 限额 1.95–2.00 A。支持本次旁路供系统、停止电池充电的实测范围，不证明电气隔离。

**本轮结论更新：** 精确关联最新提交源码的 L3 已通过 SOC 2–10% 固定 5 V/≤2 A、SOC 11–65% PPS（请求≤3 A、自然 `PPS_DONE` 退出）、SOC 71–79% 固定 5 V/≤2 A、80% 自动旁路，以及固定 5 V 路径的温度趋势限流/恢复方向。旧摘要中“输入侧电压、电流全不可读”的判断对延长导出不成立：本次 PPS 控制日志含请求与泵端 VBUS/IBUS，SMB5 样本含固定输入侧读数。该证据仍不代表实测达到 30 W。

**本次未覆盖（不影响上述场景通过）：** 无 APDO 固定 PD 档位及对应 9 V/2 A、5 V/3 A 回退；热/过流/传感器故障注入、热暂停恢复与 watchdog 失败路径；无线 RX/TX 与 Debian 集成。PPS 在 SOC65% 已自然结束并干净交还固定5V，因此 66–70% 没有继续跑 PPS。TCPM 最终只读导出包含主机 SDP 重接阶段，不以该阶段协议状态补作充电器证据。

原始延长日志、启动守卫与脱敏 Fastboot/状态/TCPM材料均只留 `.work/wired-l3-head-20261113/`，不入库。本次结论只覆盖接电充电实机场景；特定充电器分支、保护注入、无线和系统集成另行验收。
