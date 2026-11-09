# 最新 PPS 源码：Windows / WSL L2 与 RAM 启动

## 身份与边界

| 项 | 本轮记录 |
| --- | --- |
| 日期 / 操作 | 2026-11-09；维护者发起，Codex 执行构建和只读连接核验 |
| 用户要求 | 以远端最新源码编译、RAM 启动，由维护者真机充电，再读取日志完善；本轮未授权提交、推送或刷写 |
| 仓库 | fetch 后本地与 origin/codex/umi-pd-pps-fastcharge 均为 `49b1f406dc6333160e0efa69b49384e90bad60f3`；构建前工作区干净，构建后源码无差异 |
| 宿主 | Windows 10.0.26200 / x64 / PowerShell Core 7.6.9；WSL Ubuntu-24.04，Linux 6.6.87.2-microsoft-standard-WSL2 / x86_64 / Bash |
| 工具链 | aarch64-linux-gnu-gcc 13.3.0（Ubuntu 13.3.0-6ubuntu2~24.04.1）；GNU make 4.3；Python 3.12.3 |
| 内核 / 来源锁 | Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；sources.lock.json SHA-256 `344ecdb4391ab27b19337ba18b9d7d4efae87ea4a73f5ed66707bfad83b246c0` |
| 载荷 | `.work/ram-49b1f406-20261109/boot.img`；7,806,976 bytes；SHA-256 `3b5d180062e52ceb8e9c4e8852ae8ccf007d08228c4373858ff2eccdaafdd8bc` |
| 最终配置 | 同目录 `kernel.config`，SHA-256 `41d60f44a5cb03d098d71540cb994868b5ea4b3548b83b1958406974f1bbea0c`；SCSI/MMC、EXT4/F2FS 未启用；PPS、PON、USB ACM 内建 |
| 当前设备 | 唯一 Fastboot；只读返回 `product=umi`、`unlocked=yes`、`is-userspace=no`。本轮尚未运行载荷，面板/原系统身份仅沿用历史记录，未重新验证 |

## 已执行验证

在 `/mnt/c/MiSeed` 执行 `python3 tools/check_repo.py`、`python3 -m unittest discover -s tests -v`（97 项全部通过）、两份构建脚本的 `shellcheck --severity=error` 和 `git diff --check`，均返回 0。随后使用独立源码解包目录和构建目录执行：

```sh
MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/source-49b1f406-20261109 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/build-49b1f406-20261109 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/ram-49b1f406-20261109 bash tools/build_kernel_l2.sh
```

构建返回 0；内核、救援 initramfs、DTB、PPS/FG/SMB5 对象及集成 boot.img 已生成，152 个 ABL 锚点和 241 个目标符号检查通过。构建日志有 `drivers/input/modules.order` 时间戳超前 0.22 秒及 clock skew 警告；最终链接、镜像与哈希检查完成，不声明无警告构建。命令脚本、环境、完整日志、实际配置、对象与镜像均保留在上述忽略目录。

## RAM 启动未执行及恢复方案

已准备哈希、唯一设备、型号及解锁状态检查后执行 `fastboot boot`，但工具自动审批在执行前拒绝：测试供电载荷可能造成启动失败、USB 失联或充电异常，要求维护者了解具体恢复边界后明确确认。因此没有向手机发送本次启动命令，没有新的充电日志，也没有本版 L3；不将旧版短暂 PPS 或 switching 证据转记至本镜像。

后续恢复方案依据[既有 RAM 往返记录](2026-11-08-upstream-power-ramboot.md)，尚未在本镜像验证：

1. 先保持充电器断开，RAM 启动后读取电量、温度、供电状态，再由维护者现场接充电器。出现异常发热或供电异常立即拔掉充电器，不依赖 USB 命令能否响应。
2. 诊断串口可用时，异常后尝试 `charge-stop` 并读回状态，再以 `bootloader` 返回原生 Fastboot，执行 `fastboot reboot` 回原系统。
3. 黑屏或 USB 失联时，由维护者使用电源键＋音量下回 Fastboot；必要时长按电源重启。按键恢复不成功则保持充电器断开并停止测试，不自动升级为刷写、格式化或分区恢复。
4. 正常充电测试结束后接回电脑，先导出 RAM 日志和 TCPM 日志再重启；重启会丢失 RAM 日志，TCPM 读取会消费协议环。

继续前须维护者确认风险与现场按键恢复条件并明确授权本次镜像启动。持续 PPS、完整保护/温控、标度、停止恢复、拔插和 GPIO8 仍待实测及独立复核；本轮未改变供电策略，未提交、推送、发布或合并。

## 明确授权后的 RAM 启动与初始诊断

2026-11-09，维护者阅读上述风险与恢复方案后明确回复“确认授权，启动镜像吧，设备已经连接，且在fastboot模式了”。重新核验唯一 `umi`、`unlocked=yes`、`is-userspace=no`、原镜像完整 SHA-256 和构建退出码。使用本地 platform-tools 37.0.1、以唯一设备为目标执行 `fastboot boot .work/ram-49b1f406-20261109/boot.img`，返回 0；Sending 0.197 s、Booting 0.101 s 均 OKAY。未刷写分区。

