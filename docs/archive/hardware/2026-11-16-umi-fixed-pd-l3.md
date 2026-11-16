# Umi 有线固定 PD 复验：2.240A 采样触发停充

| 项 | 本次证据 |
| --- | --- |
| 日期 / 授权 | 2026-11-16；维护者要求准备最新源码镜像、Fastboot RAM 启动，后报告异常并要求读取充电日志；Codex协助启动、导出、停充及正常回退 |
| 范围 / 设备 | Umi / Mi 10 / CSOT，有线固定PD自然充电；无线RX/TX未操作，PPS未运行。不刷写、不写块设备、不改保护或注入故障 |
| 主机 / 输入 | Darwin arm64 / zsh，Platform Tools37.0.2；工作分支 `codex/fixed-pd-sampling`，HEAD `e875b24cd1955db7a5e0b3d64d63ac8ba0fa322c`；另有主机检查/文档差异，111项产品构建输入与锁一致 |
| 来源 / 配置 | Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；来源锁SHA-256 `e23ffc62a0ab8659e375f8d730d4f98323fdb3c299470b8b3377851745acbda0`；config `3695601c86a22d9ddd44d359d65c9ce0d67e880dd81047e12c9c14e03f22c940`；本轮Colima ARM64重建完成，与既有精确输入镜像一致 |
| 镜像 / 启动 | `.work/boot-l2-arm64/boot.img`，9,728,000字节，SHA-256 `ceafb34bba3fc6e45920cc9cd84fa67e793c0aa00669fae9e7c33cefe7afaeaf`；主机权限下确认 `umi/unlocked=yes/is-userspace=no`；一次RAM启动退出0 |
| 前置 / 局限 | 已备镜像、哈希、采集/停充/回退脚本；维护者在主机基线采集前断开去充电，未取得baseline；回接后取得Linux6.18.60和Umi/CSOT banner及仅RAM/虚拟挂载，补足实际启动证据。未重读底层固件 |
| 结束 / 回退 | `charge-stop`成功，日志含 `CONTROL charge_test_stop rc=0`；正常reboot退出0，ADB只读确认umi、Android16/SDK36、boot_completed=1 |

## 导出与完整性

原始材料留忽略目录 `.work/fixed-pd-sampling-l3-20261116/`，不提交镜像、长原始日志或设备标识。

1. `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/fixed-pd-sampling-l3-20261116/pd-charge.log --timeout 600`，退出0；boot_id=`9942fabc-81a2-4a41-88bd-008bec972122`，first=0、next=count=4580，storage_errors=0、序号连续，boot_ms=1616–919489。
2. 同目录 `capture_console.py --port /dev/cu.usbmodem101 --tag final --commands version status mounts thermal pps-status dmesg tcpm-log`，退出0；TCPM仅显式读取一次，411行、complete=1。
3. 同目录 `control_console.py --port /dev/cu.usbmodem101 --action charge-stop`，退出0；同采集器导出 `closing-charge.log`，退出0，同boot_id、first=0、next=count=4958、storage_errors=0；seq=4881/boot_ms=971101含停止rc=0。随后同控制脚本 `--action reboot`，退出0。

| 文件 | bytes / SHA-256 |
| --- | --- |
| pd-charge.log | 1,380,474 / `488163517c4b71716e2a7986828bb72cf9de5c3238c44aa783320067e8b3add7` |
| closing-charge.log | 1,492,619 / `36ab9fb483ee570aba3a814d9cba997f8e156018df8beb7cc85d17fc7866abd6` |
| final-tcpm-log.txt | 24,248 / `cdcf24c77bf9367b3e23c8db46b85045b2370e2953e95989f50d9b8ec6b02216` |
| final-dmesg.txt | 126,450 / `f147161d5cb1aa639268f8e783f6765fc87ee22484a7383c57535ae055781c87` |

## 时序与判断

