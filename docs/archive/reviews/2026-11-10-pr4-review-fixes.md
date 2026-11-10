# PR #4 自动审核核实与 PR #5 修复（2026-11-10）

## 范围与逐项结论

审核来源：[PR #4 CodeRabbit review](https://github.com/Ben8368/MiSeed/pull/4#pullrequestreview-5839490136)，覆盖 `a081ab302a3099eb1825ed76fe8cb011e1cec79c`。本轮修复基于 PR #5 的 `3afd66f056761c26692876585b85601612e474db`，目标分支仅为 `codex/mainline-followup`；PR #4 保持原 HEAD。机器人文本只作线索，以下结论来自固定源码、当前实现及离线复现。

| 意见 | 核实与处理 |
| --- | --- |
| 历史审计引用当前 DTS/config | 确认。核对 `42f54a2d7a338a6e329ec432c5d23c3d085effd2` 中的 `qcom,nq-nci`、MISEED_NFC_NQ 与错误的 `monitored-battery = <&umi_fg>`，将三处链接固定到该完整提交 |
| FG 电流符号仍反向，需登记债务 | 对 PR #4 成立，对当前 PR #5 已不成立。`8b6a9c2` 已在导出边界反号；当前 `qcom_fg_core.c:qcom_fg_current_now()` 处理 INT_MIN/读取失败，`umi_pps_hw.c` 与 `qcom_pm8150b_policy.c` 消费充电正号；TD-004 已准确描述源码偿还及剩余 L3，不恢复过时债务 |
| PPS 每 200 ms 无条件发事件 | 确认。工作线程与标准控制 setter 共用上次已排队通知的快照；比较策略状态、PD ONLINE/USB_TYPE、泵实时状态、HEALTH、停机意图、遥测可用性及停机待恢复标志，变化才发通知；初始状态、读错及恢复也各通知一次 |
| NFC `clock-request-pin` 不符合 TLMM binding | 确认。固定上游 `qcom,sm8250-pinctrl.yaml` 要求嵌套子节点匹配 `-pins$`。改为 `clock-request-pins`，GPIO、mux、驱动强度、偏置不变；前后 DTB/schema 对照确认 NFC 诊断消失 |
| 13.79% docstring 覆盖率低于 80% | 不作为缺陷修复目标。机器人分析 87 个函数、29 个文件，未列具体缺失契约；仓库没有该比例门槛。本轮保留必要注释，并补充通知快照、锁要求、只读状态与错误恢复说明，不批量扩写无信息量注释 |

## 接口与安全审查

有线 PPS 事件沿用固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的 power_supply 通知与标准属性，Debian 由 sysfs/udev/UPower 消费；本轮未引入 Android 服务或私有 ABI，未宣称 Debian 已集成。电流符号对照同版本 `Documentation/ABI/testing/sysfs-class-power`：电池充电为正、放电为负。

快照在 `c->lock` 内更新，通知在解锁后排队；仅增加 PD 标准属性及 BQ `0x0a` 实时状态的只读采样，不读清锁存故障。通知采样失败只进入快照，不替换既有控制与失败锁定策略。电压/电流遥测继续从标准 getter 获取，不为每个模拟量变化强制发事件。无线 RX/TX 未涉及，未改变有线电压/电流/温控/保护参数，未操作手机。

## 🚦 Audit Report

源码完成后、完整验证前已汇报 Audit：范围、接口、授权与来源为绿；8 项既有源码篇幅黄灯仍归 TD-008，其中 PPS 状态机 467 行、故障注入桩 592 行；文档预算及文件硬上限由最终仓库检查和 PR 实测复核。PR #5 原为 79 文件、7,187 新增行，规模红灯，维持既有收尾，本轮仅处理审核修复及验证。

合并验收仍有阻断边界：开发者以外专项复核、新驱动/新 ABI 的适用 L3 和精确 HEAD CI 未在本轮闭合；按维护者要求不等待、不跟进远端 CI。既有有线 PD/PPS 稳定 L3 保持原镜像证据范围。本轮属于开发者自查，不冒称独立复核；不合并、不发布。

## 验证与未覆盖项

宿主 Windows 10.0.26200 / x64、PowerShell 7.6.9、Python 3.14.2；Linux 为 WSL Ubuntu-24.04 / x86_64、Python 3.12.3；ARM64 GCC 13.3.0（Ubuntu 13.3.0-6ubuntu2~24.04.1）。命令分别在 `C:\MiSeed`、`/mnt/c/MiSeed` 执行，除明确列出的基线诊断外最终命令退出 0。

| 检查 | 命令与结果 |
| --- | --- |
| Windows 全量 | 将 `.work/tools/w64devkit-2.11.0/w64devkit/bin` 加入进程 PATH，`python -X utf8 -m unittest discover -s tests -v`：181 项，122 通过、59 跳过；缺失原生 DT/schema/Unix 工具的用例由 WSL 补充 |
| WSL 全量 | `python3 -X utf8 -m unittest discover -s tests -v`：181 项，178 通过、3 个指定 DTB/schema 输入的专项跳过；随后提供本轮 DTB 与 schema 运行 `test_wired_dt.py`，3 项均通过，相关用例合计全部实测通过 |
| PPS 真实 C 回归 | `python3 -X utf8 -m unittest discover -s tests -p test_pps.py -v`：4 项通过，包含新增通知用例与既有所有权、故障、deadline、停机恢复注入；新增用例验证 20 次空闲轮询无事件风暴、同策略状态下在线/类型/泵变化、读错/恢复、幂等 setter、停机重试及移除/休眠抑制 |
| ARM64 L2 | 下述完整构建退出 0；Image、DTB、modules、静态救援入口、initramfs、boot.img 及供电对象/符号检查通过；本轮构建日志无 Clock skew 诊断 |
| TLMM binding | `PATH=/tmp/miseed-dtschema/bin:$PATH make -C <source> O=/tmp/miseed-pr5-pinctrl-schema ARCH=arm64 dt_binding_check DT_SCHEMA_FILES=pinctrl/qcom,sm8250-pinctrl.yaml`：schema/示例无诊断 |
| 板级 DT | `PATH=/tmp/miseed-dtschema/bin:$PATH make -C <source> O=<build> ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CHECK_DTBS=y qcom/umi-csot.dtb DT_SCHEMA_FILES=pinctrl/qcom,sm8250-pinctrl.yaml:net/nfc/nxp,sn100t.yaml`；另对修复前/后打包 DTB 运行 `dt-validate -s /tmp/miseed-pr5-pinctrl-schema/Documentation/devicetree/bindings/processed-schema.json -l qcom,sm8250-pinctrl <dtb>`：NFC 诊断由有到无，既有 BQ IRQ 诊断不变，未新增诊断；不能只依据退出码标全绿 |
| 仓库与差异 | `python -X utf8 tools/check_repo.py`、`git diff --check` 均通过；8 项源码黄灯已核对 TD-008，活动文档预算合规 |

`<source>` 为 `/tmp/miseed-boot-l2/source-split-source-20261110`，`<build>` 为 `/tmp/miseed-boot-l2/source-split-build-20261110`。完整 L2：

```sh
MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 \
MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/source-split-source-20261110 \
MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/source-split-build-20261110 \
MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/pr5-review-l2 bash tools/build_kernel_l2.sh
```

| 产物 | SHA-256 |
| --- | --- |
| Image | `90d8c66101c272874bea491a5c3b5457da04bd4cdf533e7c96332cec4e603f8b` |
| DTB | `97295ace9746ce224dff11862ad2a5a1c7d5f2ee23a46254f00706dd80d27106` |
| initramfs | `a54d41ef981428464a3ea87f6c920f4252402c3f8edd11c25b4c43c6395d4265` |
| boot.img | `d315356d0cc83b2ebd56eabf7c74899513d768c9b380d84acd0c84fc34d63377` |

定向检查扩大到 TLMM 后发现**既有**有线 `umi-bq-irq-state` 的 `input-enable` 被固定 binding 拒绝（`False schema does not allow True`）。修复前后相同，归 TD-007 的整机 schema 未闭合范围；本轮未改变 BQ IRQ 硬件配置，后续由 WP9 核对 TLMM 实现/配置并取得适用复核与 L2/L3 后解除。NFC 本项已修复不等于整机 schema 合规。原始日志、对照 DTB 与构建产物仅存忽略的 `.work/pr5-review-*`，不提交。

本轮未执行 RAM boot、刷写、分区、真实块设备写入、主动充电或无线 RX/TX 测试。独立复核与适用 L3 仍按 [QUALITY](../../QUALITY.md)、TD-003–007 和既有风险跟踪；不将源码修复或 L2 扩大为真机通过。此前拆分验证见[源码职责拆分](2026-11-10-source-responsibility-split.md)。
