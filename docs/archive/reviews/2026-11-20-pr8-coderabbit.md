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

## L3 接入前修订

在 `29fd8b4c86b7c4467002562738eeb85fc52c0bcc` 加本轮工作树上修复取证误判。RAM 记录保留 RX1619 power-good 中断消息。`tools/rx_nfc_evidence.py` 使用原始内核时间，要求 PG 上升后、PG 下降与离座记录前完成 NCI RF idle。`poll` 与 `target` 均有时间边界及缺失/错误 PG 反例。

- Windows x64 / PowerShell 7.6.9：`python -m unittest discover -s tests -q` 返回 0，332 项中 201 通过、131 跳过。WSL Ubuntu 24.04 x86_64 / Bash：`python3 -m unittest discover -s tests -q` 返回 0，328 通过、4 项定向 DT 回归另跑。最终共存专项在 WSL 通过 5 项。仓库检查、差异空白检查、构建脚本 ShellCheck error 和 Bash 语法均通过。
- WSL 使用 Linux v6.18.60 `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 与 AArch64 GCC 13.3.0。`MISEED_NATIVE_DSI=0 MISEED_RX_PROFILE=<profile> bash tools/build_kernel_l2.sh` 与 `tools/validate_kernel_dt.py` 四组通过。各组另核打包 DTB 的 `pinctrl/qcom,pmic-gpio.yaml`，无诊断。

| Profile | L2 | 定向 DT | `boot.img` SHA-256 |
| --- | --- | --- | --- |
| `normal` | 0 | 0 | `7507879e8224ab56e2e59207e261e402d176a6df02edd87591299d9f2ecdbd2f` |
| `observe` | 0 | 0 | `32c7e40fa2000e3656e1ae32c88ff218fa9dd15abb0ae7280e792c3a7bc7654d` |
| `enable` | 0 | 0 | `519a4b5f182eb651d79489b02fe0e7018d115004065352861a74412853ebaa27` |
| `coex` | 0 | 0 | `2c6d2f99d4fb1458a299083d3dab98ff9ebe92ed975e21f23e9eacdffb44e71e` |

本机 `.work/pr8-l3-ready-20261120/` 保存产物与主机验收手册。`preflight.py --check` 和 `coex/run.py --check` 均通过，核对工具、配置及镜像内嵌 DTB。用既有 1,190 行 TCPM 记录回放采集，并验证行数不符时保留残帧。无线 PG、NFC 共存及有线自然工况待接机。RQ-15 的关断实现依赖 GPIO155 与无线支路断流关联证据，当前可执行 LOS 只读对照，实施条件见 RISK-013。有线保护注入仍需核定对应台架边界。本轮未执行设备操作。

## 基于 observe 实测的诊断修订

- [收尾版 observe L3](../hardware/2026-11-20-umi-rx-pinctrl-l3.md#收尾版-observe-复验) 重现上座期间 `cc_status=0x22`（CC 未连接、VBUS 检出），同时 TCPM partner 缺席。锁定 Linux v6.18.60 的 PM8150B TCPC 对两位独立读取，不能把 VBUS 位解释为外部电缆身份。
- 共存探针在离座首次拔线时仍要求 partner 缺席、CC/VBUS 均未检出。仅在上座观察期间容许 `CC=0/VBUS=1`，须有近期且原始位一致的 TCPC 读回、partner 缺席及 SMB5 禁充；离座恢复轮询前重新要求 VBUS 未检出。主机验收另核上座与离座事件、PG/IRQ、RF idle 及无丢帧。真正 CC/partner 接入、过期、读错或位矛盾不算通过。
- `observe` / `coex` 等 RX 诊断镜像沿现有 `ONLINE` GPIO 读回输出限频的原始/逻辑电平或 errno、极性与 IRQ 计数。未新增 I²C 访问、未改变 GPIO 极性、使能、RX 遥测门控或 SMB5/PPS 控制。`PG=0` 的物理原因仍需复验，不用 VBUS 推导 PG。
- 离线回放 18 组真实 `0x22` 上座状态，新判据可解析，原 `observe` 日志仍不满足共存验收。Linux RX 专项 8 项通过（含真实 C 判据的严格/上座、CC、SMB5、过期、丢失反例）；Windows 全量 334 项中 202 通过、132 跳过，WSL 330 通过、4 项定向 DT 另跑。`git diff --check`、仓库检查、ShellCheck error、`bash -n` 通过。

### 诊断修订的 L2 与验收包

WSL Ubuntu 24.04 x86_64 / Bash，固定 Linux v6.18.60 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、AArch64 GCC 13.3.0。`MISEED_NATIVE_DSI=0 MISEED_RX_PROFILE=<profile> bash tools/build_kernel_l2.sh` 使用 ABL framebuffer；`tools/validate_kernel_dt.py` 分别核验定向 binding、板级和打包 DTB，每组 4 项 DT 回归均通过，无跳过。

| Profile | L2 | 定向 DT | `boot.img` SHA-256 |
| --- | --- | --- | --- |
| `observe` | 0 | 0 | `b62e56398d1fce45f319c8339031135b08e3c43c893ba3f8b3934dbae37d2343` |
| `coex` | 0 | 0 | `f0baa84be034efd64333f56c8f6e7af6354cdd9489025a3a612fb5a41c0f94be` |
| `normal` | 0 | 0 | `d0518658edffa8d33a354a1641f92af7c491f20ffdc0ea7a3475fa82f7acf473` |

该轮候选包保存在 `.work/pr8-pg-followup-20261120/`，现为历史证据。`preflight.py --check` 和 `coex/run.py --check` 均返回 0，校验镜像哈希、配置和内嵌 DTB。未构建 `enable`，未重复上机。先用 `observe` 核对离座基线及上/离座 PG、IRQ；通过后再安排 `coex` 的 `poll` / `target` 真机验收。RQ-15 与独立专项复核仍开放。

## 上座有功率迹象但 PG 仍低：定位修订

[已确认底座供电的 L3](../hardware/2026-11-20-umi-rx-pinctrl-l3.md#确认底座供电后的-observe-复验) 同时见到电池放电从约 409–449 mA 降至 1.5–3.9 mA、TCPC `0x22`、56 条上座 GPIO7 读数为低且 IRQ=0。此前 [LOS 无线对照](../hardware/2026-11-19-umi-wireless-rx-baseline.md#无线对照与验收) 同机有 `wireless_power_good_en=1` 和输出读数。其 `rx1619_wpc_det_work()` 以 `gpio_get_value()` 更新该属性，不等同外部测得的 GPIO 电压。

- [锁定 LOS Umi DT](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/arch/arm64/boot/dts/vendor/qcom/umi-sm8250.dtsi#L132-L145) 与 MiSeed 均声明 PM8150B GPIO7 高有效。[LOS GPIO7 pinctrl](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/arch/arm64/boot/dts/vendor/qcom/kona-pmic-overlay.dtsi#L202-L211)（SHA-256 `f9ce1a725dab301e86b22c30f11a8a68520d3a66eb3dbc4e959c60e3329a5ac8`）声明正常功能、下拉、输入和 `power-source=<1>`，MiSeed 选 `<0>` 并明确禁用输出。锁定 [主线 SPMI GPIO](https://github.com/gregkh/linux/blob/f45ed76d6791cf72ff1265ee93511175bbbc2f18/drivers/pinctrl/qcom/pinctrl-spmi-gpio.c) 的 VIN 可选值由引脚 subtype 决定；LOS 静态声明不足以证明 GPIO7 真机实际 VIN 或 PG 阈值，旧镜像 GPIO1 的 pinctrl 错误也不能直接套到 GPIO7。不修改电气设置，待运行时配置或独立量测。
- [LOS RX1619](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/blob/71b13e62f057a649b77fe4062feb73ee72ad609c/drivers/power/supply/nuvolta/rx1619.c) 的 `determine_initial_status()` 读取 `0x09` 不依赖 PG；RX/TX 都会读取该地址。现有 MiSeed 产品属性以 PG 门控，旧 `observe` 因此不能区分 PG 低时寄存器可读还是 I²C 失败。仅 `observe` 以 Linux workqueue 与 regmap 每 2 s 采样一次，最多 150 次；先读 PG，仅低电平时读一个原始字节，成功后再次读 PG。记录原值及两次 PG 或 errno，不把读通认作有效 RX 遥测。devres 在 IRQ、供电属性和 GPIO 资源释放前同步取消工作。
- GENI I²C/regmap、GPIO/IRQ 与标准 `power_supply` 属性/uevent 保持原契约；诊断只走既有 RAM 日志，不引入 LOS 私有 ABI。`normal`、`coex`、`enable` 不运行此探针。未触碰 GPIO155、GPIO1、寄存器写入、无线 TX 或有线 SMB5/PPS。下次只比较有效上座窗口内的寄存器读回、PG/IRQ 和离座基线；未确认 PG 边沿前不进入 `coex`，不据 `0x22`、电流或 `0x09` 设置 `ONLINE`。

### 定位修订的离线结果

- WSL `python3 -m unittest discover -s tests -p 'test_rx*.py' -v`：8 项通过。RX1619 C 回归实际执行原始字节成功、I²C/GPIO 读错、读取中 PG 变化、150 次截止、四种 profile 门控，并保留原属性行为反例。全量 334 项中 WSL 330 通过/4 项 DT 另跑，Windows 202 通过/132 跳过。仓库检查、差异空白、ShellCheck error、`bash -n` 通过。
- WSL Ubuntu 24.04 x86_64，固定 Linux v6.18.60 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、AArch64 GCC 13.3.0；`MISEED_NATIVE_DSI=0 MISEED_RX_PROFILE=<profile> bash tools/build_kernel_l2.sh` 编译打包。`tools/validate_kernel_dt.py` 核对定向 binding、板级与最终 DTB，每组四项 DT 回归通过。三组 DTB 与上一轮候选一致。

| Profile | L2 | 定向 DT | `boot.img` SHA-256 |
| --- | --- | --- | --- |
| `observe` | 0 | 0 | `bd1379f02933cc33fbcf1d3f5d882c4c518f50e5ae213c5808c1bb432c6c5821` |
| `coex` | 0 | 0 | `2425c8adb86b2988d6eb0d14588f6b0e6ee9d763c71066d6edbb82e5f2628fef` |
| `normal` | 0 | 0 | `9032afb2d286d493a4f3a1ba0f6fe8a4ee1d4eec93cddc59f66bebce9b35e385` |

新验收包 `.work/pr8-pg-trace-20261120/`，两个 `--check` 均返回 0；核对配置、哈希、内嵌 DTB，确认打包内核中只有 `observe` 含 PG 原始字节探针。`enable` 只跑合成 profile 反例，未构建或上机。旧镜像与日志保留。本轮未操作设备，PG 根因、无线 L3、RQ-15 和独立复核仍开放。

### 提交前 L2 复验（2026-11-20）

- 依上述锁定环境重新执行 `MISEED_NATIVE_DSI=0 MISEED_RX_PROFILE=<profile> bash tools/build_kernel_l2.sh` 和 `tools/validate_kernel_dt.py`：`observe`、`coex`、`normal`、`enable` 均返回 0。每组 4 项定向 DT 回归无跳过，打包 DTB 另过 `pinctrl/qcom,pmic-gpio.yaml`，无诊断。四组内嵌 DTB、配置及 PG 探针隔离复核通过；GPIO7 VIN 仍为 `<0>`，仅 `observe` 含原始字节探针。前三组镜像哈希与上表一致；`enable` 的 `boot.img` 为 `c842486d146244333ac3e3fc3bd31240b037ffa91329da6d22282b57e4c62b94`。产物和日志留在 `.work/pr8-submit-l2-20261120/`。
- 重跑 WSL RX 专项 8 项、全量 330 项（4 项 DT 单跑）；Windows 202 项通过、132 项跳过。L0、差异空白、ShellCheck error、Bash 语法及既有两个验收包预检通过。此次未接机或 RAM 启动。LOS 运行环境与 VIN 读回边界见[只读复核](../hardware/2026-11-19-umi-wireless-rx-baseline.md#2026-11-20gpio7-活动配置与调试接口复核)，无线 PG/IRQ、共存 L3、RQ-15 与独立复核仍开放。
