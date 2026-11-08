# 2026-11-08 PPS、DTBO 与 GPIO 续查

## 范围与已有证据

承接 RQ-12 / WP9、RQ-08；用户要求继续从 Lineage 与其他小米 10 项目解决剩余问题，并核查历史采样是否已包含设备信息。沿用 PR #3 的 `codex/umi-pd-pps-fastcharge`，本轮基点 `78bcb2654d0a03f6e42a09544d0a77c01764b6b4`。Windows 10.0.26200 x64 / PowerShell 7.6.9，构建为 Ubuntu 24.04 WSL x86_64。没有连接或操作手机，没有 RAM boot、刷写、块设备访问或真实供电修改。

历史已观测 umi / Mi 10 / kona、Android 16、`4.19.325-cip131-st15-perf-g71b13e62f057`；国行、12GB/256GB 为用户报告。不能再笼统要求用户重报这些字段。精确板号、实际泵 ID、充电器铭牌和 PDO/APDO 未见于现有归档；早期 `local/` 及 Mac 真机原始日志不在本 Windows 工作区，不能据此断言原始日志绝无这些字段。既有 FG/SMB5 采样不包含充电泵身份或 PD source capabilities。

## 固定来源与采用范围

| 来源 | 固定提交 | 结论 |
| --- | --- | --- |
| LineageOS/android_kernel_xiaomi_sm8250 | `71b13e62f057a649b77fe4062feb73ee72ad609c` | 核对 Umi DTS/pinctrl、Kona pinctrl、BQ 驱动与 PD cached getters；沿用前次寄存器与策略来源 |
| misaka-link/umi-linux | `cf83073887899f485a5c606f9842c5e691ea796c` | PD PHY 用 L2A；VBUS 限制 0.5–3 A；GPIO 保留 28–31、40–43、52–55，BQ 节点仍注释/禁用 |
| colorgarden/umi-mainline | `5fa2e0048f89b82355d2a9de89c1d74ab9910959` | PD PHY/VBUS 描述一致，但保留范围更大且包括 GPIO8/68；拒绝把其全部 GPIO 掩码当成 PPS 可用配置 |
| MiCode/Xiaomi_Kernel_OpenSource，umi-q-oss | `db67ca6001320efc1f945c270635434fc403a9d4` | 官方 BQ 驱动同样关闭 watchdog，sense 字段没有有效解析；未提供可直接采用的完整保护闭环 |
| MiCode/kernel_devicetree，umi-q-oss | `2aa073826fa357cc672c1e0f590d9dbe904203b0` | `qcom/umi-sm8250.dtsi` 同样注释 2 mΩ / 8 A / 7.5 A，关闭电池 OCP 与热敏保护；不能据此填成本机保护校准值 |

具体路径与 SHA-256 在 sources.lock.json；没有构建期网络依赖或新增内核补丁。小米官方 DTS 在独立 kernel_devicetree 仓库，未以错误路径的 404 结论替代实际检查。官方内核、DT 源码仅作资料核对，未复制；PD 接线与约束的 BSD-3-Clause 来源、The Linux Foundation 归属在 power/NOTICE.md。Lineage 与本地派生内核文件仍为 GPL-2.0-only；主线源码不修改。

## 重复 PMIC 的原因与修复

既有生成器遇到同名 label 就复用主线节点。已锁定本地 MIUI OS1.0.7.0.TJBCNXM 的 DTBO（SHA-256 `d6a6f4c07d7ce88f6213cfed0cee26d55f908e8eff38675faadc6f6e4671eee6`）包含向 `spmi_bus` 注入 `qcom,pm8150@0`、`qcom,pm8150b@2` 等节点的 fragments。主线已存在 `pmic@0`/`pmic@2`，于是同一 USID 注册两次；DTBO 还会覆盖总线 cell 属性。其 `soc` fragments 能注入其他下游消费者，`tlmm` fragments 能改变 pinctrl 描述。