充电器在TCPM 61.452877s接入、877.623953s拔线，约13分36秒。广播固定5V/3A、9V/3A、12V/3A、15V/3A、20V/2.25A，无APDO；140.453176s完成9V/2A，约173秒后故障。142–312s内81组SMB5样本：MID电压8.584644–8.648094V、电流1.782220–1.984965A、中位1.883430A；BMS为SOC10–13%、31–33°C。

| 内核时间 | 事件与证据 |
| --- | --- |
| 312.854634s | 故障前power寄存器ICL/AICL/CFG为0x24（1.8A），充电启用、输入未暂停；312.899963s的worker样本为1.363105A |
| 312.975817s | 标准power_supply读取捕获 `input-overlimit measured_ua=2239615 icl_ua=1800000 contract_ua=2000000 source=power_supply` |
| 312.999922s | `fault rc=-75 stop_rc=0 suspend_rc=0`；两路停止成功，日志差约24ms，不当作硬件保护反应时间 |
| 313.026352s | chg_enable=0、phase=disabled、input_cmd=0x01、ICL_STATUS=0，CFG仍0x24；没有改限额抹去异常 |
| 313.191922s | TCPM回到5V/3A；其后至拔线保持停充。boot_ms314000–877000内505组power均input_cmd=0x01，505组charge均enabled=0 |
| 877.623953s以后 | 物理断开才开始新会话；接回电脑后重新充电，与故障保持至拔线一致。电脑窗口892.802034s的hard reset单列，不算充电器窗口故障 |

故障附近BMS为SOC13%、33°C，净电流从约+2.678A变为−0.468A，电压从4.018V降为3.719V。原始2.239615A被保留；源码实际以合约2A判超限，1.8A ICL只作诊断值，不能写成因超过热降额ICL而触发。没有同步外部电流测量，仍不能分辨物理过流或ADC/采样异常；不得放宽预算、改系数或丢弃样本。

维护者随后补报本次目视现象：“功率跳变，小于1w”。因此本轮再次出现功率跳变及低于1W的屏显值；未提供高功率端数值、发生时刻或同步外部测量，不能将每次跳变与312.976s保护事件逐一对应，也不能把该屏显值当作ADC定量校准依据。

**本轮持续固定PD充电验收不通过；标准getter超限检测、停充/输入暂停及保持至拔线再次通过L3。** 故障后保持约9分25秒。充电器窗口无Hard Reset或反复切档，未再现旧重复PD链接告警；固定源 `begin_pps=-95` 是无APDO兼容检查，无泵运行证据。RISK-004继续开放，下一步离线复核输入电流物理通道/换算、ADC及有效ICL时序，以及目视跳功率与故障时序关系。80%旁路/收尾、温标/保护注入、无线RX/TX及Debian未由本次证明。

## 新版 9V/3A + ADC 修复 L3（2026-11-16）

维护者接回 Fastboot 后，本轮使用已准备材料 `.work/pd3a-adc5-l2-20261116/boot.img` RAM 启动；SHA-256 `00594b96d74ee38195d7b4e5546fd2782698fc9c0488506d641284d751fde26b`。`Sending`、`Booting` 均 OKAY。首次 USB CDC 未枚举；重新接回后唯一串口为 `/dev/cu.usbmodem101`，控制台确认 Umi/CSOT、Linux 6.18.60、虚拟挂载，设备在电脑 USB 供电下未充电。

`baseline-charge.log` 是同一 `boot_id=e3b1c13a-376c-4354-8249-d0acf4e24a7b` 的完整 RAM 时间线：first=0、next=count=43,972、storage_errors=0、无覆盖，boot_ms 1,628–7,412,047；13,179,890 bytes，SHA-256 `b10a99d3163bed11234a5c226a7c585e4ddd5b08b5c1d2247148bdb380262611`。记录中 16,692 个 fixed-PD current 样本全部报告 contract=3,000,000uA；测量 218,315–1,985,940uA，ICL 最高2,000,000uA。未发现 ADC 转换错误或 fixed-PD overlimit/fault。电池由10%充至80%，温度约33–43°C。

