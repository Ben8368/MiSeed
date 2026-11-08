# 2026-11-08 快充准入、600 秒诊断窗口与 PR #2 CI

## 范围与环境

维护者要求参考 colorgarden/umi-mainline 移植并启用快充、检查 PR #2 CI，并允许临时绕过 70/65 至少 10 分钟后做充电器插拔实测。本轮不将该授权理解为允许猜测电气参数或关闭异常保护。宿主 Darwin 25.6.0 / arm64 / zsh；分支 `codex/umi-upstream-power-port`，基点 `a176b3c31982da625c40914a37df8ecb4453eee4`。维护者随后明确允许提交并推送 PR #2，并要求先测试采集数据；本记录此阶段修改尚未提交、推送或上机，不合并或发布。

FG/SMB5/ADC5 及断线日志已有[旧镜像的受限 L3](../hardware/2026-11-08-umi-runtime-power.md)，不重复移植。新镜像不含快充，不把编译或旧镜像的 L3 转记为新镜像已上机。

## 快充准入结论：阻断运行移植

通过 GitHub connector 重读固定提交 `5fa2e0048f89b82355d2a9de89c1d74ab9910959` 的 [BQ25970 驱动](https://github.com/colorgarden/umi-mainline/blob/5fa2e0048f89b82355d2a9de89c1d74ab9910959/power/bq25970_charger.c)、电池 DTS 和 `docs/BATTERY.md`；BQ 文件 Git blob 为 `6315672ddc4cd0720f22b5b173f6666b1db3d38a`，与本次 master 查询一致。原驱动署名 Texas Instruments（2017）及 XiaoMi（2021），GPL v2；未复制该驱动或其配置到构建输入。

- `bq2597x_init_device()` 第 1573 行关闭 watchdog；保护、ADC、限流等初始化的返回值未逐项传播，末尾仍可返回成功。
- `bq2597x_notifier_call()` 第 2077–2092 行忽略 ONLINE 读取错误，睡眠 30 秒后直接启泵；没有等待并验证 PD/PPS 合约，也没有本项目电池门控或自动测试期限。仅安装这个驱动不能形成快充控制闭环。
- `sense-resistor-mohm` 读取在第 1384 行被注释，后续仍将该配置传给采样电阻设置。不能填入推测值来声称校准或过流保护有效。
- 上游电池 DTS 关闭电池过流及电池/总线/芯片温控；本机匹配的 Android DTS 也有类似关闭项，不因它来自原厂便照搬。上游文档称镜像等上机验证、PD 未移植、温控/限流未配置，不能引用为快充 L3。
- MiSeed TLMM 因安全 GPIO 尚未锁定而禁用（TD-002）；BQ IRQ GPIO68、I2C15、主充电器/充电泵协调、PD PHY/TCPM/PPS 与保护参数均未完成本机适配。RISK-004 / RISK-010 继续适用。

关闭这些缺口后才可接入并审查快充控制器。仅放宽 SOC 或把 FCC/ICL 改大不构成快充移植，本轮不执行主动高功率充电。

## 已实现的受限测试入口

`charge-test` 仍为 60 秒；新增 `charge-test-10min`，写入 `arm-10min`，显式选择 600 秒。启动默认停充；准备时也停充。必须先观测断线，再在下一次输入接入开始计时，电脑保持连接时不会提前耗尽窗口。600 秒仅豁免 SOC 70/65，温度、电压、健康、在位、有效读数、0.5 A FCC/输入上限、过流与 watchdog 校验全部保留。

窗口在拔出、`charge-stop`、读取/限值故障时可提前结束，不能保证故障情况下继续充满 10 分钟。超时或拔出消费本次准备，恢复 70/65；78% 等高电量下保持停充，再插电脑不会自动重开测试。日志及串口响应报告所选期限。没有增加任意时长或任意电流写入口。

## PR #2 CI 根因与修复

PR HEAD `a176b3c31982da625c40914a37df8ecb4453eee4` 的 [run 42445350685](https://github.com/Ben8368/MiSeed/actions/runs/42445350685) 只有 Windows 回归失败；Boot L2、两个 Ubuntu、macOS、PR commits 均 success。

失败 job `128622999481` 中 `test_build_rejects_modular_or_missing_runtime_power_driver` 的 y/m/n 三个用例均返回 1，而预期为 0/42/42。日志明确提示 Windows Subsystem for Linux 没有已安装的发行版。`shutil.which("bash")` 找到了 WSL 启动器，不能据此认定存在可运行这些 POSIX 路径的 Bash。

该测试现与既有 POSIX Bash 测试保持一致，仅在 `os.name == "posix"` 且有 Bash 时执行；Windows 继续运行可移植测试，Linux/macOS 仍真实执行内建/模块/缺失三组反例，不安装 WSL、不取消 Windows job、不放宽产品构建断言。尚未推送，不能声明远端 Windows CI 已恢复成功。

## 验证与产物

源码改后、完整验证前已输出 Audit Report。快充运行阻断与新窗口缺少 L3/独立专项复核的边界均保留。

- macOS：`python3 -X utf8 -m unittest discover -s tests -v`，67 项，55 执行、12 平台跳过，退出 0；随后新增 Linux 专属串口命令回归由下面全量覆盖。
- Linux：既有 Ubuntu 24.04 arm64 容器，`--network none`，`python3 -X utf8 -m unittest discover -s tests -v`，最终 68 项全部通过，退出 0。结果保留于忽略的 `.work/fastcharge-20261108/linux-regression.log`。
- 实际 C worker / sysfs store 合成回归覆盖默认 60 秒、600 秒边界、jiffies 回绕、准备期间电脑在线不启充、拔出后不重复启用、非法命令、手动停止、USB/BMS/限值故障、过流、温度/电压/health 门控与停充写失败时的 input suspend / 不喂 watchdog。独立串口伪终端回归核实三个命令的真实写值和时间提示。
- L0：`python3 -X utf8 tools/check_repo.py`、`git diff --check`、`shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh`，退出 0；未改 workflow，不涉及 actionlint。
- L2：离线复用已逐文件校验的 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`；GCC 13.3.0、4 jobs，`tools/build_kernel_l2.sh` 退出 0。保留既有 152 ABL / 241 DTBO 符号检查、内建 FG/SMB5 与救援 ELF 检查。

构建命令使用 `docker --context colima-miseed-boot-l2 run --rm --network none --platform linux/arm64`，挂载本仓库到 `/workspace`、既有 volume 到 `/build`；设置 `MISEED_WORK_ROOT=/build`、`MISEED_EXTRACTED_SOURCE=/build/linux-source.mQ9SPb`、`MISEED_BUILD_ROOT=/build/build-clean.AuyWKi`、`MISEED_OUTPUT_ROOT=/workspace/.work/charge-10min-l2-20261108`、`MISEED_JOBS=4`，执行 `miseed-boot-l2:local bash tools/build_kernel_l2.sh`。

| 产物 | 大小 / SHA-256 |
| --- | --- |
| boot.img | 7,405,568 B；`793ca218ce21e8c51f597356e940a9bbfb33aeb56267b0d2a5d6b91a957030c5` |
| Image | `242457ef7f5a317cee2db41842d6743a32636ee69770aab334c83962eb49e2d4` |
| initramfs | 266,568 B；`39e81a9a6f80ee2ed2df7f62939ad067df37430fdbdd287e7f5a01fce38167db` |
| DTB | `f108a562c88b9f7d6ac8393b9657c821b2212abab3bfa60ce08271ed9765bf39` |

完整输入与其余产物哈希见来源锁；镜像留在忽略目录，不进入 Git。

## 本轮设备观测与未执行项

只运行 `python3 tools/collect_charge_log.py --port /dev/cu.usbmodem101 --output .work/fastcharge-20261108/baseline.log --timeout 30`，退出 0，导出 4378 条、覆盖 0。最后样本仍为旧镜像：SOC 77%、34°C、约 4.126 V、`enabled=0`、`rc=0`、`test_armed=0`、`test_active=0`，窗口 60 秒。

本轮尚未重启、启动新镜像、准备 600 秒测试、修改当前手机的策略或执行插拔。没有刷写、分区/格式化、身份/NV 访问或 adb root。新镜像仅 L2；拟用回退仍为串口 `bootloader` → 原生 Fastboot → `fastboot reboot`，失联时电源＋音量下恢复 Fastboot，既有回退依据见上面的旧镜像 L3。
