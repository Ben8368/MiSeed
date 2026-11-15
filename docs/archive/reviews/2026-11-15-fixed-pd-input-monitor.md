# 有线固定 PD 输入异常：离线修复与验证

## 范围与依据

维护者要求检查并修复充电异常。本轮目标为有线 USB 固定 PD / SMB5；不修改无线 RX/TX，不进行接机、供电、刷写、提交或推送。环境为 Darwin arm64 / zsh，工作分支 `codex/linux-standard-interfaces`，基线 HEAD `f349fec9f6f1562eead915bdc61ae4252f85226f` 加本轮未提交修改；保留此前本地 L3 文档。

[固定 PD L3](../hardware/2026-11-15-umi-wired-fixed-pd-l3.md) 的原始日志哈希复算一致：9V 段 1,732 组样本，仅 seq=8386、boot_ms=2222193 的 USB 输入为 2,404,735µA，当时 ICL 为 1,950,000µA、合约预算为 2,000,000µA。前后正常样本不解释该点，不能据此判为噪声或持续过流。旁路电池电流原始范围为 −4,882–0µA，修正文档单位笔误，不改原日志。

锁定 LOS `71b13e62f057a649b77fe4062feb73ee72ad609c` 的 `smblib_get_prop_usb_current_now()` 明确 PM8150B buck 为 0.2V/A，本地 ADC µV×5→µA 与之相符。固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的 `adc5_do_conversion()` 持 ADC mutex，读错会返回错误。未找到足以修正换算系数或认定并发串样的依据；ADC、ICL 动态和物理瞬态原因仍待独立复核。

## 接口审查与改动

| 功能 | mainline 子系统 / ABI / 消费者 | 本地差异 |
| --- | --- | --- |
| USB 输入实测与配置 | IIO consumer、power_supply `current_now/input_current_limit`，µA；Debian 标准电源消费者 | 沿用既有 buck 换算及属性，无新增 ABI/binding |
| 固定 PD 预算 | Type-C/TCPM power_supply；本地已登记的 `input_voltage_limit` 提案 | TCPM `current_now` 为协商电流；不能作为 ADC 实测证明，TD-004 不扩大 |
| 故障停止及旁路互锁 | 既有 SMB5 内核 worker / 故障锁，`charge_behaviour` | 无新增 Android 运行依赖；Debian 集成及上游专项审查仍开放 |

确认两个软件缺口：原 `smb5_fixed_update()` 只验证 TCPM 合约电流，没有持续检查 SMB5 实测；worker 的 full_bypass 分支缺少故障锁条件，可恢复已经暂停的输入。

本轮补入带载监测：配置 ICL 或实测 IBUS 超过已验证合约预算即报 `-EOVERFLOW`，记录实测、ICL、合约；不可恢复的监测读取错误同样锁定故障。停止路径分别尝试停电池充电、暂停 USB 输入并记录各自返回码，一项失败不跳过另一项，不喂 watchdog。full_bypass 不得绕过故障锁恢复输入。正常 5V/9V 切档仍先卸载，待原有限流策略生效后恢复；PPS 所有权期间不运行此监测。

