# PR #4 CI 修复记录

PR：https://github.com/Ben8368/MiSeed/pull/4

失败 HEAD：`63c3a04f0d93b92c5ec97d15f601589d5f52071c`

首次运行：[GitHub Actions run 61](https://github.com/Ben8368/MiSeed/actions/runs/42640323270)

## 失败与修复

- Repository L0 在 Ubuntu、macOS、Windows 均因 `docs/platforms.md` 为 60 行 / 3708 字符而失败，预算为 60 行 / 3500 字符。压缩重复状态描述后为 60 行 / 3482 字符；`tools/check_repo.py` 通过。
- 本地 L1 回归发现 `test_power_port` 的构建配置夹具没有包含新增必选项 `CONFIG_GPIOLIB=y` 与 `CONFIG_MISEED_NFC_NQ=y`。补入夹具后，macOS 原生 `python3 -X utf8 -m unittest discover -s tests -v` 共 136 项通过，22 项因缺少 Linux 专用工具跳过。
- 使用仓库现有 Ubuntu 24.04 ARM64 Colima 镜像（Python 3.12）重跑同一 L1 命令，136 项全部通过，无跳过；无需安装新的宿主软件。
- `python3 -X utf8 tools/check_repo.py`、`python3 tools/check_build_lock.py` 与 `git diff --check` 通过。

修复后的托管 CI 结果须在新 HEAD 运行结束后补记；不得将本地回归结果表述为 GitHub CI 通过。
