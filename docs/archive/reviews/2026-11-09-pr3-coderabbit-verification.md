# PR #3 自动审查意见核实与 Windows 环境补齐

## 范围与授权

2026-11-09；基线 `5fba364f0d28c655831f1339366fd47183225f18`，Windows x64 / PowerShell 7.6.9 / Python 3.14 与 WSL Ubuntu 24.04 x86_64 / Python 3.12.3。
维护者要求逐项核实 CodeRabbit 意见、补齐环境，随后改为先不提交 PR #3，构建并临时启动新镜像供真机测试；设备稍后接入。采集阶段修改留在工作区；维护者确认正常收尾后重新授权提交到 PR #3，不合并、不发布、不刷写。

审查来源：[CodeRabbit review](https://github.com/Ben8368/MiSeed/pull/3#pullrequestreview-5824154895)，17 条行内意见、3 条细节意见、2 条未验证提示及默认 docstring 检查；以下结论来自当前源码与回归，机器人等级不作为结论。

## 意见处理

| 定位 / 意见 | 核实与处理 |
| --- | --- |
| 4511391463：设备序列号 | 确认当前归档两处泄露，已脱敏。已推送祖先仍含副本；RISK-009 保留，不重写历史。 |
| 4511391481：UART-only 状态 | 确认过期；更新 platforms 区分旧镜像与当前 GPIO34/35、44/45、68 及绑定证据。 |
| 4511391492：充电日志内存 | 确认无上限增长与全量复制风险；RAM 文件预算 64 MiB，满后保留旧记录、丢弃新增并累计 storage_errors；导出不可变前缀，只用一条记录缓冲且 USB 阻塞不持锁。 |
| 4511391504：PD RX 读错后未 ACK | 确认；长度、状态和 buffer 读取失败均尝试释放 RX，ACK 失败仍不转交未读报文。 |
| 4511391518：inhibit 时仍可 claim | 确认；SMB5 inhibit 纳入 -EBUSY 条件，保留原独占/失败清理。 |
| 4511391558：协商前日志洪泛 | 确认；同一 pre-claim 错误只记录首次/变更，运行故障仍完整记录。 |
| 4511391570：挂起后无法恢复 PPS | 确认；新增挂起标志与 resume，只重启原可运行会话，保留 manual stop、DONE、FAULT；挂起停泵失败继续拒绝。 |
| 4511391589：15 W 最小运行功率 | 确认固定 TCPM 的电流/电压 setter 拒绝低于 operating_snk_mw 的请求；7.3 V×2.05 A=14.965 W 会被拒绝。改为已有软件总线最低 7 V×PPS 最低 2 A=14 W，不改电气上限或热控表；回归模拟实际 TCPM 功率门控。 |
| 4511391600：RPMh EPIPE | 确认；报告 ring overrun 后继续读取，USB 诊断也作相同处理。 |
| 4511391619：pps-status 读失败 | 确认；print_file 返回 open/read/output/close 结果，pps_control 向调度器传递失败。 |
| 4511391625：配置注释 | 确认；修正为测试节点已启用，配置项不变。 |
| 4511391638：PD 测试头部 | 确认有效帧对象数未编码；修正有效帧头；另注明在界内但长度不匹配的 transport 输入仍交 TCPM 做协议校验，不新增 PHY 协议绕过或擅自收紧其语义。 |
| 4511391649：TCPM 连续大分配 | 确认指针表使 port 超过 2 MiB；改为 kvcalloc/kvfree，分配失败仅禁用日志，Type-C 注册不依赖大表成功。维持 262144 条诊断容量。 |
| 4511391661：构建缓存永远失效 | 确认；从固定 Git blob 重新推导 PD 补丁与 RPMh PM_OPP 修改，严格比对输出及诊断 manifest；不信任缓存自报哈希，不放行未知改动。 |
| 4511391667：Windows unavailable 检测 | 确认大块读取中间标记会漏判；逐新块加重叠窗口扫描，覆盖跨块/后随 400 字节情形，保留 raw partial。 |
| 4511391680：PDO attach 连续性 | 确认相同 src_cap_id 不保证同一连接；保留缓存值但标为 unverified，整体为 partial；ID 改变/失败仍 unavailable。不增加任何设备请求。 |
| 4511402823：DTB 失败时已修改 | 确认；同目录临时副本修改及验证，全部成功后 os.replace；失败保留原文件，可重试。 |
| 细节：boot_id/first 控制字符 | 确认存在 0x08/0x0c，已修复；文本检查新增拒绝其他 C0 控制字符，并纳入 PowerShell 源文件。 |
| 细节：I2C driver / PPS status 断言 | 确认仅 compatible 字符串或 prompt 可掩盖无操作；分别映射 readlink/fopen 并断言真实字段与状态内容。 |
| 未验证：manual stop 后不再观察拔线 | 确认调度条件使清除标志分支不可达；保持只读轮询观察拔线，在线时不重新启泵；SMB5 inhibit 继续阻止接管。 |
| 未验证：target-path/phandle/别名绕过 | 具名符号隔离确实不覆盖其他寻址方式；尚无本机匹配 DTBO 证明采用这些形式。保留 RISK-010 限制，不猜测修改本机 overlay 或主线 phandle。 |
| 默认 docstring 80% | 仓库无该门禁；不为机器人默认比例批量添加无意义注释，按真实错误路径及适用回归验收。 |

## 环境

此前 129 项中的 41 项跳过：5 项缺 dtc/fdtget/fdtput/fdtoverlay，36 项要求 POSIX/Linux（Bash 8、权限/符号链接/FIFO 8、POSIX 管道 4、Linux GCC/伪终端 16）。WSL 原已能执行全部用例。现在补齐原生设备树工具，并以 `pwsh -NoProfile -File tools/test_host.ps1` 临时设置进程 PATH、强制检查工具、分别执行 Windows 与 WSL 回归及 ShellCheck；不修改用户或系统 PATH。

DTC 1.7.0 [官方源码包](https://www.kernel.org/pub/software/utils/dtc/dtc-1.7.0.tar.xz)，SHA-256 `29edce3d302a15563d8663198bbc398c5a0554765c83830d0d4c0409d21a16c4`。在 WSL 用 bison/flex 生成 parser/lexer，再用本机 w64devkit 2.11.0 GCC 16.2.0 编译 Windows x64 程序；version_gen.h 固定为 DTC 1.7.0，未采用所在 MiSeed 工作树的 git 后缀。DTC 保留 GPL-2.0-or-later，libfdt 保留 GPL-2.0-or-later OR BSD-2-Clause，原文件头、许可证和源码留在忽略目录 `.work/tools/dtc-1.7.0/`。

| 原生工具 | SHA-256 |
| --- | --- |
| dtc.exe | `55ac25d8d4025c0a43ee656302bf244371c4a5e97824c48735cb26494267ba09` |
| fdtget.exe | `8ab5eee2af337cbfda66f309e82aa1ba5fa911f5d2a51d8605ca161d10a62d13` |
| fdtput.exe | `9a71481a95a5746bb7d9ddbbca9020b4efe50e30be61c25c5d7cec169e8af92a` |
| fdtoverlay.exe | `bf574e63e8d7a48b7eb1835a5acb28c932ddc9d388f3c588b3062c6562fe9b54` |

## 验证与真机边界

最终执行 `pwsh -NoProfile -File tools/test_host.ps1`，退出 0：Windows 全量 136 项通过（39 项 Linux/POSIX 条件跳过），WSL 全量 136 项通过、零跳过；ShellCheck error 级通过。相较原先 41 项，原生设备树的 5 项已执行，新增 3 项 Linux 专项另在 WSL 执行。源码、测试和设备树输入锁校验及 `git diff --check` 均通过。缓存复用专项对构建后真实源码运行 `verify_kernel_source.py`，退出 0，确认 PD/PM_OPP/manifest 均可按固定 Git 输入验证。

L2：`MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/source-review2-20261109 MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/build-review2-20261109 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/pr3-review2-20261109 bash tools/build_kernel_l2.sh`，退出 0；6 项 libfdt 回归实际执行。GCC 13.3.0 / make 4.3。构建出现 modules.order 未来时间戳和 clock-skew 警告，未宣称无警告构建。

新 `boot.img` 为 7,811,072 bytes，SHA-256 `4f923319af9be70efb7fb855dcb992930c3be1ef3d945cd039a0d1fe8540601f`。精确源码输入、未提交基线、锁哈希、命令和原始输出保存在忽略目录 `.work/pr3-review2-20261109/`，不纳入 Git。

维护者随后明确告知设备已连接且为 Fastboot。启动入口复核回归/L2 退出码、所有构建输入哈希、唯一设备 `product=umi` / `unlocked=yes` / `is-userspace=no` 及镜像哈希后，仅执行一次 `fastboot boot`：Sending 0.196 s、Booting 0.102 s 均 OKAY，总计 0.304 s，退出 0。未刷写或重启设备，未发送启充/停充控制命令。

启动后电脑未枚举到诊断串口；维护者随即确认“已经在测试充电了”，即设备已从电脑移至充电器。本次当前证据为 Bootloader 接受 RAM 镜像及维护者开始充电的报告；尚无新串口或完整充电时间线。后续接回电脑且不重启，先导出 RAM/TCPM 日志，再分项评估新错误/恢复路径。

历史已验收 `44854093…e98a89f94c` 的 PD/PPS 成就保持不变；新镜像 L3 与开发者以外专项复核继续跟踪，RISK-004 不关闭。回退按本轮交代的电源键＋音量下进入 Fastboot、重启回原系统；异常发热/供电先断开充电器，不依赖软件停机成功。该启动阶段未提交、推送或更新 PR #3。

## 首轮充电：PPS 正常收尾转固定 PD

维护者接回电脑报告“PPS 充电中途自动切换 PD”。保持本次启动，只读识别 MiSeed VID 1d6b / PID 0104 的 COM6，验证控制台身份后导出 charge-log、version、pps-status、status 和 tcpm-log；未发送启停充、重启或刷写命令。

- 镜像仍为 `4f923319af9be70efb7fb855dcb992930c3be1ef3d945cd039a0d1fe8540601f`；version 为 Linux 6.18.60 / GCC 13.3.0。boot_id `ffd36b77-79bf-40c0-a7ff-d65cfd3861aa`。
- RAM：first=0、next=1017、count=1017、storage_errors=0，序号连续，BEGIN/END 验证通过；TCPM 报告 1091 行、complete=1，主机可解析时间戳恰为 1091 条，没有本轮丢行证据。完整原始数据留在 `.work/pr3-review2-20261109/charger1/`。
- 约 11.998 s 进入 SETTLE，12.529 s 启泵；RUN 样本 266 条，内核时间 12.736695–94.109917 s，持续 81.373 秒，请求逐步从约 8.54 V 升至 9.10 V，电流请求 2.05 A。
- 最后一次不满足收尾电流条件的 RUN 在 83.797872 s，IBAT=2200 mA；84.101570–94.109917 s 的连续 RUN 记录中，净充 IBAT 低于 2200 mA，末尾 2173 mA，BMS/泵电压 4371/4377 mV。主充实际 FV 4400 mV；电压已高于 `max_bat_mv-80`（4320 mV）。这与现有 2.2 A、10 秒 taper 收尾判据吻合。
- TCPM 于 94.371840 s 由本机发起新的固定合约协商，94.372502 s 请求 PDO 0、5000 mV / 3000 mA，94.520452 s 设置完成。内核 94.520531 s 报告 `state=4 reason=0 stop=0 pd_stop=0 owned=0`，即 DONE、停泵和固定合约回退成功，并非 FAULT。此前 SOC 84%、电压未到 4400 mV；结合连续样本和源码，该退出归因于 taper 路径。
- SMB5 随即恢复 enabled=1，约 96.629 s 后电池净充约 2.4 A，后续 SOC 达 85%。充电器连接阶段未出现实际 hard/soft reset 状态切换或 `miseed-pps failure`；早期电脑 USB 和约 135 s 接回电脑后的复位不混入充电器会话。
- 接回后 `pps-status state=1 error=0 requested_mv=9100 requested_ma=2050 stop_pending=0` 是等待态与历史请求值；不能解释成采集时仍处于 PPS。

本轮 L3 已取得 RAM/Linux/USB、PPS 协商/启泵/约 81 秒调节、收尾退回固定 PD/SMB5 继续充电及完整日志导出证据。暂不改收尾阈值，也未绕过温控或延后停泵；其余接收故障、挂起恢复、完整保护专项仍需各自验收。采集阶段修改仍保留在工作区；核实正常收尾后，维护者再次授权提交至 PR #3。

## 提交授权与检查

维护者确认切换符合正常策略后要求提交到 PR #3。本次仅纳入已核实的源码、测试、环境入口及证据摘要，镜像、工具二进制、完整日志与输入快照均留在忽略目录。提交前再次核对 31 项构建输入、来源锁和镜像哈希，全部与本轮 L2/L3 一致；本轮不更改收尾门槛，不合并，不等待新 HEAD 的 GitHub CI。CodeRabbit 意见逐项核实，不将默认评级或 docstring 比例当作验收。