Windows 随后枚举 MiSeed VID 1d6b / PID 0104 USB ACM 为 COM6。核验接口和诊断控制台身份后，只读执行 `version`、`pps-status`、`charge`、`status`、`mounts`，均收到完整控制台响应：

- `version` 返回 Linux 6.18.60、MiSeed@Ubuntu2404、GCC 13.3.0，与本次构建版本信息一致。
- `15-0066` 已绑定 `umi-bq25970-pps`；`pps-status` 为 `state=1 error=0 requested_mv=0 requested_ma=0 stop_pending=0`，即等待连接，未请求 PPS。
- 初始电脑 USB 会话最新样本为 SOC 88%、电池温度 300 deci°C（30°C）、BMS VBAT 4,328,602 µV；普通 USB 被识别为 SDP。此处不宣称存在充电器 PD/PPS 合约。
- `charge` 快照 first=123、next=155、count=32、overwritten=0；已校验序列连续和 BEGIN/END 匹配。仅此初始 32 条快照，不当作后续充电器完整时间线。
- 挂载列表只有内存 rootfs 和 devtmpfs/proc/sysfs/debugfs/configfs，无持久块设备挂载。

原始输出保存在同一忽略目录的 `fastboot-boot.log`、`initial-*.log`；Windows 只读串口采集助手为同目录 `read-diagnostics.ps1`。以上证明本镜像 RAM/USB 诊断 L3，不证明持续 PPS 或完整充电保护。已交给维护者用此前同一充电器和线缆短测约 30 秒，随后接回电脑且不重启；后续优先导出 RAM/TCPM 日志，再按实际协商、启泵和退出证据完善源码。本阶段没有发送手动启充、停止或重启命令，尚未采集外接充电器测试结果。

## 充电器实测与 RUN 电压门控修正

上述“待取日志”是交接时状态。其后维护者报告短暂 PPS 再稳定 PD，照片显示 PPS 8.7 V、0.0 A、低于 1 W；照片本身不能证明有效净充电。原镜像同一启动 ID 的保留内核记录显示：97.888 s 启泵，98.355 s RUN 返回 -34（BMS 4154 / 泵 4259 mV）；133.707 s 再次启泵，133.899 s RUN 返回 -34（4127 / 4237 mV）。两次均 switching=1，fault/alarm/bus_low/bus_high=0，随后正常停泵并释放独占；后一次恢复固定 5000 mV / 3000 mA。不是未获得 APDO，而是启泵后软件 RUN 的额外 ±50 mV 门控退出。差值物理原因尚未证明。

原始 `charger1-charge-log.log` 头宣称 first=0、next=967、count=967、overwritten=0，实际有序列缺口及不完整行；**不是完整导出**。此前口头“完整”已纠正，不能把未知期间当成连续证据。关键失败序列 351/484 和 TCPM 原始输出保存在旧忽略目录。维护者已重启回 Fastboot，缺失 RAM 记录不可补取。Windows 采集助手改为 1 MiB 缓冲/1 ms 接收并强制逐条序列及 BEGIN/END 校验；失败保留 `.partial`，旧缺段导出已作为反例验证。

固定 LOS Umi `mi,use-qcom-gauge` 路径在运行调压使用泵 VBAT，没有运行跨芯片 ±50 mV 停泵判据。本地修正保留启泵前门控、各路绝对限值和原保护/回退，RUN 调压及收尾使用 max(BMS VBAT, pump VBAT)，并补充成功采样的泵电压日志。新增实际 worker 合成用例覆盖 ±105/110 mV、±50/51 mV 启动边界、较高读数抑制升压/降压/收尾、带偏差时绝对越限、故障及协商后重采样。针对性 4 项测试通过，完整 L2/新 L3 尚待执行；该修正未提交。新产物与输入补丁使用 `.work/ram-run-vbat-20261109/`，不覆盖基线证据。

## RUN 修正版完整构建与连接检查

2026-11-09 再次 fetch，远端仍为 `49b1f406dc6333160e0efa69b49384e90bad60f3`。新镜像是该基线加本地未提交修正，不能标为纯远端 HEAD；输入补丁 SHA-256 `1d558633fac662b2ed187f295bec92b39617150a16ecb2ca912277d5f4cf80b6`，逐文件输入保存在新忽略目录 `source-inputs.json`。构建后已重新核对源文件哈希与构建日志一致。

