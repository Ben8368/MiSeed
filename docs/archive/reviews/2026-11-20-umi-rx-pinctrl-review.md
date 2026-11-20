# Umi 无线 RX pinctrl 与原生 DSI 复核（2026-11-20）

基线 `3350aa1` 加本轮工作树。宿主 Windows x64 / PowerShell 7.6.9，构建与 libfdt 回归在 Ubuntu 24.04 WSL2 x86_64。使用锁定的 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、arm64 GCC 13.3.0、GNU Make 4.3、dtschema 2025.8 和 yamllint 1.38.0。

## 审查结论

- 无线 RX 保持 I²C/regmap、gpiod/IRQ 与只读 `power_supply` 接口。默认状态只配置 PM8150B GPIO7 输入，移除 GPIO1 无线泵使能状态和 TLMM155 的默认配置。GPIO155 由 `enable` 实验调用 gpiod。两种显示配置均保留 GPIO157，当前驱动使用 GPIO7 的边沿中断。标准用户态仍读取 RX `online`、`voltage_now`、`current_now`。
- [旧镜像 L3](../hardware/2026-11-20-umi-rx-pinctrl-l3.md) 保留诊断模式下的驱动绑定和电池电流观测。RISK-013 跟踪普通模式双输入下有线 SMB5/PPS 退出与恢复、无线 RX 的 power-good 和保护。无线 TX 独立。

## 验证

- L0 `python -X utf8 tools/check_repo.py`、`git diff --check`、`shellcheck --severity=error tools/build_kernel_l2.sh` 均返回 0。
- L1 `python3 -m unittest discover -s tests -q` 返回 0，326 项中 322 通过、4 项定向 schema 测试跳过。`test_abl_isolation.py` 的 8 项均运行。以 `3350aa1` 的两份 DTSI 分别替换测试输入，原生 DSI 样本被 GPIO 表检查拒绝，无线 RX 样本被默认 pinctrl 检查拒绝。
- L2 各用独立的锁定源码、构建和输出目录运行 `MISEED_RX_PROFILE=normal MISEED_NATIVE_DSI=<0|1> MISEED_JOBS=7 bash tools/build_kernel_l2.sh`，两个配置均完成 arm64 编译与打包。
- 分别运行 `python tools/validate_kernel_dt.py --source <source> --build <build> --dtb <output>/umi-csot.dtb`，两个配置均通过定向 binding、板级与最终 DTB 检查，分别运行全部 4 项 DT 正反例。再次读取最终产物并校验 profile、隔离和 RF 资源，返回 0。构建与 schema 日志位置见本机 `.work/review-rx-20261120-wsl-root.txt`。

| 配置 | boot.img SHA-256 | 最终 DTB SHA-256 |
| --- | --- | --- |
| 默认显示 | `b5d732dc0e7851fdbad7edecff07b57d175ec1a2508f2dc37c27e56219aa08af` | `083419757aa405955f169c08852e97e97f6de40dcb543df0c10219c2acac25bc` |
| 原生 DSI | `791fd8750f5e7beaa4dddd5c626f0353c319416120c6976c404c9a704e1ce557` | `1dcb700da40ae6db9ce4f668880ebd7274001b2f1e73ab242a8f6103ed257d2d` |

新镜像 L3、独立供电复核和本次改动的远端 CI 待验。
