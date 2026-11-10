# 2026-11-10 PR #5 审核核实与修复

基线 `5f04d36c54b00f15e9d96927f49c89920a4dafdd`，分支 `codex/mainline-followup`；核实 CodeRabbit 2026-11-10 的 1 条正式意见、2 条 nitpick 及覆盖率提示。审查意见仅作输入，结论来自本地 Git 历史与当前实现。

## 逐项结论

| 意见 | 核实与处理 |
| --- | --- |
| 历史 FG/SMB5 链接不对应审计版本 | 确认。`42f54a2` 使用未拆分文件，且该目录与首次归档提交间无变化；恢复原文件名并固定到完整基线 SHA。同表其余驱动/配置/工具链接一起固定，防止后续实现倒灌历史证据。另说明审计时未提交的 NFC 元数据诊断、测试、来源锁及文档差异，rescue 链接固定到包含诊断的首次归档提交 |
| PPS/NFC 注释错位、USB 尾部孤立注释 | 确认。`pps_control_path` 查找有线 BQ25970 的 `charge_behaviour`，调用者可写入，不能声称绝无供电变化。恢复有线 PPS 查找说明，把只读 NFC 注释放回 `report_nfc_status`，删除孤立注释；不改可执行语句 |
| 来源锁 build 描述重复三遍 | 确认。保留一遍；说明字段不参与输入哈希，但三份 C 文件注释变化必须更新各自 SHA-256，已同步 |
| docstring 覆盖率 21.18% 低于机器人 80% 阈值 | 不采纳为修复目标。仓库没有该比例门槛，提示未指出具体缺失契约；保留必要注释，本轮不批量扩写 |

## Audit 与边界

修改后、完整验证前已汇报 🚦 Audit Report。本轮仅证据/说明/注释修复，不引入接口或供电参数变更；有线 PPS 与无线 RX/TX 分开，无线未涉及，无设备操作。8 项源码篇幅黄灯仍归 TD-008；PR 规模红灯维持既有收尾，文件硬上限按推送后实测核对。

开发者以外专项复核、适用新驱动/ABI L3、精确 HEAD 托管 CI 等既有合并验收边界未闭合。本轮为开发者自查，不合并、不发布；按维护者要求不查询、不等待新 HEAD CI。

## 验证

宿主 Windows 10.0.26200 / X64 / PowerShell 7.6.9；C 回归和 ARM64 交叉编译在 WSL Linux x86_64 执行。工作目录分别为 `C:\MiSeed`、`/mnt/c/MiSeed`。ARM64 GCC 为 Ubuntu 13.3.0-6ubuntu2~24.04.1。

| 检查 | 准确命令 / 结果 |
| --- | --- |
| 来源锁正反例 | Windows：`python -X utf8 -m unittest discover -s tests -p test_build_lock.py -v`：12 项，10 通过、2 项 POSIX fixture 跳过；WSL 全量覆盖这 12 项，均通过 |
| WSL 离线全量 | `python3 -m unittest discover -s tests -v`：181 项，178 通过、3 项 DTB/schema 输入专项跳过，退出 0；无 DTS/binding 变更，本轮不重新验证该硬件配置 |
| C 语义自查 | 三份修改文件分别提取 `git show 5f04d36c54b00f15e9d96927f49c89920a4dafdd:kernel/<file>`，对原文件与当前文件执行 `aarch64-linux-gnu-gcc -E -P -I kernel <file>`，三组输出逐字节相同 |
| 救援 ARM64 L2 | `aarch64-linux-gnu-gcc -Os -static -s -Wall -Wextra -Werror -o .work/pr5-review-rescue-init kernel/rescue_init.c kernel/rescue_fs.c kernel/rescue_usb_diag.c kernel/rescue_devices.c kernel/rescue_power.c kernel/rescue_nfc.c kernel/rescue_usb.c kernel/charge_log.c`：退出 0；`aarch64-linux-gnu-readelf -h/-l` 确认 AArch64 且无 INTERP；SHA-256 `014147ca4f21bedd347b0450b18a3fcea07227548767264e877631c0c3fce1cd` |
| 历史证据 | 从修复后的审计记录提取 18 个 GitHub blob 链接，以 `git cat-file -e <SHA>:<path>` 验证全部存在；对基线链接逐项 `git diff --exit-code 42f54a2 abe000d -- <path>`，均无差异，退出 0 |
| 仓库 / 差异 | `python -X utf8 tools/check_repo.py`、`python -X utf8 tools/check_build_lock.py`、`git diff --check`：最终均退出 0；检查活动文档预算、来源锁与源码篇幅，只有 8 项既有黄灯。首次差异检查发现移除孤立注释后 EOF 多余空行，已清理并更新哈希 |

未重建 Linux Image/DTB，未运行 L3；本次 L2 仅覆盖受注释修改影响的静态救援程序，不把既有完整内核构建或真机证据转记为当前提交结果。全量回归在末尾空行清理前执行，清理后源码预处理一致性、ARM64 L2 与来源锁专项再次通过。原始日志与产物保留在忽略的 `.work/`，不提交。