- 全部 97 项 Linux 回归、仓库检查、ShellCheck error 级和 diff 检查通过；实际 worker 用例增加多个边界与故障断言，测试计数仍为 97。Windows 采集校验接受既有完整 32 条快照，拒绝旧缺段日志及五类损坏帧。
- 完整 L2 返回 0。旧源码缓存 `drivers/pmdomain/qcom/Kconfig` 不符合锁定源验证，构建器保留旧目录，改用从固定内核提交重新解包的临时源码和干净构建目录；没有绕过校验。此项产生缓存回退警告，未发现编译器警告或 clock-skew 警告。
- 新 `boot.img` 7,806,976 bytes，SHA-256 `a9c1af3409c303876a48d04c2e84ca683e2155e29599baed43164136ba530cb1`。最终 `resolved.config` SHA-256 `41d60f44a5cb03d098d71540cb994868b5ea4b3548b83b1958406974f1bbea0c`。
- 内核仍为 Linux 6.18.60 / `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，GCC 13.3.0；PPS 对象 SHA-256 `7de52579c9bec795d97e46aebe987ad6a49f3da1ed4b12273ded971b5b7ba69b`。完整输出、状态与校验摘要在 `.work/ram-run-vbat-20261109/`。
- 维护者报告已进入 Fastboot，但 Windows 启动前多次枚举均为 0 台设备；受检查门控阻止，尚未执行该镜像的 `fastboot boot`，也没有新 RAM/充电 L3。已提示重新插接数据线；保持已有 RAM 启动授权和上述恢复方案，待实际连接恢复后继续。未刷写、提交、推送或合并。

## RUN 修正版 RAM 启动与测试交接

2026-11-09，维护者再次确认手机处于 Fastboot，连接恢复。启动前唯一设备返回 `product=umi`、`unlocked=yes`、`is-userspace=no`；核验构建状态 0 和镜像完整 SHA-256 `a9c1af3409c303876a48d04c2e84ca683e2155e29599baed43164136ba530cb1` 后，按已有明确授权执行 `fastboot boot`。Sending 0.198 s、Booting 0.101 s 均 OKAY，总计 0.304 s，退出码 0；没有刷写。

Windows 枚举 USB VID 1d6b / PID 0104 的 COM6。只读采集 version、pps-status、charge、status、mounts：Linux 6.18.60；15-0066 绑定 umi-bq25970-pps；PPS state=1、error=0、requested_mv/ma=0、stop_pending=0。电脑 SDP 会话最新电池样本 SOC 87%、290 deci°C（29°C）、VBAT 4,334,217 µV；不把电脑会话当作充电器 PD/PPS 证据。挂载列表仅 rootfs/devtmpfs/proc/sysfs/debugfs/configfs，没有持久块设备挂载。

新启动 ID `0f1726ed-ff48-4a0e-aa5d-47be3e52ddc3`；初始 charge 快照 first=38、next=70、count=32、overwritten=0，经采集助手及独立读回核实序列连续、BEGIN/END 一致。证据为新忽略目录的 fastboot-boot.log/status、initial-*.log、initial-verification.json；仅此 32 条初始快照已确认完整，不替代后续充电器导出。

已交维护者使用之前同一充电器/线缆短测约 30 秒，异常发热立即拔掉充电器，结束后接回电脑且不要重启；随后先导出 TCPM/RAM 日志，再评估持续调节、净充电及退出。本次仅证明修正版 RAM/USB 诊断 L3，充电器效果与完整保护仍未验收。没有发送手动启充、停充或重启命令，没有提交或推送。

## RUN 修正版充电实测与 Windows 完整导出

2026-11-09，维护者完成同一充电器/线缆测试并接回电脑，报告 PPS 约 5 秒后回 PD；照片显示 PPS 8.8 V、0.7 A、6 W。手机保持上述同一 RAM 镜像和启动 ID，没有重启。本阶段仅发送只读诊断和日志命令，没有手动启充、改变保护参数或刷写。

- Windows 原 PowerShell 采集 `charger1-charge-log.log.partial` 声称 613 条、实际仅 591 个完整记录且有序号缺口，已拒绝作为完整导出。改用 pyserial 3.5、1 MiB 接收缓冲及连续读取后，`charger2-charge-log.log` 为 first=0、next=874、count=874、overwritten=0。
- 纳入仓库的采集器再次实际执行 `python -X utf8 tools/collect_charge_log.py --port COM6 --output .work/ram-run-vbat-20261109/charger3-charge-log.log --timeout 30`，退出码 0；first=0、next=2517、count=2517、overwritten=0。两份完整导出均由共享解析器独立读回核对，启动 ID 均为 `0f1726ed-ff48-4a0e-aa5d-47be3e52ddc3`；摘要为同目录 `charger-export-verification.json`，原始长日志仅本地保留。
- 完整记录中 67.564996 s 启泵；67.759566 s RUN 的 BMS 4126 / 泵 4234 mV（+108 mV）继续运行，未重现原 -34 门控故障。共 33 条 RUN 样本，末条 78.240202 s；后段 BMS 4372–4374 / 泵 4373–4376 mV，电池净充电约 1068–1081 mA。VBUS 8660–8821 mV、IBUS 645–806 mA，热计算值约 33.7–34.1°C、thermal_level=0。
- 78.633102 s `state=4 reason=0 stop=0 pd_stop=0 owned=0`，属于 PPS_DONE，未见 PPS failure；启泵记录至完成回退为 11.068106 秒，不能等同于仪表显示时长或精确导通时间。已保存的 TCPM 输出在 78.502487 s 请求固定 5000 mV / 3000 mA，78.632858 s 接受并返回 SNK_READY；这只是合约上限，不是实测 3 A。TCPM 读取会消费环形日志，本文件无逐条连续编号，不单独宣称整份协议时间线完整。

退出原因是基于代码和完整日志的推断：启动 SMB5 的 `fv_uv=4400000`，驱动保留低于板级上限的继承 FV。RUN 以较高 VBAT 达到 FV−80 mV（本次 4320 mV）且电池净电流低于 2200 mA、连续 10000 ms 后进入 PPS_DONE；从 68.427982 s 起的采样和完成时刻吻合。SOC 未到 95%、绝对 VBAT 未越 4400 mV，未见原电压差错误；固件没有独立的 taper 原因字段，不把该推断写成直接打印的原因。固定 LOS 参考同样使用 80 mV / 2200 mA / 10 秒收尾条件，因此本轮不为延长 PPS 放宽阈值。

本轮改动仅为宿主采集工具与回归：新增 Windows 明确 COM 选择、USB VID/PID 和诊断 banner 检查，复用跨平台严格帧解析；缺段/乱序/错误尾帧/异常行/断线/超时均失败，保留 partial 并禁止覆盖原文件，最终文件独占发布。Windows 需可选 pyserial，本机已有 3.5，没有安装依赖。没有修改此前固件输入补丁、构建产物或供电源码；镜像哈希与 L2 身份沿用上文。

验证：Windows Python 3.14.2 的 `python -X utf8 -m unittest tests.test_charge_log tests.test_charge_log_windows -v` 共 15 项，10 通过、5 项 POSIX/C 编译器用例跳过；WSL Ubuntu-24.04 的 `python3 -X utf8 -m unittest discover -s tests -v` 共 106 项全部通过，退出码 0，输出保存在同忽略目录 `collector-regression-linux.log`。新增 9 项用例覆盖逐字节拆包/空读、帧损坏、超时/容量上限、串口身份、完整发布、失败 partial 与不覆盖。实际 COM6 导出退出码 0。`python -X utf8 tools/check_repo.py` 与 `git diff --check` 均退出码 0；`shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` 退出码 0；固件源码/锁文件和镜像哈希再次核对一致。

本次证明短时 PPS 有电池净充入且正常回固定 PD，不证明低电量持续 PPS、完整温控/故障恢复或各路标度准确。下一轮在维护者回原系统正常使用降低电量后，用同一充电器/线缆记录起始 SOC/VBAT，复用已核验镜像测试；保持日志导出先于重启。未提交、推送、合并或重新启动手机。

## 78% 电量再次 RAM 启动

维护者随后报告当前电量 78%、已连接 Fastboot，继续此前同一镜像的较低电量测试。宿主仍为 Windows x64 / PowerShell 7.6.9。核验唯一 Fastboot 设备 product=umi、unlocked=yes、is-userspace=no，完整镜像 SHA-256 `a9c1af3409c303876a48d04c2e84ca683e2155e29599baed43164136ba530cb1`、build.status=0 及构建输入哈希一致后，按已有 RAM 启动授权执行 fastboot boot：Sending 0.197 s、Booting 0.102 s 均 OKAY，总计 0.304 s，退出码 0。没有重新编译或修改供电策略，没有刷写。

本轮证据单独保存于 `.work/ram-run-vbat-20261109/soc78-round2/` 的 preflight.json、fastboot-boot.log/status。启动后曾枚举到 VID 1d6b / PID 0104 的 COM6；随后打开诊断端口时设备已不在枚举列表，身份门控拒绝读取，未发送诊断命令。故 78% 仅为维护者报告，尚无本轮启动 ID、VBAT、温度或 PPS 状态，不能沿用上一轮样本。连接断开原因尚未确认，不能据此认定手机已经连接充电器或充电成功。

已提示如已换接同一充电器/线缆，短测约 30 秒后接回电脑且不重启，异常发热立即拔掉充电器；恢复路径沿用本记录已授权方案。本轮等待维护者完成物理测试，再先导出 RAM 日志核实持续 PPS、净充电与退出。文档检查及 git diff --check 在本轮收尾复核；未提交、推送或合并。

## 78% 报告电量下的带载 VBUS 退出与修正

维护者接回设备并报告仍约 5 秒 PPS 后回 PD。本轮使用 `python -X utf8 tools/collect_charge_log.py --port COM6 --output .work/ram-run-vbat-20261109/soc78-round2/charger1-charge-log.log --timeout 30` 完整导出，退出码 0：boot_id=`8bd4423a-fbd7-47ef-90ed-1425afd8d268`、first=0、next=381、count=381、overwritten=0；独立读回再次校验通过。另保存 pps-status、tcpm-log、version、status；协议日志为消费式读取，不能单独证明完整。原始日志与分析 JSON 仅保留在该忽略目录。

设备首条 SMB5 样本 SOC 79%、35°C、VBAT 4.150868 V，继承 FV 4.40 V；78% 是维护者测试前读数，两者不混同。22.224367 s 启泵，21 条 RUN 记录显示净充电从负值升到 1.601 A；29.332325 s 故障样本净充电 1.796 A、BMS 4271 / 泵 4285 mV、VBUS 8676 mV、IBUS 1199 mA，fault/alarm/bus_low/bus_high=0、switching=1，返回 -34。29.490938 s 完成 PPS_FAULT 回退且 stop=pd_stop=owned=0。启泵至故障 7.107958 秒，至回退完成 7.266571 秒；不把它等同于仪表显示时长。

TCPM 在 29.305862 s 完成 8880 mV / 3000 mA 合约，而随后泵端 ADC 8676 mV 比请求低 204 mV，越过旧 RUN 的 request−200 mV 下界；电池尚未达到 4320 mV 收尾区。本轮明确是下向 VBUS 门控故障，不沿用上一轮正常收尾结论。29.490704 s TCPM 恢复固定 5000 mV / 3000 mA 合约；这不是实测 3 A。故障前差值随电流逐步增加，与带载路径压降相符，但连接/线缆/采样各自贡献未确定，不能据此猜电阻或电压补偿值。

重新核对固定 LOS `71b13e62f057a649b77fe4062feb73ee72ad609c` 的 pd_policy_manager.c/.h 与锁文件 SHA-256 一致；其 RUN 以电池/总线电流、VBAT 及硬件状态调节，并无 source request 与 pump ADC 的固定下向 200 mV 停泵规则。修正仅让该下向一致性检查留在启泵前，运行继续保留 7.0–9.5 V 绝对 VBUS 范围、request+200 mV 上界、各路 VBAT/电流/温控、switching/bus_low/high、保护寄存器及精确合约校验。没有调高任何电气阈值或放宽收尾；下向门控作用域改变属于供电控制修改，尚须新 L2/L3，不能凭来源证明真机安全。

故障日志新增 request_mv/request_ma/max_bat_mv，避免下轮只能从 TCPM 关联判断。实际 worker 合成测试覆盖启泵 200/201 mV 边界、运行 200/201/204/282 mV 差值的请求前/后采样、实测 8880/8676 场景，以及带差值时的欠压、过压、过流、过温、泵停止、总线故障和合约失配；另覆盖协商过程中出现新总线故障或新越限的停止。针对性 `python3 -X utf8 -m unittest tests.test_pps -v` 4 项通过；完整回归和 L2 另记后续结果。新产物目录 `.work/ram-load-vbus-20261109/`，不会覆盖上一轮镜像或输入补丁。本轮尚未重启手机。

新修正的完整 Linux 回归 `python3 -m unittest discover -s tests -v` 共 106 项全部通过（20.611 秒），仓库检查、ShellCheck error 级及 diff 检查在构建前均通过。反向验证在临时头文件中恢复旧 RUN 下界后，真实 worker 的 8880/8676 场景断言失败；当前修正通过同一用例，证据为新忽略目录 old-gate-regression.json，不修改工作区。完整 L2 由新目录 run-tests-build.sh 发起，固定 source-load-vbus-20261109 / build-load-vbus-20261109 路径与独立输出，结果待完成核验。

## 带载 VBUS 修正版 L2 与 RAM 启动

完整构建脚本退出码 0，镜像 7,806,976 bytes，SHA-256 `7da71525906043a03b93c202ca5c2a7e31f22a146c3bc2d481f18be1f980f619`；PPS 对象 SHA-256 `a46c95467f145f5bcd54cbf0e801fc8ff4742e8774ac2580c9ca02deb82de078`；resolved.config SHA-256 仍为 `41d60f44a5cb03d098d71540cb994868b5ea4b3548b83b1958406974f1bbea0c`。基线仍 `49b1f406dc6333160e0efa69b49384e90bad60f3` 加未提交修改，输入补丁 SHA-256 `531348c5985958d0ac0cc1ae5adafcfd6a206fb595d58af4616e27049ebb8e60`；构建前后源码输入哈希一致。Linux 6.18.60、固定内核 revision 和 GCC 13.3.0 不变，DTB 锚点/目标符号校验通过。

首次 make 在 include/config/auto.conf 上报告最多约 1 秒未来时间戳及 clock skew 警告；没有隐去警告或声称首轮无警告。在同一源/构建目录和 KBUILD 身份环境重新执行 `make -C /tmp/miseed-boot-l2/source-load-vbus-20261109 O=/tmp/miseed-boot-l2/build-load-vbus-20261109 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j12 Image qcom/umi-csot.dtb modules`，退出码 0、无 clock skew，前后 Image/DTB/PPS 对象/config 哈希全部一致，原打包镜像仍对应相同输入。证据为新忽略目录 timestamp-recheck.log/json、source-inputs.json、verified-outputs.json、build.log/status。

读取旧镜像日志后手机连接状态改变：COM6 不再枚举，Fastboot 唯一设备出现（Agent 未发送 bootloader/重启命令）。按此前已明确授予且持续有效的 RAM 测试授权，重新核对 product=umi、unlocked=yes、is-userspace=no、构建退出码及新镜像完整哈希，执行新镜像 fastboot boot：Sending 0.202 s、Booting 0.100 s 均 OKAY，总计 0.308 s，退出码 0。没有刷写。boot-preflight.json 和 fastboot-boot.log/status 保存在新忽略目录。

启动后等待 15 秒未枚举到 MiSeed USB 诊断口，未发送诊断或充电命令；尚无新版启动 ID、电量、温度或充电 L3。不能以 Fastboot 返回成功代替诊断系统可用和持续 PPS 的证明，也不能由 USB 不可见推断已经连接充电器。已提示如维护者换接充电器，按同一充电器/线缆短测约 30 秒后接回且不重启，异常发热立即断开；恢复边界沿用已授权方案。下一步验证是否跨过旧 204 mV 压差退出点及后续限制。未提交、推送或合并。

## 带载修正版 PPS 中断与自动重连调查

维护者报告可持续约 30 秒，短暂停止后会重新进入 PPS；照片显示 PPS 9.0 V / 1.3 A / 12 W，仅代表仪表显示，不替代电池净充电。只读枚举 COM6 并采集本镜像日志，没有发送充电控制、重启、Fastboot 或刷写命令，没有改固件。version 返回既有固定 Linux 6.18.60 构建身份。

前两次 `python -X utf8 tools/collect_charge_log.py --port COM6 --output .work/ram-load-vbus-20261109/charger{1,2}-charge-log.log --timeout 30` 均因序列缺口被正确拒绝，保留 .partial；第一份在 seq=289 附近截断记录，不能视为完整。使用同一已验证诊断身份、1 MiB 缓冲，先连续读入原始字节至提示符，再用共享严格解析器校验，第三份 charger3-charge-log.log 成功：boot_id=`681f7c26-6963-481a-ba88-88199edba84f`、first=0、next=841、count=841、overwritten=0。不能据此声称 Windows 导出偶发缺段已彻底修复。完整原始字节、失败副本、pps-status/version、协议日志及分析 JSON 均仅保留于同一忽略目录。

| 段 | 内核启泵时间 → 故障时间（秒） | 间隔 | 退出 | 证据边界 |
| --- | --- | --- | --- | --- |
| 1 | 10.802646 → 18.808486 | 8.005840 秒 | -71 / EPROTO | 首次协议复位链条可见，随后约 1.70 秒重新启泵 |
| 2 | 20.509903 → 41.781838 | 21.271935 秒 | -71 / EPROTO | 后段 TCPM 已丢失，不能认定与首次完全相同；随后约 1.69 秒重新启泵 |
| 3 | 43.470524 → 58.414575 | 14.944051 秒 | -107 / ENOTCONN | 连接丢失，随后约 1.64 秒重新启泵；物理/协议起因未分清 |
| 4 | 60.053600 → 62.619873 | 2.566273 秒 | -71 / EPROTO | USB 输入转为离线，可能包括维护者拔充电器接回电脑的操作，不能直接算自动中断 |

四段分别有 23/64/45/8 条 RUN 样本。第二段最大电池净充电 2245 mA，实测请求与泵端 VBUS 差最大 221 mV；旧 -34 门控未重现。首次故障前/故障样本 fault/alarm/bus_low/bus_high=0、switching=1，净充电 1945 mA、BMS 4336 / 泵 4342 mV；第二次故障样本净充电 2118 mA、BMS 4369 / 泵 4374 mV。此处证明短时有效充电和重连，不能声明稳定持续 PPS 或硬件无任何未记录瞬态。

首次协议链条：18.762250 s 请求 APDO 9020 mV / 3000 mA（控制器故障记录的 9000 mV 是上一成功请求）；18.808269 s TCPM 收到 header=0x0；18.808274 s 报 Data role mismatch；18.808276 s 进入 ERROR_RECOVERY；18.808281 s 报 Received hard reset；18.808285 s 进入 PORT_RESET；18.850805 s VBUS off。随后 CC/VBUS 恢复、重新读取源能力、固定 PD 协商与 PPS 请求，20.509903 s 再次启泵。首次停顿在 PD 协议/连接层触发，并非旧固定压差门控或 PPS_DONE 收尾。接收硬复位不等于已证明充电器主动故障：本机 PHY 中断和报文处理仍需核实。

协议文件 charger1-tcpm-log.log 保存开头至约 25.58 秒的记录，并有 68.401531 s 的 overflow。固定内核 TCPM LOG_BUFFER_ENTRIES=1024，记录器满时丢弃后续记录；再次 tcpm-log 输出为空。故第二/三次中断前后的协议上下文已经不可补取，841 条完整 RAM 充电日志不等于完整协议日志。本次文件没有 console 的 1024 行截断提示；根因是内核环形缓冲溢出，不能误写为主机文件读取截断。

源码核查另发现确定的错误路径：固定内核 qcom_pmic_typec_pdphy_pd_receive() 在 RX_SIZE 无效（<1 或 >29）时 goto done，但 ret 保持成功，msg 尚未由 regmap_bulk_read 填充，仍会调用 tcpm_pd_receive。使用实际函数和合成 regmap 在 WSL 隔离复现：size=0/30 均 read=0、forwarded=1、ack=0；size=1/29 正常读取并转交。源码 SHA-256、命令助手与结果保存在 investigate-pdphy-rx.py / pdphy-invalid-rx-investigation.json。此为 L1 合成证据，不证明本次 header=0x0 来自该路径；当前缺少实际 RX_SIZE/STATUS 与信号复位诊断，不能跳过证据直接忽略零头报文或放宽角色检查。

本轮仅完成只读调查与权威记录更新。后续应修复已复现的无效接收错误路径、补充 RX/信号复位诊断并解决协议保留窗口，然后新 L2/L3 对照。供电阈值及协议角色校验保持不变；没有新镜像构建、部署、提交或推送。不要把缺失的后段协议原因补写成已确认结论。

## PD 接收修正版完整 L2 与产物复核

接续前述调查，已修复离线复现的无效 RX 长度错误路径和确认失败转交，增加复位诊断、8192 条协议缓冲与 Windows 有界先收后校验，详见[来源与修正记录](../research/2026-11-09-pd-receive-guard.md)。基线仍为 `49b1f406dc6333160e0efa69b49384e90bad60f3` 加未提交修改；独立目录 `.work/ram-pd-rx-20261109/` 保存构建证据。Linux/WSL 全部 116 项回归通过（20.302 秒）；`bash tools/build_kernel_l2.sh` 完整 L2 退出码 0，环境为 Ubuntu-24.04 / WSL x86_64、Python 3.12.3、ARM64 GCC 13.3.0，内核 revision 不变。

镜像 7,811,072 bytes，SHA-256 `0d0ab728a20e78f71e907e4333c714ead3b313b835aca0e5b6bab49bdd4f3b5e`。PD PHY 对象 SHA-256 `a1c95091df3aac6d00613d9443a2d7b04cbb80645cc53eb2cb54d2a2f15dd483`；配置及 PPS 对象哈希与前轮相同。保存的输入补丁 SHA-256 `d21e1de854c10bdd6bdc8f0d3882116858a21cc35c387623167f4e9a564fa9e1`；按 source-inputs.json / verified-outputs.json 复核基线、保存补丁、12 项源码输入和 5 项输出全部匹配。首次编译短暂出现 clock-skew 警告，同构建树复查退出码 0、无该警告，Image/DTB/配置/PPS/PD PHY/TCPM 对象前后哈希不变，证据为 timestamp-recheck.json。新镜像未做 RAM/L3，不能推断持续 PPS 已稳定。

## Windows 原生 C 编译器补齐与复跑

维护者正在使用手机耗电；本轮仅操作本机工具与离线测试，没有连接、重启或控制手机。宿主 Windows x64（10.0.26200）、PowerShell 7.6.9、Python 3.14。原测试因 PATH 找不到 `cc` 跳过，现从 [w64devkit 官方 v2.11.0 发布](https://github.com/skeeto/w64devkit/releases/tag/v2.11.0) 获取 `w64devkit-x64-2.11.0.7z.exe`，下载 SHA-256 `54c2bb9962e8a30c8270e34c5efc3d5fee579f53087fa7d435b95fbba2af7b2b` 与 GitHub release asset digest 匹配，发布元数据保存为 `.work/tools/w64devkit-2.11.0/release.json`。归档及工具链保留于忽略目录，不纳入源码或固件分发。

解压后的 `cc.exe` 位于 `C:\MiSeed\.work\tools\w64devkit-2.11.0\w64devkit\bin`，版本 GCC 16.2.0，目标 x86_64-w64-mingw32。只为测试进程加入 PATH，不修改系统或用户环境；复跑命令见[主机工具](../../HOST_TOOLS.md#windows-原生-c-离线测试)。`python -X utf8 -m unittest tests.test_pd_diagnostics tests.test_charge_log_windows -v` 退出码 0，18 项全部通过、零跳过（4.863 秒），日志保存在 `.work/ram-pd-rx-20261109/windows-tests-native-cc.log`。此前跳过的实际接收函数测试已原生编译并执行，覆盖 0～255 长度、各步 I/O 和 ACK 失败、短报文尾部初始化与真实零头传递。该结果是 Windows L1，手机内核仍使用既有 ARM64 工具链构建；原中断与已修复错误路径的因果关系继续待新 L3 核实。

## PD 接收修正版首次 RAM 启动：等待屏幕确认

维护者确认设备已准备好并进入 Fastboot，延续此前明确 RAM 测试授权和上述恢复方案。启动前重新核对唯一设备 product=umi、unlocked=yes、is-userspace=no，构建退出码 0、12 项源码输入、5 项产物及基线均匹配，镜像 SHA-256 为 `0d0ab728a20e78f71e907e4333c714ead3b313b835aca0e5b6bab49bdd4f3b5e`。第一次误从旧产物目录调用启动助手，被已有证据保护在发送任何设备命令之前拒绝；将已审阅助手复制到新产物目录后执行，实际只发送一次新镜像 RAM 启动命令。

`fastboot boot` 返回 0，Sending 0.201 s、Booting 0.102 s 均 OKAY，总计 0.309 s；没有刷写分区。证据保存在 `.work/ram-pd-rx-20261109/boot-preflight.json`、fastboot-boot.log/status。随后诊断端口未出现；再次只读检查 Fastboot 与 ADB 均无设备，串口仅 COM1，未见对应 MiSeed/Android/Xiaomi USB 设备。尚未获得 version、pps-status、电量或温度，没有发送充电控制命令。状态仅证明 Bootloader 接受载荷，不证明 Linux 已启动；停止自动启动重试，待维护者提供屏幕情况，保持充电器断开，必要时沿用现场按键恢复方案。

## PD 接收修正版充电 L3：启泵前协议回退

维护者报告“这次是完全没有pps”并将设备接回电脑；仅执行只读身份核验、`charge-log`、`version`、`pps-status`、`status` 和一次 `tcpm-log`（此命令消费内核 TCPM 环）。没有重启、刷写或下发充电控制。COM6 VID:PID=1d6b:0104 返回预期 MiSeed 问候；version 为 Linux 6.18.60。充电日志 `boot_id=5c8829cc-01ef-45df-9251-54f6fea8426c`，first=0、next=322、count=322、overwritten=0，完整帧校验成功；TCPM 导出 292 行、limit=8192、complete=1。原始证据仅保存于 `.work/ram-pd-rx-20261109/charger1-*`，未提交长日志。

18.252 秒收到六组 Source PDO，包括 5–11V/5A APDO；18.405 秒首次进入 5V/3A PPS，18.528 秒收到 8.42V/3A 请求的 PS_RDY。18.529 秒请求同电压改为 2.05A，18.531 秒收到 header `0x07b6`（零数据控制报文类型 `0x16`，此固定内核 PD 定义将 22–23 保留），TCPM 在 `SNK_NEGOTIATE_PPS_CAPABILITIES` 中进入 SOFT_RESET；随后重取源能力并退回固定 5V。约 19.000 秒 PPS worker 报 -71/EPROTO，故障时 `switching=0`、VBUS/IBUS=0、泵未启用；回退状态 `stop=0 pd_stop=0 owned=0`。此处只是短暂 PPS 合约协商，不能称为稳定 PPS 快充。接回电脑后的 `pps-status` 为 state=1、error=-71，是历史错误值而非当前正在 PPS。

没有出现 `invalid_rx_size`、`rx_error`、`zero_rx_header` 或 `rx_reset_signal` 记录，不能据此证明报文来源、所有 PHY 字节和充电器行为均正确；`0x16` 可能涉及链路/接收异常，不能直接认定充电器故障。保留控制报文触发 TCPM 原有软复位，属于保护性回退；不绕过校验，也不放宽电流/温控。USB 重新连接电脑后的 43–45 秒另有源能力等待超时和发出硬复位，属于另一连接，不混为充电器中断根因。SOC 样本 74%、温度 39°C；后续电脑 USB 样本电池净充电约 2.47A 不能计作 PPS 泵电流。待分析为何第二次电流请求收到该报文及是否可安全减少协商，不在根因不明时强制持续 PPS。

## 同镜像再次连接充电器：约 82 秒启泵

维护者报告断开 USB、重新连接充电器后 PPS 稳定，随后接回诊断口。只读导出 `charger2-charge-log.log`：与首轮相同 boot_id `5c8829cc-01ef-45df-9251-54f6fea8426c`，first=0、next=1756、count=1756、overwritten=0，校验连续和结束帧；其中含首轮旧记录。之后读取 version、pps-status、status，最后单次读取消耗式 `tcpm-log`，`charger2-tcpm-log.log` 为 4946 行、limit=8192、complete=1。没有重启、刷写或发送充电控制；原始文件均留在忽略目录 `.work/ram-pd-rx-20261109/`。

本次 304.822 秒请求 8.42V/2.05A 时 PD TX complete status=1，紧接收到 header `0x07b6`（保留控制类型 0x16）；与首轮不同，TCPM 发送 Not Supported 并于 304.847 秒重试，304.877 秒收到 PS_RDY 完成 8.42V/2.05A 合约。约 305.142 秒启泵；305.341 至 387.290 秒的 250 条 RUN 记录连续，峰值电池净充电 3.263 A、泵输入 1.895 A，请求电压 8.44–9.22V。用户报告期间未见 PPS 退出；日志内没有早于末尾断线的泵故障、角色错误、接收硬复位或新增 PD PHY 接收错误。约 387.471 秒 CC 变化、387.478 秒断开/VBUS off，紧接 387.481 秒泵 fault=1、switching=0、-34 回退；顺序与用户拔线相符，不能单独归因于泵内部故障，也不能证明拔线是唯一原因。接回电脑后的 `pps-status` state=1、error=-34 是最后故障值，不代表当前仍在 PPS。

该 L3 证明本次合约、启泵和约 82.3 秒短期连续调节，不证明长期稳定或完整保护/温控已验收；保留类型 0x16 和发送失败来源仍开放。协议环中约 254 次 APDO 请求多为运行期间约 0.3 秒一次，后续应评估调节频率及其对链路稳定性的影响，不能为了维持 PPS 而忽略异常控制报文或削弱保护。

准备 PR #3 收尾时，在 Windows x64 / PowerShell 7.6.9 重跑 `python -X utf8 -m unittest discover -s tests -v`：116 项、退出 0，39 项因 POSIX/Linux/设备树工具等平台条件跳过；同一原生 `cc` 环境下相关 18 项专项全部实际执行、退出 0。WSL Ubuntu-24.04 重跑完整 116 项（21.536 秒）、退出 0、零跳过；`shellcheck --severity=error`、`python -X utf8 tools/check_repo.py`、`git diff --check` 均退出 0。核对当前基线、12 项源码输入、5 项 L2 输出以及 build.status=0 均与此镜像原始记录吻合。上述是本地证据，不替代推送后精确 HEAD CI、独立专项复核和更长时段 L3；PR 保持草稿，RISK-004 继续阻断合并。
