# 2026-11-13 有线三阶段策略与固定PD档位协调

## 范围与接口审查

维护者授权按最终方案调整代码，并报告历史PPS曾超过3A；该报告不混作已归档IBUS实测。既有记录包含电池净电流超过3A/约5.1A，以及旧策略输入峰值2.864A；旧L3结论保留，不外推本版。目标为有线USB输入≤3A，电池侧仍独立受6A及更严格热限额约束。无线RX/TX不变，无设备连接、供电操作、刷写、提交或推送。

宿主Windows 10.0.26200 x64 / PowerShell7.6.9；工作分支codex/wired-six-stage-power，保留既有未跟踪MDSS记录。目标Linux固定f45ed76d6791cf72ff1265ee93511175bbbc2f18；参考LOS固定71b13e62f057a649b77fe4062feb73ee72ad609c。SMB5/PPS/BQ既有来源许可未改变。

| 功能 | mainline接口/消费者 | 本地差异与退出要求 |
| --- | --- | --- |
| 分段输入/电池充电 | power_supply input_current_limit、charge_behaviour；Debian标准消费者 | 内核互锁继续归TD-004；Debian集成另验 |
| 80/71旁路 | charge_control_end_threshold/start_threshold、STATUS与净电流 | 改SOC阈值不伪报Full；旁路保留系统输入预算10W |
| 固定PD档位 | Type-C/TCPM、标准USB connector capabilities/caps-N；power_supply input_voltage_limit，µV | 固定Linux仅有PPS协商属性和Type-C可选PD对象，缺少充电消费者选择固定输入档位的标准power_supply通路；本地通用化扩展复用firmware profile与TCPM AMS，需Type-C/power_supply子系统复核，不声称已上游合入 |
| 有线温控 | IIO/thermal、SMB5 BMS温度、power_supply限流 | PPS六路模型不套用于固定PD；固定PD趋势反馈不添加温度阈值，效果/温标另验 |

复用依据：固定源码tcpm.c的tcpm_pd_set、tcpm_pd_select_pdo、tcpm_pps_set_out_volt及PD复位生命周期；Documentation/devicetree/bindings/connector/usb-connector.yaml的capabilities与caps-N；Documentation/ABI/testing/sysfs-class-power的电源属性单位/读写模型。通用化扩展只在firmware已声明的sink profile间选择，不接受任意裸PDO，禁止source角色改档，AMS繁忙不提前修改profile，未完成协商返回EAGAIN。input_voltage_limit同时约束固定与PPS请求；协议复位/断开恢复默认profile；已有Type-C对象继续公开。TD-004/TD-007保留专项复核、上游设计审查/替换及跨板型生命周期验收边界。

## 实现

策略正文只在[kernel/power/PPS.md](../../../kernel/power/PPS.md#状态与安全退出)。0–10%固定5V/2A；11–70% PPS30W期望/≤3A/2:1电压，或固定PD9V/2A（无9V用5V/3A）；71%起固定5V/2A；80%停电池充电旁路、≤71%固定5V复充，复充周期不重入PPS。27W固定PD仅作为后续评估目标，未开放9V/3A。PPS交回固定PD时在恢复输入前落实10W预算；复充阶段不能再次claim PPS。

PPS9.5V、6A电池、全部原始热表、传感器新鲜度和BQ保护不放宽。SMB5固定切档先核实停电池充电/暂停输入，设置高档FSW和连续输入范围，再协商；核对已完成合约/空载电压后设置对应输入范围/FSW，受合约限流后恢复。匹配LOS的9V固定PD2A上限、5V3A板级上限保留。升温每次观测增加100mA输入降额，冷却后按jiffies保证每秒最多解除100mA，中断突发不能加速恢复；已有冷端/45°C/读错保护优先，反馈并非新硬件保护阈值。

## 验证与边界

Windows充电相关15项全部通过；一次原生全量尝试暴露两项既有显示测试的WinError1314符号链接权限限制，未修改这些无关测试；当时的文档预算/换行及BQ桩问题已修复，最终仓库检查与BQ/充电专项通过。Linux完整覆盖对应权限、符号链接、串口和FIFO用例。

- `wsl -d Ubuntu-24.04 --cd /mnt/c/MiSeed -- bash .work/wired-policy-20261113/final-tests.sh`：完整回归242项，零跳过，50.559秒，退出0；ShellCheck error级通过。DT反例包括拒绝固定9V/3A及默认接入profile夹带9V。
- `bash .work/wired-policy-20261113/build.sh simpledrm`和`native`：两套固定源码ARM64编译/链接/打包、定向binding、CHECK_DTBS、最终DT/schema及四项DT回归全部通过、无跳过。首次L2发现局部变量current与内核宏冲突，已更名input_ua后完成最终双构建。
- `.work/wired-policy-20261113/verify.py`：两套各27份供电源码/头与工作树逐字节一致；板级输入、构建来源锁、TCPM转换输入/输出摘要一致；镜像内kernel/ramdisk/DTB与对应构建文件逐字节一致。详情在忽略目录的verified.json。
- 最终来源锁SHA-256：`3f815a1cdd0c37cf422f1b58dc66ddf813472e240d5fc5327b0864307051e452`。

| 镜像 | 字节 | SHA-256 |
| --- | ---: | --- |
| simpledrm | 9654272 | `fe6404449e9f9f58619e9f80bb5b9354b1195b7514103f891f2de33b0cc64652` |
| native | 14135296 | `3f51ca26c5b29001c68cbf03c47ebac0f6574410d976025068516989dd60d218` |

L2环境为WSL Ubuntu24.04 x86_64、GCC13.3.0、GNU make4.3、dtschema2025.8、yamllint1.38.0；原/tmp缓存已缺失，使用独立持久缓存`/home/xiaoluo/.cache/miseed-l2-20261113/`，按既有锁重新取得源码和头模板，不覆盖历史构建。全部原始日志、构建产物及核验脚本仅留忽略目录，不入库。

🚦 Audit：本版L0/L1、双L2与DT/schema🟢；源码篇幅10项🟡（无≥601行源码，已更新TD-008中实际变动计数）；新硬件行为/温标/保护🟡，高风险合并验收因新L3和开发者以外专项复核未完成仍🔴。无新私有sysfs或Android运行依赖，TCPM标准属性扩展仍为本地提案，TD-004/TD-007未关闭。未查询远端CI，未提交、推送、操作设备或改无线RX/TX。

新固定9V、温度反馈、80/71循环及本版3A PPS的L3未执行；硬件温标/保护及独立复核仍归RISK-004/TD-005。不得将离线或编译通过写成真机可用。

验证日志SHA-256：linux-tests.log `3fc3672806ceb5d14d6e619f46731b61096d712709376b09687144d8ff158975`。

## 本地提交

维护者随后明确授权本地提交。本次只提交上述有线策略、验证和对应文档；源码/构建输入未再改动，沿用242项零跳过、双L2/DT与镜像核验。既有无关MDSS/SMMU记录留在工作区。主作者保留维护者Ben，附Codex协作署名；不推送、不发布、不合并，新L3与独立复核仍待完成。
