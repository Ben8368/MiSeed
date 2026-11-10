# 2026-11-10 已实现能力 Mainline 接口修正

## 范围与固定依据

对应 RQ-14、WP2/WP9/WP10、TD-004/006/007。基于 `codex/nfc-wireless-charging` / `a081ab302a3099eb1825ed76fe8cb011e1cec79c` 的本地修改；Windows X64 / PowerShell 7.6.9，L1/L2 在 WSL Linux x86_64 / bash 5.2.21，aarch64-linux-gnu-gcc 13.3.0 / GNU make 4.3。只修正已有能力；不开发无线 RX/TX、触控、GPU、原生 DSI、UEFI 或 Debian，不操作设备或实际供电，不提交、推送、发布或合并。

逐项读取固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 的 `Documentation/ABI/testing/sysfs-class-power`、`drivers/usb/typec/tcpm/tcpm.c`、`drivers/iio/adc/qcom-spmi-adc5.c` / `qcom-vadc-common.c`、`drivers/gpu/drm/sysfb/{Kconfig,simpledrm.c}`、`drivers/gpu/drm/clients/Kconfig` 和 `display/simple-framebuffer.yaml`；没有使用移动分支替代项目依据。

SMB5 电流换算依据为已锁定 LOS `71b13e62f057a649b77fe4062feb73ee72ad609c` 的 `drivers/power/supply/qcom/smb5-lib.c` 中 PM8150B buck 0.2 V/A 与 `smb5-reg.h` 的 OTG enable 状态。前者本地 SHA-256 `8f542b9679a95da4c5a1bdf1d622ea4e06ab31345b158c9a68bb7d7ff9edeb68` 与已有来源锁匹配；未猜测增益、电阻或阈值。仅据事实自行实现换算，未引入新二进制、固件或 Android 运行依赖。原有 TI、Qualcomm/The Linux Foundation 及策略作者和 GPL-2.0-only 归属保留。

## 接口矩阵与修改

| 已有能力 | Mainline 子系统 / ABI / binding | 标准消费者与本地处理 |
| --- | --- | --- |
| 电池 FG | power_supply BATTERY / current_now | sysfs、uevent；在对外属性层转成充电正、放电负，原始 ADC 解码和 STATUS 判断不变。读取错误原样返回，INT_MIN 拒绝溢出；SMB5 和 PPS 消费者同步修正 |
| 有线 SMB5 输入电流 | power_supply USB / current_now，ADC5 IIO | ADC5_USB_IN_I 是电压通道、processed 为 µV，旧代码直接当 µA。sink/buck 按固定器件依据乘 5；拔线返回 0，OTG/boost 返回 ENODATA，I/O 错误和负值/溢出不伪报读数；不改变保护阈值 |
| 有线 BQ25970/PPS | power_supply、TCPM、thermal；既有本地 YAML | 寄存器更新、停泵、保护读回与 ADC 抽到 bq25970.c/.h，Kbuild 组合链接；Umi 的阈值选择、热模型、策略和互锁留在板级控制器。内部不新增 EXPORT_SYMBOL 或 UAPI；这不是独立通用泵驱动已上游的声明 |
| USB 类型 | TCPM power_supply USB_TYPE | 泵代理 TCPM 的 C/PD/PD_PPS；补齐原先漏报的 Type-C 支持掩码，避免标准 sysfs 枚举与实际返回值不一致 |
| PMIC 原始观测 | SMB5 所有者内的可选诊断 | 删除独立 observer 驱动、MFD 自动构建注入和虚构 DT 节点。原只读采样移入 SMB5 worker/同一把锁，CONFIG_MISEED_POWER_DIAGNOSTICS 默认关闭；RAM 诊断 profile 显式启用，power_supply 产品接口不依赖它 |
| 继承 framebuffer 显示 | 上游 simpledrm / DRM KMS / fbdev client；既有 simple-framebuffer binding | 配置切换 DRM_SIMPLEDRM，禁用竞争的 FB_SIMPLE；保留大字横屏 fbcon。可由标准 DRM 消费者使用固定模式；仍依赖 ABL 初始化，未实现原生 DSI、面板刷新或 GPU |
| NFC、USB 诊断、温控与资源框架 | NFC/NCI + Netlink/AF_NFC、configfs/TTY、IIO/thermal、gpiod/regulator/clock | 已采用标准路径，保留；未更改其硬件接线、VEN、IRQ、功率/温控参数或安全停机顺序。Debian 集成与适用 L3 仍分别验收 |

