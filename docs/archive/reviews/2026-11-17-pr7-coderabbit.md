# PR #7 CodeRabbit 意见核实与修复（2026-11-17）

## 范围与基线

维护者授权逐项核实、修复并推送到既有 PR #7，收尾阶段豁免 PR 文件数/新增行数限制；未授权合并。审查基线为 `82afa6d47d65e1de4087525c0755122b287f5eaa`，工作分支 `codex/fixed-pd-sampling`。CodeRabbit [完整审查](https://github.com/Ben8368/MiSeed/pull/7#pullrequestreview-5934650297)共四项具体意见，均确认存在需要修复的缺陷，但 Kconfig 的因果细节按本地实测修正。

目标仅为**有线**固定 PD/PPS 与 BQ25970 遥测；无线 RX/TX 不在本轮改动范围。保留 Linux power_supply、Type-C/TCPM 标准接口、既有 DT、保护参数及许可证。本轮未接入或操作手机，无刷写、真实块设备写入或供电操作。既有 L3 结论继续按其原设备、源码和实测范围有效。

## 逐项结论

| 意见 | 独立核实与修复 | 回归证据 |
| --- | --- | --- |
| [BQ 配置校验失败仍可返回旧样本](https://github.com/Ben8368/MiSeed/pull/7#discussion_r4596650062) | `configured` 未清除，电压/电流 getter 只检查 `sample_valid`，不检查 `sample_error`；因此校验失败必须同时使样本失效，保留原错误码。成功校验不改变样本。 | 实际芯片 C：已有有效样本后的 I2C 失败、配置读回不符与 watchdog 到期均拒绝电压/电流旧值；健康状态、重新准备/采样恢复通过。 |
| [PPS 必须依赖内建 I2C](https://github.com/Ben8368/MiSeed/pull/7#discussion_r4596650088) | 锁定内核 Kconfig 实测旧 `I2C=m` 得到 `PPS=y、BQ core=m`，并非机器人所说 core 必然被强制为 y；真实缺陷仍是内建控制器可引用模块芯片 API。改为 `depends on I2C=y`。 | 锁定 `scripts/kconfig/conf` 运行真实本地 Kconfig：旧 y/m/n 分别为 PPS y/y/n、core y/m/n；修复后为 PPS y/n/n、core y/n/n。独立 `I2C=m、BQ core=m` 保留且 arm64 编译通过。 |
| [固定 PD getter 使用过期协商电流](https://github.com/Ben8368/MiSeed/pull/7#discussion_r4596650097) | `pd_current_ua` 只在 worker 更新，源端重新协商后的 getter 可漏判。每次保护检查通过 TCPM 标准 `CURRENT_NOW` 读取当时预算，验证范围后与 ADC/ICL 比较；读取错误仍执行既有停充/挂起。 | 实际 SMB5 C：缓存 3 A、当前预算 2 A、实测 2.4 A 时保留原 ADC 返回值并锁故障停充；反向预算上升不误报；非法预算、ADC/TCPM/ICL 读取失败与既有故障恢复反例通过。 |
| [PD 链接恢复失败却被当作成功](https://github.com/Ben8368/MiSeed/pull/7#discussion_r4596650104) | 锁定 `class.c` 的 Type-C setter 在先移除旧链接、随后两次发布失败时留下空链接。新增独立 `psy_pd_link_dirty` 状态；失败的发布恢复及 AMS 回滚都保留待修复标记，getter 报错，标准 Type-C 选择、同档限压重试及 reset 在适用路径修复链接。 | 实际 TCPM 提案 C：非零旧限额下双重发布失败、同档重试失败/成功、AMS 回滚双失败、首次无 owner 失败后的 reset、PPS 限压遇到坏链接均通过；互斥、能力集和 AMS 失败不提交配置的规则保留。 |

新增回归分别注入审查基线的旧 `bq25970_control.c`、`qcom_pm8150b_pd.c`、`tcpm_fixed_profile.inc` 时均触发预期断言失败；当前源码全部通过。故障用例不是只匹配新增文本。Kconfig 另以上述旧/新依赖矩阵取证。

## 验证环境与结果

- 宿主：Windows 10.0.26200 x64、PowerShell 7.6.9、Python 3.14、便携 GCC 16.2.0；WSL Ubuntu-24.04 / Linux x86_64。构建 arm64 使用锁定 GCC 13.3.0 与 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`。
- L0：`python -X utf8 tools/check_repo.py`、`git diff --check` 通过，构建输入 SHA-256 已同步；未引入外部来源或扩大私有 ABI。源码黄灯仍归 TD-008，本轮改动文件无源码篇幅红灯。
- L1 Windows：进程 PATH 补齐仓库便携编译器/DTC，测试进程 `SetErrorMode(0x8003)` 避免故意崩溃反例弹窗，运行 `python -X utf8 -m unittest discover -s tests -v`；285 项，216 通过、69 宿主不支持项跳过，退出 0。
- L1 WSL：`python3 -X utf8 -m unittest discover -s tests -v`；285 项，281 通过、4 项定向 DT 专项在普通入口跳过，退出 0；DT 专项由下述独立入口实际执行。`shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` 退出 0。
- L1 故障反例/配置矩阵：`.work/pr7-coderabbit-proof.py` 执行旧源码预期失败与 Kconfig y/m/n、独立模块配置矩阵，退出 0；脚本仅作本地取证，结论及固定输入已记录于本归档，正式 C 回归已纳入 tests。
- L2 原生 DSI：`MISEED_NATIVE_DSI=1 MISEED_JOBS=8 MISEED_WORK_ROOT=/home/xiaoluo/.cache/miseed-l2-20261113 MISEED_EXTRACTED_SOURCE=/home/xiaoluo/.cache/miseed-l2-20261113/source-pr7-coderabbit-20261117 MISEED_BUILD_ROOT=/home/xiaoluo/.cache/miseed-l2-20261113/build-pr7-coderabbit-20261117 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/pr7-coderabbit-native-20261117 bash tools/build_kernel_l2.sh` 退出 0。新源码目录从锁定 Git 树提取，日志无时钟偏移诊断；修复后的芯片、SMB5、TCPM 均编译并链接。
- L2 独立芯片：同源码，另建 `build-pr7-coderabbit-independent-20261117`；`allnoconfig` 后开启 `MODULES/OF/POWER_SUPPLY=y、I2C/CHARGER_BQ25970_CORE=m`，`olddefconfig` 核对 SMB5/FG/PPS/TCPM/auxiliary/thermal 均关闭；通过 `tools/run_build_command.py` 的时钟守卫运行 `make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image modules`，退出 0。模块无 Umi/SMB5/TCPM/auxiliary/thermal 未定义符号。

- Schema/DT 专项：`/home/xiaoluo/.venvs/miseed-dtschema-2025.8/bin/python tools/validate_kernel_dt.py --source /home/xiaoluo/.cache/miseed-l2-20261113/source-pr7-coderabbit-20261117 --build /home/xiaoluo/.cache/miseed-l2-20261113/build-pr7-coderabbit-20261117 --dtb /mnt/c/MiSeed/.work/pr7-coderabbit-native-20261117/umi-csot.dtb` 退出 0；定向 binding、板级/最终 DTB 与四项 DT 反例全部实际执行，无跳过。

产物 SHA-256：

- `boot.img`：`1ecb39576bade7b0e2dc9576a5689643785cfae7f55165dacbf0470a49e6fa86`
- 最终 DTB：`8be099adb994ba2f7e44ebfa3c72568896a36b55f72474fe9789bf80647a0573`
- 独立 `bq25970_charger.ko`：`07c346ee6aad2421b29cb581ea2bdb8693e7ca06c319d3f0d7472fb5039e6f4e`

本地完整日志为 `.work/pr7-coderabbit-{repo,windows,linux,proof,l2,independent,schema}.log`，长日志、临时脚本与产物不提交。提交/推送前还须核对署名、执行 PR base..head 提交检查，并在 PR 记录最终 HEAD 与远端 CI 的实际状态。

## 🚦 Audit 与验收边界

- 🟢 范围/真实性/错误路径/来源：四项意见均有源码依据、失败反例和恢复证据；标准接口及保护值未扩展。
- 🟢 文档预算/构建输入：仓库检查与差异空白检查通过，哈希对应本轮实际文件。
- 🟡 源码篇幅：既有黄灯责任及退出条件仍归 TD-008；本轮未产生新的黄灯文件。
- PR 规模：维护者本轮明确豁免新增行数/文件数限制，仅继续审查修复与必要验证记录；不豁免其他质量及合并要求。
- 🔴 合并验收：本轮适用 L3、开发者以外独立供电专项复核及最终精确 HEAD CI 须按 RISK-004/TD-004/005 和 PR 待办继续完成；本轮离线修复与推送不代表这些门禁关闭，不合并。
