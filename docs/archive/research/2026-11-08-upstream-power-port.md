# 2026-11-08 umi 电源实现来源评估与编译移植

关联 RQ-03 / RQ-08 / RQ-11，WP1 / WP9。维护者要求参考 colorgarden 与 misaka-link 的充电实现，尊重原作者并构建验证。宿主 Darwin 25.6.0 / arm64 / zsh；工作分支 `codex/umi-upstream-power-port`，基点 `2acf745`。未提交、推送或发布。

## 候选比较与采用范围

| 来源 | 固定源码 | 可复核的原始报告 | 本次决定 |
| --- | --- | --- | --- |
| colorgarden/umi-mainline | `5fa2e0048f89b82355d2a9de89c1d74ab9910959` | `docs/BATTERY.md` 写明驱动已编过、`boot-umi-batt.img` 等上机验证；电池值为近似值，温控策略尚未配置 | 采用 `power/qcom_fg.c`、`power/qcom_pm8150b_charger.c` 做编译移植；不采用电池 DTS、BQ25970 或保护关闭项 |
| misaka-link/umi-linux | `cf83073887899f485a5c606f9842c5e691ea796c` | README 报告 CSOT / umi 上电量可读、70/65 区间控制、37 项测试和停充电流归零；脚本首次引入提交 `0685f9d6c1bd87994fcddd224c77dd08211013a4` | 保留其真机成果与作者归属，作为优先对照线索；不复制脚本/服务或整套 rootfs |

上游页面及目录通过 GitHub connector 重新查询，两个最新树 SHA 均与本项目已有版本锁相同。misaka 的记录支持作者自己的系统可用；没有提供可对应本次 MiSeed 6.18.60、当前设备固件和完整保护配置的专项记录，不转写为 MiSeed L3。README 对原创脚本注明权利保留，但后续查到的同提交 `NOTICE-许可与出处.md` 第 3 项明确允许脚本按 GPL-2.0 单独复用；许可采用该更具体声明，不把 README 的过时描述当成禁止复用依据。本次不直接引入服务是因为诊断环境没有 Bash/systemd、兼容的 `charging_enabled` 和可信 SOC。其 SOC 滞回控制不能单独替代充电硬件保护。

## 文件闭包与署名

直接构建输入是上述 colorgarden 提交的两份完整驱动加 MiSeed 适配，不依赖下载执行上游脚本、子模块、固件或 rootfs。来源追溯到 zhaoyuanwang123/armbian `sm8250-7.2` 的 0024、0025、0028、0037、0039、0041；这些补丁不是本次构建输入，不从其移动分支拼装代码。

原始路径、原文件 SHA-256、本地路径与修改后 SHA-256 保存在 `manifests/sources.lock.json` 的 `colorgarden-umi-reference.power_port_files`；构建连接位于 `kernel/power/Kconfig`、`tools/apply_power_port.py`，没有额外补丁顺序。逐文件 GPL-2.0-only；保留 The Linux Foundation、Caleb Connolly、Joel Selvaraj、Yassine Oudjana、Teguh Sobirin 的原始头与 MODULE_AUTHOR，移植维护者归属见 `kernel/power/NOTICE.md`。

本地修改仅包括：移除 FG 旧 API 宏（6.18 已有新 API），将文件内 IRQ handler 声明为 static 消除编译告警；SMB5 从私有 `charging_enabled` 属性适配标准 `charge_behaviour`，映射 `auto` / `inhibit-charge` 并拒绝其余枚举；空白规范化。没有复制或执行上游属性扩展脚本，不修改 Linux power_supply 核心。

## 安全审查与阻断边界

1. SMB5 `smb5_init_seq` 在 probe 期间解除 USB suspend、启用充电、改变 Type-C、执行上游标为 Disable watchdog 的配置项，随后用电池描述设置 FV，工作队列可根据 APSD 写入限流。没有可信本机电池信息时不得加载。
2. colorgarden 电池 DTS 用 4680 mAh / 4.4 V 近似值，而本机 Android 先前报告 4780 mAh profile；BQ25970 配置关闭电池/总线/芯片温控等保护，拒绝移植这些配置。
3. FG 的 Gen4 ops 仍复用 Gen3 V/I 读法；温度转换和 probe 的 IMA/DMA 写入均未在本机验证。编过不能证明测量或保护有效。
4. 对方 charger DTS 的 IRQ 列表落在 `0x13`，而各功能的真实 IRQ 归属尚未逐项核实；本项目另有 RISK-010 的 DTBO 重复 PMIC。没有复制 IRQ 或增加运行节点。
5. 两份驱动固定为 `=m`，构建器拒绝 `=y` 或缺失；产物独立保留在忽略的 `.work/`。诊断 initramfs 仅含现有 PID1 和节点，不含 `.ko` 或模块加载器，Image 检查没有两份驱动的 probe 符号。不能手动加载这些模块来绕过安全边界。
6. 上游 SMB5 错误路径还需修正并专项验证：过压 IRQ handler 没有检查 regmap_read 返回值就使用输出，health 读法记录过压后仍可能给出 Good；这些是暂缓加载的具体依据，当前仅编译，不把它们当成已生效的安全保护。