构建器新增 simpledrm 配置/对象/链接检查、旧 FB_SIMPLE 和独立 observer 排除检查、BQ 两个组成对象/函数链接检查；新文件全部纳入来源锁。缓存验证器拒绝恢复旧 MFD observer 注入，旧未知缓存保留并由构建器选择全新目录。旧观测器历史证据链接固定到原提交，原有日志与镜像证据不重写。

## Audit 与验证

源码修改后、完整验证前已在对话输出 🚦 Audit Report。首次 L2 检出新函数参数 `current` 与内核宏冲突，已改为 `current_ua`；初次 L1 的构建 fixture 缺少新 DRM 必需项，已补齐。第二次内核已成功链接，但打包前错误要求存在内建组合 `umi-bq25970.o`；按该内核 Kbuild 实际行为改为核验组成对象及最终链接符号，并另实编模块组合路径。修正后完整 L2 与回归通过，前述失败不计通过。

下列命令均在 `C:\MiSeed` 或 WSL `/mnt/c/MiSeed` 运行，最终退出码均为 0。schema 使用 `PATH=/tmp/miseed-dtschema/bin:$PATH`。

| 验证 | 准确命令与结果 |
| --- | --- |
| Windows L0/L1 | `python -X utf8 tools/check_repo.py`、`git diff --check`；`python -X utf8 -m unittest discover -s tests -v`：172 项，99 项执行通过、73 项因 POSIX/C 编译器/DT 工具条件跳过 |
| WSL 全量 L1 | `MISEED_WIRED_DTB=/mnt/c/MiSeed/.work/mainline-interfaces-l2/umi-csot.dtb MISEED_DT_SCHEMA=/tmp/miseed-boot-l2/mainline-build-20261110/Documentation/devicetree/bindings/processed-schema.json python3 -m unittest discover -s tests -v`：172 项全部通过，无跳过 |
| ARM64 Boot L2 | 下方环境执行 `bash tools/build_kernel_l2.sh`：内核、DTB、modules、静态救援 initramfs 和 boot.img 全部完成；配置、组成对象、链接符号、旧 ABI/observer 排除与打包校验通过 |
| binding | `make -C <source> O=/tmp/miseed-mainline-schema-build ARCH=arm64 dt_binding_check DT_SCHEMA_FILES=power/supply/qcom,pm8150b-fg.yaml:power/supply/qcom,pm8150b-charger.yaml:power/supply/xiaomi,umi-bq25970.yaml:net/nfc/nxp,sn100t.yaml:display/simple-framebuffer.yaml`：目标 schema/示例无诊断 |
| 板级 DT | `make -C <source> O=<build> ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CHECK_DTBS=y qcom/umi-csot.dtb DT_SCHEMA_FILES=power/supply/qcom,pm8150b-fg.yaml:power/supply/qcom,pm8150b-charger.yaml:power/supply/xiaomi,umi-bq25970.yaml:power/supply/battery.yaml:thermal/thermal-zones.yaml:thermal/thermal-sensor.yaml:net/nfc/nxp,sn100t.yaml:display/simple-framebuffer.yaml`：所列范围无诊断；不声称整机 schema 合规 |
| 打包后 DTB | `python3 tools/check_wired_dt.py --dtb .work/mainline-interfaces-l2/umi-csot.dtb --schema <build>/Documentation/devicetree/bindings/processed-schema.json` 通过；另运行 `dt-validate -s <同一schema> -l net/nfc/nxp,sn100t.yaml:display/simple-framebuffer.yaml .work/mainline-interfaces-l2/umi-csot.dtb`，输出为空；不只依据 make/validator 返回码 |
| ARM64 无诊断/模块对象 | 下方命令完成真实内核头下的 SMB5 无诊断对象与 BQ 模块组合对象；原始观测日志字符串未编入 SMB5，模块内 BQ 函数已解析，没有未解析的 bq25970_* 依赖 |
| Shell | `shellcheck --severity=error tools/build_kernel_l2.sh`：通过 |
| 源码缓存 | `python3 tools/verify_kernel_source.py --repository /tmp/miseed-boot-l2/linux-stable.git --revision f45ed76d6791cf72ff1265ee93511175bbbc2f18 --source /tmp/miseed-boot-l2/mainline-source-20261110`：固定源码及允许的本地覆盖验证通过 |