生成器新增独立 disabled 接收节点及 disabled `/abl_ovf` 容器。dtc 编译后只重定向 `__symbols__` 中 `soc`、`spmi_bus`、`tlmm` 的外部 overlay 入口，保留 `miseed_live_*` 路径用于诊断；不删除或重建主线节点，不修改任何已编译的 phandle 或主线内部引用。构建在 DTB 副本上完成重定向，不污染 dtc 输出缓存；随后验证实时总线仅有主线 PMIC@0/1/2/3/4/5/a/b、隔离状态和 GPIO 掩码。

这修复已确认的 symbol-target 注入原因；不声称能拦截任意 `target-path` 或启动固件的任意二进制修改。当前手机实际安装的 DTBO 仍未取得哈希。RISK-010 只推进到源码/离线验证，关闭还需当前镜像的启动、SPMI/PON 与 USB 往返 L3。

## GPIO 与供电路径

固定主线将 UART12 接到 SOUTH GPIO34/35，I2C15 接到 SOUTH GPIO44/45；两份 Umi 主线参考都没有保留 34/35。主线 `pinctrl-msm.c` 在 pinmux request 时检查有效掩码，gpiolib 用 gpio-reserved-ranges 排除初始化方向读取。当前最小诊断配置只开放已有消费者 UART12 的 34/35：`<0 34>, <36 145>`，覆盖其余 181 个 GPIO 编号中的所有未使用引脚（包括 UFS reset 180）。TLMM 改为 okay，不批量探测 NORTH，不开放快充 GPIO；该配置未取得本机新 L3。

| 路径 | 来源确认 | 本轮处理 |
| --- | --- | --- |
| I2C15 | `0x884000`、GPIO44/45、QUP15 | 原接线正确；保持禁用，44/45 暂不开放 |
| 泵中断 | GPIO68，下降沿 | 来源用途明确，但两份主线对该脚准入相冲突；仍保留，不能只改 PPS status 就启用 |
| USB 输入断开 | Lineage `vbus-disable-gpio` = GPIO8，active-high；suspend/default 输出低 | 确认它是输入切断信号，不是 PD PHY 电源或 OTG boost；不套用高电平初始化，不接入未审核写操作 |
| 无线充电路径 | NCP3902 / DC / RX GPIO 与 PMIC GPIO1 | 与有线 PPS 分开，未把无线状态组合照搬到有线启泵 |
| PD PHY | `vdd-pdphy-supply = <&vreg_l2a_3p1>` | 补齐缺失引用；L2 按 phandle 验证，不猜电压 |
| TCPM VBUS provider | 主线已有 `vdd-vbus-supply = <&pm8150b_vbus>`；Umi 参考限制 0.5–3 A | 补齐 regulator 约束并验证引用，节点保持 disabled，不加 boot-on/always-on；它是向外供电的 boost，不是接收 PPS 的调压器 |

Type-C 继续 sink/device、仅 5 V/0.5 A 固定回退 + PPS APDO，DWC3 继续 peripheral。Type-C/I2C15/泵/VBUS provider 保持 disabled。正常 USB 控制台依旧不依赖 Type-C 的新启用。TD-002 不能因编译通过就关闭；需要独立复核与新 L3，再逐引脚扩大最小范围。

## 补采入口与不需要重复的信息

`tools/collect_device.py --power` 为可选的只读补充：读取 hwversion/SKU/product.board、uname；从普通权限 dmesg 中仅提取固定 BQ detect_device 的十进制寄存器 0x13 ID，丢弃其他原始日志；读取 usbpd0 的缓存 pdo_h，前后读取 src_cap_id，不一致则该快照不可用。没有 root、重启、写 sysfs、I2C 扫描或寄存器转储。日志不可读、已滚动、多个不同 ID、usbpd0 不存在等均记为部分/缺失，不假定芯片。

