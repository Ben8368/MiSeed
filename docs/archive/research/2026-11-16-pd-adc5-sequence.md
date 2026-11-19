# WP9：有线固定 PD ADC 配置与电压消费者修复

## 输入与准入

接续[11-16 L3](../hardware/2026-11-16-umi-fixed-pd-l3.md)的2.239615A/2A故障及维护者补报的<1W跳功率。维护者要求实际修复；Darwin arm64/zsh，工作分支 `codex/fixed-pd-sampling`，HEAD `e875b24`加本轮改动。设备已停充并回Android，后续离线开发不接机、不改真实供电。

| 功能 → 子系统 | 固定主线 / ABI / 用户态 | 本地差异 |
| --- | --- | --- |
| ADC转换 → IIO | Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` `qcom-spmi-adc5.c`；IIO processed属性，charger和thermal为标准消费者 | 沿用主线数据结构、锁、换算及DT binding；替换PMIC5配置顺序并核对通道/完成状态，ADC7保持原路径 |
| 有线带载电压 → power_supply/IIO | 同一MID/USBIN标准consumer；Debian标准电源消费者后续 | 固定PD worker复用已有 `smb5_get_input_voltage()`，带载用MID、暂停用USBIN；没有新增用户ABI |

范围是有线固定PD。ADC5由其他电源/温度consumer共享，本次不启用无线RX/TX，也不更改PPS策略。标准接口与本地修正不等于上游已合入，后续需向IIO维护者评审该配置序列/有效性检查；Debian集成未验。既有私有协调债TD-004及硬件不确定性RISK-004不扩大。

来源为已经锁定的LOS仓库 `71b13e62f057a649b77fe4062feb73ee72ad609c`，新增核对 `drivers/iio/adc/qcom-spmi-adc5.c`、`qcom-vadc-common.c`（哈希在来源锁），前者GPL-2.0-only、The Linux Foundation 2012–2020，后者同许可；只选择配置函数的逐寄存器写法，未导入Android框架或下游整驱动。原主线文件版权保留，派生片段归属记于 `kernel/power/NOTICE.md`。

同源历史 [ab8da1dd](https://github.com/LineageOS/android_kernel_xiaomi_sm8250/commit/ab8da1ddfe5ff07f79e6a18a4c5f41931558c3f2) 明确将配置API改为逐寄存器写入。主线锁定版本仍一次写6字节，连同转换请求一起发送；本机LOS依次写0x42–0x46，再单独写0x47。该差异客观存在，但原提交说明没有给出本次2.240A异常的因果证明。

## 合约能力与实际充电目标

维护者明确提出输入≤3A可接受。旧fast profile把9V能力限制为2A，导致2.239615A读数按2A合约停充；充电器已广播9V/3A。本次将firmware sink PDO改为9V/3A，SMB5允许已协商预算≤板级3A；低能力源仍按TCPM实际合约保护。3A是协议预算，实际输入仍保留18W/ICL≤2A及温控降流，不擅自将充电目标改为27W；没有把2A合约下的过流保护直接放宽为3A。

## 实现与边界

1. `adc5_configure.inc` 先完成配置，再单独请求转换；每个I/O错误立即返回，不重试、不重新采样。请求前回读通道，完成后核对EOC、故障位及通道；不合格返回错误，由既有charger互锁停充，不将错误当作正常数值。正常原始样本、缩放及按实际合约保护均保留。
2. `apply_adc5_fix.py` 锁定原始和输出SHA-256，拒绝未知源码/符号链接，构建和缓存验证器接线。新构建输入、测试及许可证纳入来源锁；来源锁检查不是传递依赖闭包。
3. 固定PD worker以前每秒读取高压USBIN，即便带载getter已改MID；该消费者遗漏现统一到物理电压选择函数，转换阶段保持输入暂停后的无负载核对。不把USBIN勘误直接推成电流异常的已证根因。
4. 故障回放保留本次1.363105A worker→2.239615A标准读取，在2A电源下仍必须两路停止并保持故障；3A合约下同值必须被接受，3A以上则停充。没有调整换算、滤波丢样或忽略错误。单写顺序回归使用无法锁存批量请求的外围模型，只证明实现顺序和失败路径，不冒充真实硬件错误模型。

## 验证记录

- 本轮ARM64 L2：既有 `miseed-boot-l2:local`、独立Colima卷，运行 `MISEED_WORK_ROOT=/build MISEED_EXTRACTED_SOURCE=/build/pd3a-adc5-source-f45 MISEED_BUILD_ROOT=/build/pd3a-adc5-build-f45 MISEED_OUTPUT_ROOT=/workspace/.work/pd3a-adc5-l2-20261116 MISEED_JOBS=4 bash tools/build_kernel_l2.sh`，退出0。固定GCC13.3.0/make4.3，6项ABL回归及完整内核/ADC对象/DTB/rescue/打包通过，无时钟告警。
- 同容器 `/build/pd-pps-dtschema/bin/python tools/validate_kernel_dt.py --source /build/pd3a-adc5-source-f45 --build /build/pd3a-adc5-build-f45 --dtb /workspace/.work/pd3a-adc5-l2-20261116/umi-csot.dtb`，退出0；定向binding/board/最终DTB和4项真实反例全部通过、零跳过。9V/3A被接受，9V/3.1A被拒绝。
- PATH前置同一venv，MISEED_WIRED_DTB为上述最终DTB、MISEED_DT_SCHEMA为该build的processed-schema.json，`python -X utf8 -m unittest discover -s tests -v`：**280项全部通过、零跳过**，退出0。包含ADC逐I/O故障/通道/完成状态反例、批量请求突变失败、3A/2A合约差异及本次故障回放。
- macOS定向ADC/固定PD/PPS合计9项通过；boot/cache 21项中3项环境跳过，不当作通过，Linux全量已覆盖。ShellCheck error级、仓库检查和git diff --check通过；构建脚本301行黄灯已登记TD-008。

| 本轮产物 / 输入 | SHA-256 |
| --- | --- |
| boot.img（9,728,000字节） | `00594b96d74ee38195d7b4e5546fd2782698fc9c0488506d641284d751fde26b` |
| DTB | `0020226ac3b50875cbe876ca1ce229429d4b38798d30aeb85612a1956c61cfba` |
| resolved.config | `3695601c86a22d9ddd44d359d65c9ce0d67e880dd81047e12c9c14e03f22c940` |
| 来源锁 / 构建副本 | `fd5c960bd918a18d374be8d04bc3e00622278f2c7763693c65dd6c79124b8623` |

镜像在 `.work/pd3a-adc5-l2-20261116/`，构建/DT/回归/来源复核日志在 `.work/fixed-pd-repair-20261116/`，新接机包在 `.work/pd3a-adc5-l3-20261116/`，启动仍须主机USB权限。源码和镜像修改并不证明目视跳功率已经消失；新镜像L3、开发者以外专项复核及精确HEAD CI尚待执行。不提交、推送、合并。