Boot L2 环境（`<source>`、`<build>` 分别对应以下 EXTRACTED_SOURCE、BUILD_ROOT）：

```sh
MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2 \
MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/mainline-source-20261110 \
MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/mainline-build-20261110 \
MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/mainline-interfaces-l2 \
bash tools/build_kernel_l2.sh
```

诊断关闭与模块路径复验，实际临时目录为 `/tmp/miseed-mainline-product.1Jy723`：

```sh
source_tree=/tmp/miseed-boot-l2/mainline-source-20261110
product_build=$(mktemp -d /tmp/miseed-mainline-product.XXXXXX)
cp /tmp/miseed-boot-l2/mainline-build-20261110/.config "$product_build/.config"
"$source_tree/scripts/config" --file "$product_build/.config" \
  -d MISEED_POWER_DIAGNOSTICS -d MISEED_CHARGE_DIAGNOSTICS \
  -m CHARGER_MISEED_UMI_PPS
make -C "$source_tree" O="$product_build" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  -j12 olddefconfig prepare modules_prepare
make -C "$source_tree" O="$product_build" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  -j12 drivers/power/supply/qcom_pm8150b_charger.o drivers/power/supply/umi-bq25970.o
```

新合成回归覆盖 FG 的充电/放电/零值、ADC 失败与符号溢出；SMB5 电流单位换算、拔线、OTG、不可信值/读取失败；PPS 使用标准电流符号的原生产 worker、故障停机/回退/所有权保留；诊断启用时四个只读寄存器的每条错误路径与关闭后无寄存器依赖。未重写策略模型或改变阈值来适应测试。

最终 Boot L2 有一条既有 WSL 时间偏差警告：`.config` 的时间领先 2.4 秒。显式 syncconfig、对象/符号与最终产物检查通过，无最终 C 编译错误或 schema 诊断；不据此声称环境可复现性闭合。原始日志仅存忽略目录 `.work/mainline-{l2,linux-tests,windows-tests,binding,dtbs,product-objects,source-check}.log`，不提交长日志或产物。

本地产物仅留 `.work/mainline-interfaces-l2/`（Image 在 L2 build tree），未启动：

| 文件 | SHA-256 |
| --- | --- |
| boot.img | `704040f9c39dfe1a44c79cb6a26ee01bac0bb009b81de17b3364e3276bdba617` |
| umi-csot.dtb | `0b85be50c6eb2df3a83e9d688d799fefb46ccf967b7ead79f6efe8d3b7abd8dc` |
| Image | `c0ef2ece9fb6b696dc528e04cc21c0dd768c6a727a252116f499330692de8abc` |
| resolved.config | `08b70762778823dac5ffdbd0c72f7516c9d69c0dc0f18c4d4d7b93f4a3634030` |
| rescue-initramfs.cpio.gz | `74462a4d9fe12e40381293ae6a7ea1f361f2bdbc70bdebdb095dec3ee0c38223` |

## 仍需关闭

本地标准接口修正不等于上游合入、Debian 集成或真机可用。需在新镜像上核对电池电流方向、SMB5 ADC 量纲与读数、simpledrm/旋转控制台、PD/PPS/thermal/DT 的失败与恢复，以及 NFC 标准读卡和服务。保护注入、固定 PD 热模型、FG profile/校准和休眠按原风险继续；未改变已宣告稳定的旧镜像 PD/PPS 成果。

BQ 与 SMB5 的失败锁定所有权仍由内核内部互锁保障；没有用非原子的用户空间写属性替代。BQ 仅完成硬件访问层拆分，完整独立芯片驱动/板级策略组织及 binding 仍待子系统评审。`ignore_unused`、板级 GPIO 保留和 ABL/DTBO 交接没有在硬件未知时强行撤除；原生面板/资源管理、UEFI→Linux 和 Debian 的产品验收仍开放。开发者以外专项复核、精确 HEAD CI 与明确合并授权未在本轮完成。

## PR #4 提交授权

维护者随后要求将上述改动提交到 PR #4。本批在既有 `codex/nfc-wireless-charging` 分支追加提交，保留已推送历史；提交前重查来源锁、仓库检查、差异和提交署名。上述本地验证仍对应相同代码与构建输入；托管 CI 另以提交后的精确 HEAD 为准，不继承旧 HEAD 结果。该授权不包含合并、设备操作或镜像发布。