策略正文见 [PPS.md](../../../kernel/power/PPS.md#状态与安全退出)。不滤波、丢弃或钳位原始值，不放宽 9V/2A 或保护阈值。不使用未校准容差把 ADC 与刚降低的温控 ICL 当作同步硬件保护；软件按标称 1s worker 采样，不能保证捕获该次瞬态或替代硬件保护。

## 验证结果

- `python3 -X utf8 -m unittest discover -s tests -p 'test_fixed_pd.py' -v` 与 `test_charge_policy.py`：各 2 项通过，退出0。真实 C 函数回放 2.404735A，覆盖合约边界、5V/无9V回退、ICL越限、采样/寄存器失败、停充失败和输入暂停失败、故障保持及拔线解除；worker 回归断言故障旁路不恢复输入、不喂狗。
- `.work/fixed-pd-fix-20261115/check_regressions.py`：退出0。使用相同新桩编译原 HEAD 函数时，异常电流回放断言失败；仅移除新的旁路故障条件时，输入恢复次数断言失败。反例在临时目录执行，未改工作树源码。
- macOS Python3.11：完整回归258项，退出0，42项因缺少Linux/DT工具或schema环境跳过。Linux ARM64 / Ubuntu24.04 容器：完整回归258项，退出0，254项通过，4项 DT/schema 环境用例跳过。本轮未修改DTS/binding；未将这些跳过计为通过。
- `python3 -X utf8 tools/check_repo.py`、`git diff --check`：退出0；文档职责/链接/预算和最终来源锁检查通过，仅有既存源码篇幅黄灯。构建来源锁与当前锁逐字节一致，两份改动C源码哈希对应构建锁。
- L2 使用既有 `miseed-boot-l2:local` 容器，挂载仓库至 `/workspace`、既有卷至 `/build`，执行 `MISEED_WORK_ROOT=/build MISEED_OUTPUT_ROOT=/workspace/.work/fixed-pd-monitor-l2-20261115 MISEED_JOBS=4 bash tools/build_kernel_l2.sh`：退出0。Linux固定版本如上；GCC13.3.0 / make4.3。旧缓存身份检查失败后由原脚本保留缓存、使用新源码与构建目录；完整内核/DTB/rescue/链接/打包及4项ABL叠加回归通过，无时钟警告。首次构建因本地源码哈希尚未同步而停止，同步后完成本次构建。

| 当前未提交输入 / 产物 | SHA-256 |
| --- | --- |
| `qcom_pm8150b_pd.c` | `58dd6a56fd431ed5235e11bb4c7c50f2d6d74f7eba8e5379bc7bc29cae36d79d` |
| `qcom_pm8150b_policy.c` | `5d91daf47e3e4729be68457842e174e7564b45b82ae4f286be6d0684bba9a2d9` |
| 来源锁与构建副本 | `ea030fc7e2830d9e7efe78f6a361b8f85da07f8939325c01e59925786176a1a4` |
| 新 `boot.img` | `b638ee91676835f30c98544044a0c77fb23c5ac339bfc746a64a6bf700ea5870` |
| DTB（与旧镜像一致） | `c428951d470edd4f99c47f06609eb95671b829cee6b4c6f8dbdf43753019acd5` |
| resolved.config（与旧镜像一致） | `3695601c86a22d9ddd44d359d65c9ce0d67e880dd81047e12c9c14e03f22c940` |

镜像目录为 `.work/fixed-pd-monitor-l2-20261115/`，此前 L3 镜像目录未覆盖。日志在 `.work/fixed-pd-fix-20261115/`，不入库：L2日志SHA-256 `09f2508e7e73cd3a2a808ff33e421e2c448230a008a857410e44f867c1042b6a`；Linux回归日志 `ec21dab379f3742f10aa85b6b3500671999e68a5fd02ceef3cdd0db64be2d365`。

## Audit 与关闭边界

🟢 本地L0/L1/L2、既有标准属性及来源/许可边界；🟡 `qcom_pm8150b_policy.c` 340行，保留完整状态机，归TD-008/WP9；其余本轮改动源码≤300行。🔴 合并验收：新代码L3、开发者以外专项复核与精确HEAD CI未完成；原2.405A原因、严格限流、温标和硬件保护仍归RISK-004，未据软件回归关闭。旧固定PD/PPS L3继续保留各自原镜像范围。

后续L3必须先核验本轮镜像/配置/来源锁与采集材料并另行取得本轮供电授权；只观察自然固定PD工况，不主动注入真实过流。重点核对原始ADC、电流配置、合约及故障停止/暂停返回码；发现超限或保护异常立即断开有线输入并回退。当前未要求维护者接机。
