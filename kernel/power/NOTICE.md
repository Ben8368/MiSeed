# FG / SMB5 来源与归属

本目录两份驱动改编自 colorgarden 的 [umi-mainline](https://github.com/colorgarden/umi-mainline/tree/5fa2e0048f89b82355d2a9de89c1d74ab9910959/power)，固定提交 `5fa2e0048f89b82355d2a9de89c1d74ab9910959`；逐文件 SPDX 为 GPL-2.0-only，许可证全文见 [GPL-2.0-only](../../LICENSES/GPL-2.0-only.txt)。MiSeed 顶层 AGPL 不覆盖这些驱动。

原文件头及 MODULE_AUTHOR 保留：

| 文件 | 原始归属 |
| --- | --- |
| `qcom_fg.c` | The Linux Foundation；Caleb Connolly、Joel Selvaraj、Yassine Oudjana |
| `qcom_pm8150b_charger.c` | Teguh Sobirin；基于 Caleb Connolly 的 PMI8998 charger；保留原 MODULE_AUTHOR |

colorgarden 的文档将其来源追溯到 zhaoyuanwang123/armbian `sm8250-7.2` 的 0024、0025、0028、0037、0039、0041 补丁。这里使用 colorgarden 固定提交中的完整文件，不重新拼接移动补丁序列。感谢这些作者与移植维护者的工作。

MiSeed 修改：移除 FG 的旧版 API 兼容宏、将文件内 IRQ handler 声明为 static；Gen4 仅读外围 readback，不为读数初始化 IMA/DMA 或加载电池 profile。SMB5 采用标准 `charge_behaviour`，修复过压、读取错误及 SMB5 状态枚举；删除旧的 Type-C/禁用 watchdog 初始化序列，改用本机 PM8150B 编码、读取失败停充和有界测试。没有引入 colorgarden 的近似电池 DTS、BQ25970 或关闭保护配置。

misaka-link 的 [umi-linux](https://github.com/misaka-link/umi-linux/tree/cf83073887899f485a5c606f9842c5e691ea796c) 提供 CSOT 设备上的充电区间控制及停止充电电流归零报告，是本次对照证据。README 与其具体 [NOTICE](https://github.com/misaka-link/umi-linux/blob/cf83073887899f485a5c606f9842c5e691ea796c/NOTICE-%E8%AE%B8%E5%8F%AF%E4%B8%8E%E5%87%BA%E5%A4%84.md) 的许可描述不一致，后者明确授权脚本按 GPL-2.0 单独复用。脚本依赖 `charging_enabled`、Bash/systemd 与可信 SOC，本次诊断环境均不具备；未复制脚本或服务，没有把其测试视作 MiSeed 验证。

运行版接线参考 Armbian/build `8ddeef322b4706b5be19d0fb7869f107c117666d` 的 0027（map220v、Jiali Chen）和 0029（Teguh Sobirin、Jiali Chen）；电池设计描述及 70/65 滞回来自 misaka-link。保留 map220v 对 PM8150B Gen4 支持的贡献归属。新增 DTS 派生部分维持 GPL-2.0-only。

PM8150B 的 FV/FCC 编码、watchdog 和 Umi 正常充电温区/电压上限，另对照本机 Android 对应的 [LineageOS 内核](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/tree/71b13e62f057a649b77fe4062feb73ee72ad609c)：`qpnp-smb5.c`、`smb5-reg.h`、`umi-sm8250.dtsi`。厂商代码归 The Linux Foundation 及原贡献者；本地代码只移植已核实的定义和控制逻辑，不引入厂商二进制或认证数据。

先前仅模块编译的评估见[历史记录](../../docs/archive/research/2026-11-08-upstream-power-port.md)。当前运行版默认抑制充电，读数及保护状态核对通过后才准备用户授权的单次插入测试；原始与本地文件哈希继续由来源锁维护。新增显式 600 秒 SOC 豁免入口，原 60 秒默认及其余保护不变；当时 BQ/PD 快充未采用，详见[专项核查](../../docs/archive/reviews/2026-11-08-fastcharge-ci.md)。


## LineageOS PPS 移植

`umi_pps.c`、`umi_pps_policy.h` 和 `umi-pps.dtsi` 从 LineageOS 固定提交 `71b13e62f057a649b77fe4062feb73ee72ad609c` 的 `drivers/power/supply/ti/bq2597x_charger.c`、`bq25970_reg.h`、`pd_policy_manager.c/.h` 以及 Umi/Kona DTS 选择性适配，保持 GPL-2.0-only。BQ 原驱动 Copyright (C) 2017 Texas Instruments、MODULE_AUTHOR Texas Instruments；策略 MODULE_AUTHOR Fei Jiang <jiangfei1@xiaomi.com>，策略头文件保留 a0220433 / Mar 27, 2017 的归属。参考驱动的 TI GPL v2 授权、策略 GPL 模块声明与根 COPYING 一并检查；来源锁记录逐文件哈希。未复制其整个下游 USB PD 实现。

本地修改包括主线 TCPM power_supply 接口、强制板级参数、全保护读回、拒绝未知芯片 ID、SMB5 独占和输入暂停、有界手动测试与失败停止。没有沿用关闭 watchdog/温控的初始化，也未声称保护已通过实机验证。Linux 原生 TCPM/PMIC Type-C/PD PHY 维持内核自身许可证及作者，不改其文件。移植范围、来源报告和运行阻断见[本轮评估](../../docs/archive/research/2026-11-08-lineage-pps-port.md)与[接口说明](PPS.md)。