固定 Lineage 的 MODEL_NAME 仅返回 master/slave/standalone 工作模式，不能区分 BQ25970、SC8551、NU2105；因此不使用名称判定实际 IC。`get_src_cap_ext` 虽是 sysfs 读取接口，却会发送 PD 消息，明确不在允许列表。PDO 是读取时所连接电源的缓存能力，不等于充电器铭牌；若 ADB 走电脑 USB，不能把电脑能力称作另一充电器的档位。采集功能仅离线测试，本轮未执行手机采集。

## 剩余准入

来源对照已经解决注入原因、最小 GPIO 范围、缺失 PD PHY 电源引用和采集入口；不能解决实际 IC/实板热敏电阻与采样校准、watchdog 刷新/超时停泵及 VOUT_OVP 位语义。官方源码也未提供可直接移植的缺失值。先查可访问的既有 Android 启动日志、匹配板级资料与被动 PD 缓存，无需先拆机。若这些证据仍无法确定器件/电阻网络，再考虑专业硬件检查；协议/驱动问题不靠拆机解决。

下一阶段顺序：独立审查 → 明确授权及回退方案 → 新镜像仅验证启动、UART/TLMM、唯一 SPMI/PON/USB 往返 → 获取 GPIO8/68 和芯片/保护证据 → 不启泵的绑定/读回 → 另行准入 PD/PPS 与有界充电。当前分支不声称快充可用，不继承参考 Android 的 L3。

## 本轮验证

源码修改后、完整验证前已输出 Audit。最终结果：

| 层级 / 环境 | 命令 | 结果 |
| --- | --- | --- |
| L0 / Windows | `python -X utf8 tools/check_repo.py`、`git diff --check` | 0 / 通过 |
| L1 / Windows Python 3.14 | `python -X utf8 -m unittest discover -s tests -v` | 0 / 89 项，45 通过、44 项 POSIX/编译器/libfdt 等平台相关跳过 |
| L1 / WSL | `python3 -m unittest discover -s tests -v` | 0 / 89 项全部通过，无跳过 |
| L0 / WSL | `shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` | 0 / 无 error |
| L2 / WSL | `MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/pps-followup-l2 bash tools/build_kernel_l2.sh` | 0 / ARM64 镜像完整构建；入口强制运行 5 项真实 libfdt 正反例 |
| 本地参考 DTBO | `python3 -u .work/pps-followup-reference/verify_overlays.py` | 0 / 29 项逐个叠加全部成功，主线 SPMI/TLMM 子树属性值与 phandle 逐字节一致 |

L2 仍为 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`、GCC 13.3.0（Ubuntu 13.3.0-6ubuntu2~24.04.1）、make 4.3。最终 DTB SHA-256 `fa8de9f861f5d4cf5385f2667db9e50d019ec37cc16be973818cc37b814a7b13`；boot.img SHA-256 `b206851346c7efbbdadacdd5015feb15745134281ee5f360fc3b15e26d1770f0`。原始日志、参考 DTBO、叠加产物及 boot.img 均仅在忽略的 `.work/`，不提交。

29 个 DTBO 是已锁定的本地官方包参考，逐个独立应用；不代表当前手机 ABL 实际选择或安装的 DTBO 已验证。构建门禁保留合成的 libfdt 注入正反例，不把厂商二进制加入 CI。中间一次 89 项回归仅因 platforms 活动文档超预算失败，压缩后完整复跑通过；最终结果不把该失败计为通过。

本轮开始时查询，旧 HEAD `78bcb2654d0a03f6e42a09544d0a77c01764b6b4` 的远端六项 CI 已全部成功，CodeRabbit 跳过；这是追加修复之前的结果。新提交 CI 须另查精确 PR HEAD。本轮没有 PPS、GPIO 或 PMIC 新 L3，也没有独立人工专项复核；RISK-004、RISK-010、TD-002 保留相应运行准入边界。
