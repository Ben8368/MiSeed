# 有线固定 PD：统一采样保护、MID 电压与 TCPM 链接修复

## 输入与接口审查

接续[修复版 L3 异常](../hardware/2026-11-15-umi-fixed-pd-monitor-l3.md)，维护者要求实际修复。Darwin 25.6.0 arm64 / zsh，工作分支 `codex/linux-standard-interfaces`，HEAD `3263cff0483f9cfa66c5ac693d759b4c7a6710d2` 加本轮修改；不提交、推送、合并。异常测试已停充，维护者确认回到 Android；本次离线修复未再次启动或供电。

| 功能 | 固定 mainline 接口 / 消费者 | 本地适配 |
| --- | --- | --- |
| 有线输入电流 / 电压 | Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` IIO consumer、power_supply `current_now/voltage_now/input_current_limit`；Debian 标准电源消费者 | 既有 µA/µV ABI 不变；所有标准输入电流读取共用故障互锁 |
| PM8150B 电压源 | 主线 ADC5 已有 `ADC5_MID_CHG_DIV6` 及 1:6 prescale；本地 charger binding | 本机固定 LOS `71b13e62…` 已定义该通道；在原电流/USBIN 通道外增加命名 MID consumer，不新增私有用户态 ABI |
| PD profile 生命周期 | Type-C/TCPM 既有 PD 实例链接；本地 `input_voltage_limit` 提案 | 按固定上游 `tcpm_pd_set()` 先 unlink 再 link；保留 TD-004/006 与上游审查边界 |

无线 RX/TX 未修改；有线 PPS 所有权期间不运行固定 PD 监测。不增加 Android HAL/Binder、厂商投票框架或 Debian 私有接口。

## 已修复的软件缺口

1. 旧 worker 与 sysfs 是独立 ADC 采样：L3 的 2.425170A 值由 `current_now` 读出，worker 不一定采到同一个瞬态。新标准 getter 持 SMB5 锁，使用同一合约/ICL 检查与故障处理；超限先锁定、分别尝试停充和暂停输入，再返回原始 ADC 值。读取失败保留错误并触发锁定，不靠丢样、滤波、钳位或重试变正常。worker 样本及错误标明来源，后续正常读数不解除故障；故障后的 worker 入队在同一锁内完成，不能越过卸载的取消操作。
2. 固定 LOS `smb5-lib.c` 的 `smblib_get_prop_usb_voltage_now()` 明确 PM8150B 高压 USBIN 电压 ADC 存在异常，使用 MID；同提交 `umi-sm8250.dtsi` 给出 MID_CHG_DIV6/1:6。本地带载电压和功率预算改用该物理通道；输入暂停时保留 USBIN 无负载电压检查。沿用 LOS 的 MID<1V 不可信边界，但返回错误，不复制其合约估算替代实测。电流仍沿原 buck µV×5→µA，不更改电气参数或保护阈值。此遗漏与功率跳变的因果关系仍需新 L3 验证。
3. 上游 Type-C setter 不替换现存链接，原 profile 直接重挂导致重复文件名。新路径先移除旧链接、发布新链接再提交 profile/协商；发布失败不排队切档并尝试恢复旧链接，AMS busy 同样回退。detach/reset 即使发布失败也恢复安全 attach capability，保留重试标记并使读回报错，不能继承 9V profile 或伪报成功。
4. 诊断只读增加实际 ICL、AICL settled/status、power path、输入暂停及 ICL 配置寄存器，来自同一本机 LOS `smb5-reg.h`；不确认 IRQ、不写寄存器、不引入额外控制 worker。用于下轮区分硬件调节与软件动作，不据当前缺失寄存器时间线断言功率跳变根因。

来源复核：现有 LOS `smb5-lib.c` SHA-256 `8f542b9679a95da4c5a1bdf1d622ea4e06ab31345b158c9a68bb7d7ff9edeb68` 与来源锁一致；其文件、DTS/寄存器及上游 ADC5/Type-C 源已在既有固定来源范围，保留 GPL-2.0-only 归属。没有下载或运行新外部代码，传递依赖闭包仍按原锁开放。

## 验证与审计

定向 C 回归覆盖新 L3 2.425170A 原样回放、标准读取触发停机、两种停止各自/同时失败、后续正常样本保持故障、PPS/卸载边界；MID/USBIN 通道选择、低 MID 和读错不得估算；Type-C 重复链接真实语义、发布失败、AMS busy、reset 失败后安全能力及重试。在临时副本撤掉 getter 故障动作、使用原 HEAD 的直接重挂、或恢复带载 USBIN 选择，三项反例均断言失败；工作树源码未被反例修改。

- 最终 L2：既有 `miseed-boot-l2:local` Linux ARM64 容器（仓库映射 `/workspace`，既有架构卷映射 `/build`），执行 `MISEED_WORK_ROOT=/build MISEED_EXTRACTED_SOURCE=/build/fixed-pd-sampling-source-f45 MISEED_BUILD_ROOT=/build/fixed-pd-sampling-build-f45 MISEED_OUTPUT_ROOT=/workspace/.work/fixed-pd-sampling-l2-20261115 MISEED_JOBS=4 bash tools/build_kernel_l2.sh`，退出 0。GCC13.3.0 / make4.3，缓存按固定 Linux 逐文件校验通过，完整内核/DTB/rescue/链接/打包及6项ABL回归通过，无时钟告警。最终来源锁与构建副本逐字节一致。
- 严格 DT：同卷的 `/build/pd-pps-dtschema/bin/python tools/validate_kernel_dt.py --source /build/fixed-pd-sampling-source-f45 --build /build/fixed-pd-sampling-build-f45 --dtb /workspace/.work/fixed-pd-sampling-l2-20261115/umi-csot.dtb`，退出 0。新增主线 ADC5 binding 范围；dt_binding_check、相关 dtbs_check、最终打包 DTB/schema 和4项真实反例全部通过，零跳过；错误 MID 通道/分压比会被拒绝。不据定向检查声称整机 DTBO 合规。
- Linux 最终全量：PATH 前置 `/build/pd-pps-dtschema/bin`，`MISEED_WIRED_DTB` 为上述最终 DTB，`MISEED_DT_SCHEMA=/build/fixed-pd-sampling-build-f45/Documentation/devicetree/bindings/processed-schema.json`，执行 `python -X utf8 -m unittest discover -s tests -v`，**266项全部通过、零跳过**，退出0。macOS相同全量266项、222通过/44平台或工具环境跳过，退出0；跳过不计通过，Linux已覆盖。仓库检查与 `git diff --check` 退出0。

| 最终产物 / 输入 | SHA-256 |
| --- | --- |
| boot.img（9,728,000 bytes） | `ceafb34bba3fc6e45920cc9cd84fa67e793c0aa00669fae9e7c33cefe7afaeaf` |
| DTB | `15dd191673554c45399db1607fe5024f083f4997b1e489ba02dc90c7130f4628` |
| resolved.config | `3695601c86a22d9ddd44d359d65c9ce0d67e880dd81047e12c9c14e03f22c940` |
| 来源锁 / 构建副本 | `e23ffc62a0ab8659e375f8d730d4f98323fdb3c299470b8b3377851745acbda0` |

镜像在 `.work/fixed-pd-sampling-l2-20261115/`；最终构建、DT、全量回归和反例结果在 `.work/fixed-pd-remediation-20261115/`。开发中补全 reset/卸载失败路径后已按最终锁重建，不交付中间镜像。一次最终构建因卷空间不足而在提取阶段退出2；清理本轮中间展开/构建目录，并将两份旧 `linux-source.*` 缓存完整归档到该目录的 `cache-backup/`，gzip CRC和SHA-256双向核对后释放原缓存，再成功构建；保留来源Git缓存、镜像和原始真机日志。归档可恢复全部历史差异，未删除真机数据或绕过构建门禁。

后续交付：维护者授权推送至PR7。PR6已合并，转入新工作分支`codex/fixed-pd-sampling`，基于最新main `8d772e39f26444333825f18a9edfd2c4288a5fcb`；已核对该基线文件树与上述`3263cff`完全相同，修复源码/来源锁/镜像及L2/L3证据不变。仅提交并推送本轮改动，不合并；原始镜像/日志仍留忽略目录。

🚦 内核接口、原始数据保全与来源边界自查 🟢；`qcom_pm8150b_hw.c` 360 行 🟡，按 TD-008/WP9 保留同一硬件读写职责。后续[本镜像L3](../hardware/2026-11-15-umi-fixed-pd-sampling-l3.md)已通过标准getter超限检测、停充/暂停及故障保持，旧TCPM告警未再现；持续充电仍因输入异常中断，开发者以外专项复核及精确输入CI未完成，合并验收 🔴。真实过流/ADC原因、硬件保护、板级温标及功率跳变继续归RISK-004；新轮接机仍须先备材料，异常断开有线输入并保全日志。