本镜像仍含已定位的 PPS 准入缺陷。TCPM 记录固定PDO 5/9/12/15V与20V，无APDO；87.891s请求9V/3A。约90.082s日志出现 `miseed-pps failure stage=begin_pps error=-95`，此前 SMB5 临时暂停；同一时刻 fixed-PD 样本降到约0.22–0.28A，约1秒后回到约1.98A，未锁定保护故障。主线 TCPM 把 `USB_TYPE_PD_PPS` 设在发现 PPS APDO 时，因此修复准入应只允许这个类型，普通 `USB_TYPE_PD` 不应先 claim/suspend SMB5。

约4,123.220s、SOC71%、电池温度43°C，策略请求PDO0（5V/3A），TCPM短暂报告9V/277mA standby，约148ms后进入5V/3A；随后SMB5恢复，约1秒样本由0.47A回到1.83A。该SOC阶段按既有策略转5V/10W。SOC80%后 `full_bypass=1` 并停止电池充电，属于既有80%上限。二者不能仅凭低屏显值认定为ADC故障。新镜像物理屏显跳变时刻没有同步记录；亚秒瞬态可能低于1秒采样分辨率。

最终只读状态是连接电脑USB的5V/SDP，未充电；thermal显示 wired-PD cooling state=0。TCPM 环含278条、complete=1；`final-tcpm-log.txt` SHA-256 `4c775ceec7ad0b540065b2785e9133ea3c9542fb57e351d9c42ff8cd26e1941e`。`final-dmesg.txt` SHA-256 `15a15b1bf74fa77212680148d75b9eb1cc59d8228afbf85bba5ad3e5d0dde2fd`。这次运行证明9V/3A合同与ADC输入监测工作、无新增固定PD保护跳闸；它同时复现一次无APDO PPS探测导致的可见输入暂停，以及SOC阶梯降档。普通PD准入修复已落源码和回归，尚未包含在本次镜像；无线RX/TX未操作。

## 无 APDO PPS 准入修复 L2（2026-11-16）

基于同一锁定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 重建，分支 `codex/fixed-pd-sampling`，HEAD `e875b24cd1955db7a5e0b3d64d63ac8ba0fa322c` 加工作区改动。修复要求 TCPM 明确报告 `USB_TYPE_PD_PPS` 才接管 SMB5/启动 PPS；普通 `USB_TYPE_PD`（固定档、无 APDO）直接保持固定 PD，不再短暂 suspend 输入。固定 PD 合约仍可到 9V/3A，充电目标18W、ICL≤2A；有线目标，未触及无线 RX/TX。

新 RAM 镜像 `.work/pd3a-adc5-l2-20261116-r2/boot.img`，9,728,000 bytes，SHA-256 `570b28c4f7275c324d2959be18db5aeed65284e965f2b4534ce3295cfde94105`。配置SHA `3695601c86a22d9ddd44d359d65c9ce0d67e880dd81047e12c9c14e03f22c940`；DTB SHA `0020226ac3b50875cbe876ca1ce229429d4b38798d30aeb85612a1956c61cfba`。Linux 容器下锁定源码复核通过；定向 binding/最终 DTB schema 检查和4项 DT 回归通过；完整 Linux `python -X utf8 -m unittest discover -s tests -v`：280项，零失败、零跳过。macOS 原生套件另跑280项，其中44项因宿主缺 Linux/DT 工具跳过；不替代容器结果。`tools/check_repo.py` 与 `git diff --check` 通过。

新镜像真机 L3 尚未运行。接机包位于 `.work/pd3a-adc5-l3-20261116-r2/`，含单次 Fastboot RAM 启动前置检查、采集/控制脚本与镜像/输入哈希；本轮没有刷写或对设备再次发出启动/断电命令。L3需复验固定无 APDO 时无短暂停顿、可见功率跳变与日志时间线；异常发热、异味、停充或低于1W持续跳变时立即断开有线充电器，USB 控制台保留恢复路径。PPS与无线 RX/TX均不在本次测试范围。
