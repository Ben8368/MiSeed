# 源码篇幅门禁落地（2026-11-10）

基于 `8b6a9c2b54b1bd9c17d0efe0f153802a45e09912` 工作树，宿主 Windows 10.0.26200 / x64，PowerShell 7.6.9。仅调整治理文档、L0 检查器和离线回归；无设备操作、提交、推送或合并。规则正文见 [QUALITY](../../QUALITY.md#源码篇幅门禁)，黄灯归 [TD-008](../../TECH_DEBT.md)。

## 已知阻断与拆分责任

不设置历史基线或例外；现有红灯使全量仓库检查失败，债务登记不解除阻断。后续须按职责拆分并完成适用验证，本轮不改产品源码：

| 文件 | 行数 | 责任 | 待评估的职责边界 |
| --- | ---: | --- | --- |
| kernel/power/qcom_fg.c | 1513 | WP9 | 寄存器/SRAM、代际读数、power_supply 与生命周期 |
| kernel/power/qcom_pm8150b_charger.c | 1263 | WP9 | 有线 SMB5 硬件、标准属性、板级策略/诊断；保持安全互锁 |
| kernel/rescue_init.c | 1109 | WP2 | 初始化、USB 控制台、设备/NFC 诊断、有线控制 |
| kernel/power/umi_pps.c | 1099 | WP9 | 有线 PPS 状态机、保护/采样、thermal 与生命周期 |
| tests/test_rescue_init.py | 661 | WP2 | C 测试桩与 Python 场景组织 |
| tests/test_power_port.py | 653 | WP9 | 移植接线、FG/SMB5 ABI、有线保护测试 |

未修改产品驱动、DTS、构建配方及来源锁。后续有线驱动拆分须保持锁、所有权、停机/恢复语义；无线 RX/TX 独立设计。行数可自动检查，职责及安全语义须人工复核。

## 验证

| 命令 | 退出码 | 结果 |
| --- | ---: | --- |
| `python -X utf8 tools/check_repo.py` | 1 | 仅上述 6 项红灯失败，同时打印 3 项黄灯；文本、文档预算、链接和来源锁无其他错误 |
| `python -X utf8 -m unittest discover -s tests -v` | 0 | 共 179 项，106 通过、73 跳过；新增 7 项门禁回归均通过 |
| `git diff --check` | 0 | 已跟踪改动无空白错误；新文件另由仓库文本检查覆盖 |

回归覆盖 300/301/600/601 边界、空行/注释/末尾换行、源码与非源码范围、Git 已跟踪但忽略及未跟踪新源码、无 Git 排除目录、债务不豁免红灯、CLI 红灯退出 1 和黄灯退出 0。原全仓断言已明确隔离新增源码门禁，其余规则继续验证；新集成回归独立验证实际扫描与退出码，不在生产检查器中豁免现有超限。

73 项跳过涉及 Linux/POSIX、C 编译器、libfdt/dtc 或已编译 DTB/dtschema。本次仅覆盖本机 L0/L1；未运行远端 CI、L2、L3，治理门禁合并前仍需开发者以外专项复核。现有 workflow 已调用检查器，无需改 workflow；未执行 Shell 改动。
