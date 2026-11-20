# PR #8 本地收尾复核（2026-11-20）

审查覆盖 `c9022adf9374dedb161fd20c3d6af954519127c5`，远端基线规模 2,363 行新增、78 个文件。以下结论来自逐项对照工作树、锁定 Linux v6.18.60 和离线反例。[CodeRabbit 审查](https://github.com/Ben8368/MiSeed/pull/8#pullrequestreview-5988876592)

| 行内意见 | 核实和处置 |
| --- | --- |
| 7738 · NFC 文档 | `normal` 禁用 NCI RF 门控。文档明确 `coex` 前提与 eSE 范围。 |
| 7746 · 断线取证 | TCPM `PORT_RESET` 可注销 partner，`ONLINE` 反映 sink 使能。探针要求 partner 缺席，并核对近期同一 TCPC 的 CC 与 VBUS，证据未齐时继续等待拔线。 |
| 7754 · RF 解禁竞态 | NCI 最终清门控时在设备锁内检查 RX 通知代数并复读 `ONLINE`。旧代数或读取失败会保持门控。 |
| 7761 · PPS 认领 | 认领 API 返回实际所有权。`-ENOLINK` 等认领前失败可重试，认领后写入失败保留所有权供安全收尾。 |
| 7768 · PPS 暂停 | 运行中测到 RX 在线时转可恢复暂停，保留停泵、恢复固定 PD、最后释放 SMB5 的顺序。RX 读取错误进入故障处理。 |
| 7772、7787 · 有线会话 | USBIN_PLUGIN 可受 RX 供电影响，不能直接用于拔线复位。复用锁定 TCPC 的 `CC_ATTACHED` / `VBUS_DETECT`，保留 RX 遮蔽时的旁路、PD、热和故障状态，并处理 RX 期间真正的有线拔插。 |
| 7775、7797 · RX GPIO | 实验镜像的 TLMM155 在失败与解绑时解除使能。GPIO7 明确输入且关闭输出，沿用既有电源轨和下拉。真实引脚电平与时序列入 WP12。 |
| 7779 · 启泵窗口 | 在 BQ25970 启动前复核 SMB5 所有权、SMB5 禁充及输入挂起状态与 RX。I²C 写入期间仍可能发生边沿，硬件选路及 RQ-15 有线优先继续由 RISK-013 跟踪。 |
| 7816、7820 · 构建与 DT | 对工作、源码、构建和输出实际路径获取排他锁，覆盖路径别名及显式缓存覆盖。SN100T 检查完整 TLMM 提供者、偏移及有效极性。 |
| 7825 · Android 只读采集 | 在启动 ID 区间内读取机型、内核和 LOS 版本，开始采样前复核 ID。保留工作区原有的有线阶段样本条件。 |
| 7829、7838、7844 · RAM 日志 | 拒绝丢记录或读取错误。以原始 printk 时间归属内核证据，要求读到结果后的周期采样，并保留多词电池状态。`coex` 禁用 suspend，取证时间口径与镜像配置绑定。 |
| 3 条测试建议 | SMB5 回归分别记录 suspend 位与写入次数。每个内建驱动单独做缺失和模块化反例。Windows 断线测试等读取线程实际收到错误后再检查完整帧。 |

有线继续沿用 TCPM 和 `power_supply`。PM8150B TCPC 原始状态仅在驱动内部和 RAM 诊断日志使用。无线 RX 接口保持原有 `power_supply` 属性，NFC 共存沿用 NCI/UAPI，未扩充控制 ABI。

## 复核结果

- Windows x64 / PowerShell 7.6.9，Python 3.14。`PYTHONUTF8=1 GIT_CONFIG_COUNT=0 python -m unittest discover -s tests -q` 返回 0，331 项中通过 200 项，131 项因宿主缺依赖或不适用而跳过。环境变量仅用于该进程树。
- Windows + WSL Ubuntu 24.04 x86_64 / Bash。`python3 -m unittest discover -s tests -v` 返回 0，331 项中通过 327 项，4 项专项 DT 测试由 schema 流程另跑。`shellcheck --severity=error tools/build_kernel_l2.sh` 与 `bash -n tools/build_kernel_l2.sh` 返回 0。
- 仓库 `python tools/check_repo.py` 与 `git diff --check` 返回 0。源码篇幅黄灯见 TD-008。
- 固定 Linux v6.18.60 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，AArch64 GCC 13.3.0。`MISEED_NATIVE_DSI=0 MISEED_RX_PROFILE=<profile> bash tools/build_kernel_l2.sh` 构建与打包结果见下表，工作与输出目录保留在本机。`tools/validate_kernel_dt.py` 校验定向 binding、板级和打包 DTB，另跑 4 个 DT 回归。`normal` 的最终 DTB 另按 `pinctrl/qcom,pmic-gpio.yaml` 检查，无诊断输出。本轮使用 ABL framebuffer 构建配置。

| Profile | L2 | 定向 DT | `boot.img` SHA-256 |
| --- | --- | --- | --- |
| `normal` | 0 | 0 | `ce531d6223e491e09d8e42cb3cacfc6b8a4d316c1d551275b073a8eae8954dc8` |
| `coex` | 0 | 0 | `29ee74e30805469b456422dad5c43466d6f4d1c4c62e5dcb110efa4ac242e6b8` |
| `enable` | 0 | 0 | `793598e5c141f94fafabd17b300c4014a453e02558f3bdcffb89ce88aeb9c59c` |

远端 CI 成功仅对应 `c9022adf9374dedb161fd20c3d6af954519127c5`。本地收尾的电源与 NFC 专项 L3 及开发者以外复核仍开放。WP9/WP12 核实 GPIO7/155 和两路输入的电气归属后才验证 RQ-15 的无线断流和有线保留。[远端 CI](https://github.com/Ben8368/MiSeed/actions/runs/44691799514)
