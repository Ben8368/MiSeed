# PR #2 CodeRabbit 复核与修正

日期：2026-11-08。复核目标为 PR #2 已发布源码 HEAD `a6eceed6c21745e7fc5be3b53d8d17cad16e882e` 的 CodeRabbit 意见，并结合此前发现的探测初始充电抑制失败缺少输入挂起回退问题。用户要求暂缓合并；修复会提交到 PR 工作分支以便重新运行远端 CI 和复核，本记录不表示 PR 已合并。

## 复核结论

CodeRabbit 的具体意见只作线索，逐项对照本地源码、构建契约和可复现回归后，确认并修复了这些问题：

- 电量寄存器端点 0/255 被压成 1%/99%；PMI8994 软/硬 JEITA 温度阈值索引与通用属性映射相反；Gen3 半摄氏度换算截断精度。
- 电量计属性在探测完成前可能访问尚未初始化的电池信息或 SRAM；移除时 notifier、状态延迟工作及 SRAM 工作队列的释放次序不完整；SRAM 访问重新申请可受旧 release work/completion 影响，超时可能留下请求位；IMA 清理失败未向调用方传播。
- SMB5 在“不处于充电”时把在线输入 ADC 读数伪装成零，测试模式阈值仍报告正常阈值，短暂断连可漏采，命令文案也未区分只读采样和充电控制。已修正属性、读取、事件处理和说明。
- 充电器探测初始禁止充电命令失败会直接返回，遗漏独立 USB 输入挂起路径。现在初始探测、工作线程、测试停止和 teardown 使用同一抑制回退 helper，并记录两条路径的结果；初始路径仍在主路径失败时使探测失败。
- staging/applier 跟随目标目录符号链接，验证器可能读取 FIFO 标记、允许目录占用生成文件名，构建错误时遗留临时 fallback 树，缓存测试继承环境变量污染结果。增加拒绝条件和错误清理回归。
- observer 测试复制生产常量，且 Boot CI 文档没有写明 FG/SMB5 对象检查；测试改读生产宏，更新 CI 说明。合并门禁补充精确 PR SHA、base/head 合并树验证或分支保护要求。

PMI8994 的软/硬索引另外对照 Qualcomm 派生内核的 `FG_MEM_SOFT_COLD/SOFT_HOT/HARD_COLD/HARD_HOT` 布局：`0x454` 的偏移 0/1 是 soft，2/3 是 hard。来源：[LineageOS `qpnp-fg.c`](https://github.com/LineageOS/android_kernel_xiaomi_sdm845/blob/lineage-22.2/drivers/power/supply/qcom/qpnp-fg.c)。未采纳“待 provenance closure 后再核查输入哈希”的前瞻建议；本次修改已重算 `sources.lock.json` 中实际改动的构建输入哈希，但该建议指向未来来源闭合任务，不是本轮可复现缺陷。

## 验证记录

环境：macOS Darwin arm64，zsh。工作区 `HEAD` 与当时 PR #2 发布 SHA 一致，源码修改仍未提交。

| 检查 | 结果 |
| --- | --- |
| `git diff --check` | 通过，退出码 0 |
| `python3 -X utf8 tools/check_repo.py` | 通过，退出码 0 |
| `python3 -X utf8 -m unittest discover -s tests -v` | 75 项，61 通过、14 跳过、0 失败。跳过项需要 Linux Bash 或 Linux GCC/伪终端 |
| `shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` | 通过，退出码 0 |
| `bash tools/build_kernel_l2_macos.sh` | 通过，退出码 0；固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，aarch64 GCC 13.3.0。检测到原缓存中的 `Documentation/devicetree/bindings/power/supply/qcom,pmi8998-charger.yaml` 与锁定源码不符后，保留原缓存并使用临时源码/构建树完成构建 |

此次工作树 L2 的 `boot.img` SHA-256 为 `4396142a6882685a9ab7f2f177064cf4c2b96e43327bba2c7f44e9b332b3ca6c`；编译对象 `qcom_fg.o` 为 `ad376378a3981f10d4e6b0a510372fea5564fea023067e444464e4d50860a6a0`，`qcom_pm8150b_charger.o` 为 `fe4314a15987f96f089f3a54f447e549a56dbb5c5b4a952490cce5560e1165ff`。这是构建证据，不代表真机行为。

PR 远端 CI 的此前成功结果只适用于原发布 SHA，不覆盖本记录的修复。推送后仍须确认新 HEAD 的约定 CI，并完成开发者以外的独立专项复核；本轮没有新增 L3 证据，不得据本地 L2 结果推断设备安全或合并条件已满足。