准入结论：允许源码与编译移植；主动充电、FG 遥测及驱动加载暂缓，关联 RISK-004 / RISK-010。独立专项复核、保护/错误/恢复路径测试与对应本机授权 L3 仍需完成；登记风险不解除阻断。

## 本地验证

本轮 Audit 与静态修正后的补充均在相应完整验证前输出。最终完整构建、64 项 Linux 离线测试、macOS 64 项测试（52 执行、12 平台跳过）、ShellCheck error 级、仓库 L0 检查和 `git diff --check` 均退出 0。新增测试覆盖内建/缺失驱动拒绝、错误源码版本/符号链接拒绝、重复暂存，以及 `auto` / `inhibit-charge` 映射、非法枚举拒绝与 I/O 错误传播；不代表硬件保护验证。

构建在既有 Ubuntu 24.04 arm64 Colima 容器中断网运行，GCC 13.3.0 / GNU make 4.3。最终构建命令：

```sh
docker --context colima-miseed-boot-l2 run --rm --network none --mount type=bind,source=/Users/ben/Project/Code/MiSeed,target=/workspace --mount type=volume,source=miseed-boot-l2-miseed-boot-l2-arm64,target=/build --workdir /workspace --env MISEED_WORK_ROOT=/build --env MISEED_OUTPUT_ROOT=/workspace/.work/upstream-power-port-l2-20261108 --env MISEED_JOBS=4 miseed-boot-l2:local bash tools/build_kernel_l2.sh
```

宿主执行 `python3 -X utf8 -m unittest discover -s tests -v`、`shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh`、`python3 -X utf8 tools/check_repo.py`、`git diff --check`。Linux 以相同容器、`--network none` 和工作区挂载执行 `python3 -X utf8 -m unittest discover -s tests -v`。

两份原始文件的 `git hash-object` 分别为 `e83d78edeff5dd963202b614b0dcc376de9b6d68` / `30f26d80a327bf6c96b3e1bcc265e8b4040afb7e`，与上游固定树的 blob 一致。最终编译没有 C/DTS 告警或错误；构建入口因旧 Linux cache 含既往移植而拒绝复用，保留旧目录、从固定 Git revision 新建源码/构建目录，这条预期的缓存警告不冒充编译告警。

| 最终产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| Image | 6,951,424 | `69f51295abde32c298c8c25809df8ff169a5151b7cb1f73c73535dbbde81989b` |
| boot.img | 7,397,376 | `3e7083a7ca5f8dc4bc9a3901305fe80add064f9b324bfe47b6cd3e92e383489f` |
| resolved config | 65,478 | `a938b008ea0dbc06d93374aea8b40c828953d28720b71ba3841dad344ac2367c` |
| DTB | 171,876 | `b4ea967d51e731b0b1628ae53e4e4e0d2be8ab5d77b561895115bdcaf5141616` |
| initramfs | 266,171 | `7cbd57b8b4183d4398f2e1d3033e8ee59ed6b4fe77bde1b78c9d9c3164f0391f` |
| qcom_fg.ko | 26,032 | `1fc312c76cb4dbddcef559460aab0ba0f38a22ed467757b299f843e0ee49f1c9` |
| qcom_pm8150b_charger.ko | 17,776 | `b8560387dd09534179ea8befc80de277a7ddde6372a2bda155510730f086cab0` |

两次完整构建的诊断镜像一致；同环境复建不等于独立构建闭包已闭合。静态解包 boot v2 验证 ramdisk 与本地 initramfs 一致，CPIO 仅有 `dev`、`proc`、`sys`、`dev/console`、`dev/null`、`init` 和 trailer；模块不在 Image，亦不在 ramdisk。完整产物和原始日志仅保留在忽略的 `.work/upstream-power-port-l2-20261108/` 与同前缀日志。

## 设备操作与未覆盖

来源评估阶段仅通过 ADB 只读确认设备为 `umi`、Android 16、普通 shell，未重启。后续维护者明确回复“授权本次诊断 RAM 启动及返回 Android”，已完成标准 `adb reboot bootloader`、指定镜像 RAM boot、只读日志检查、Linux → Fastboot → Android；准确命令、载荷和结果见[本轮真机记录](../hardware/2026-11-08-upstream-power-ramboot.md)。未使用 adb root、加载 FG/SMB5 模块、刷写或控制充电。

未覆盖：实际电池测量、充电启停/限流/限压、温控、过压/过流故障恢复、长时运行、其他设备、独立专项复核及远端 CI。本次模块 L2 与诊断镜像 L3 均不解除主动充电阻断。
