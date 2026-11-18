# Umi CSOT 90Hz 移植与 Linux 接口审查

## 范围与来源

RQ-14 / WP2；宿主 Darwin 25.6.0 arm64、zsh；基线 `f464b38029f47965919ab5ec7d55d718069d7682`，工作分支 `codex/display-90hz`。本轮审查 MiSeed 已有接口与 colorgarden 显示实现，移植固定 CSOT 90Hz；不以来源报告替代本项目 L3。

读取引用对话后，复核固定来源 `colorgarden/umi-mainline@531c16b9e50f0351ad0b95051c13a705ad8e223b`：`dts/xiaomi-umi-panels.dtsi`（BSD-3-Clause，SHA-256 `b9eba6584dfdad6b9ebc6c7149c01efed8529ad10cc1003ea3fb13e2ba200bc1`）给出 H 16/8/8、V 4/4/8、90Hz、GPIO50 高电平选择；`patches/panel-xiaomi-umi.c`（GPL-2.0-only，SHA-256 `8589418e099ee114902b25eeeb52be2e7ca550ea614dd9e89b5c8d3ad29632a3`）包含厂商约128ms等待说明，而参考实现实际使用20ms。`dts/sm8250-xiaomi-umi-common.dtsi`（BSD-3-Clause，SHA-256 `d4f51be4d33db8c82fe5fdf37a103bad4e23acf566b3cb2487305434a3219afa`）未保留GPIO50。原贡献者为 colorgarden/umi-mainline contributors，驱动作者 color；仅选择时序、接线和等待值，不复制通用驱动。

来源提交 `027e114627e750071abbf898f1eccd79fc4fbf1d` 报告1080×2340@90实测；未提供对应本机的KMS事件或光学测量。参考实现还有任意DSI写入/寄存器读回的`dbg/kick/repower/dsi`私有sysfs、任意DT命令序列及GPIO获取错误被忽略；不准入这些部分。源码存在、来源实测、本项目L2、本项目L3和上游合入分别记录。

## 移植前接口对照

锁定 Linux 6.18.60 / `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，直接核对其 `Documentation/gpu/drm-{uapi,kms-helpers}.rst`、`include/uapi/drm/drm_mode.h`、`Documentation/ABI/stable/sysfs-class-backlight`、`Documentation/driver-api/gpio/consumer.rst`、`Documentation/devicetree/bindings/display/panel/panel-common.yaml`、`drivers/gpu/drm/msm/dsi/dsi_host.c`。下表在实施前建立。

| 功能 | mainline模型 / ABI或binding | 标准消费者 | 本地差异与处置 |
| --- | --- | --- | --- |
| 1080×2340@90 | drm_panel / MIPI DSI / DRM KMS mode、vblank事件 | libdrm modetest、Wayland compositor | 固定模式1112×2356、235788kHz；不新增刷新率sysfs或私有ioctl |
| DSC / DSI链路 | drm_dsc_config、mipi_dsi_*、MSM DSI | 由KMS驱动链自动配置 | 保持既有DSC及682MHz上限；主线按DSC比例与mode计算实际时钟，不能据682MHz常量声称链路时钟不变 |
| 背光 | backlight / DCS、标准brightness及bl_power | 标准backlight消费者 | 保持0–2047及已有生命周期；实际亮度、PM仍需专项验收 |
| 刷新选择脚 | gpiod、panel专用YAML | 内核panel生命周期；用户态只消费KMS | panel-common无此信号；新增必需disprate-gpios，描述高=90Hz，不冒用enable/reset/TE语义 |
| 显示交接 | MSM MDSS/IOMMU/CCF | KMS/fbcon | 既有ABL供电继承、CCF补丁与根身份quirk继续归TD-006，不因90Hz关闭 |
| NFC / 按键 | NFC/NCI、generic netlink/AF_NFC；input/evdev | 标准NFC消费者、evtest | 已有标准实现；NDEF/PM/eSE与按键L3未闭，不据此宣称产品完成 |
| 有线电池/充电/PD/PPS | power_supply、Type-C/TCPM、thermal/IIO | power_supply消费者、Type-C/thermal接口 | 仍有charge_test私有诊断、内核互锁和TCPM本地扩展；TD-004/005/007继续开放 |
| 无线RX / 无线TX | 应独立接power_supply及对应硬件驱动 | 尚无本地消费者验证 | 均未移植；有线结果及wireless_therm不能替代 |
| 启动/救援/Debian | 产品为UEFI→EFI Stub→Linux→Debian | 标准发行版消费者 | ABL/Fastboot、rescue命令和TCPM debugfs为TD-006诊断；UEFI/Debian尚未完成 |

复用评估：锁定主线没有本变体驱动；panel-simple不能表达其DSC/DCS初始化；truly,nt35597的mode-gpios选择单/双DSI，sharp,ls037v7dw01选择接口模式，均不覆盖本信号。沿用现有固定DRM panel，并为已知硬件信号扩展专用binding，不引入厂商框架。新binding是本地扩展，不能称已上游标准化；按固定源码 `Documentation/devicetree/bindings/{writing-schema,submitting-patches}.rst` 与 `Documentation/process/submitting-patches.rst`，先独立binding补丁，再驱动/DTS，运行schema，使用get_maintainer.pl送DRM/panel、GPIO/DT维护者审查；未完成归TD-007。

## 实施边界与待验证

仅原生DSI opt-in profile改为单一90Hz模式，默认simpledrm不变。不暴露未经验证的60/90动态切换。只额外开放GPIO50；probe不改电平，prepare初始化后等待厂商说明的约128ms，再设置高电平并传播方向设置错误，失败重新断言reset，允许后续重试。GPIO缺失、占用和EPROBE_DEFER均失败返回，不依赖ABL残留。独立供电不在本次改动范围。

验证计划：实际C生命周期故障注入/恢复及KMS模式发布；板级GPIO allowlist及缺失/错误资源反例；完整离线回归；固定ARM64内核/DTB/initramfs/boot L2；定向dt_binding_check/CHECK_DTBS和最终打包DTB验证。L3需另行明确授权、材料备齐后单次RAM启动；以KMS 90Hz模式、连续vblank/page-flip时间戳及目视确认分别取证，不能只凭点亮或DRM名称认定物理90Hz。当前不接入或操作手机，不提交/推送。

## 验证结果

先输出🚦Audit，再运行完整验证；首轮发现技术债文档超预算6字符及旧回归依赖诊断文字，已修复并重跑。以下均针对本轮最终源码，源码修改后未再改变构建输入。

| 检查 | 命令 / 结果 |
| --- | --- |
| 仓库 / 空白 | `python3 -X utf8 tools/check_repo.py`、`git diff --check`，退出0；仅TD-008既有篇幅黄灯，本轮修改源码最长216行 |
| macOS L1 | `python3 -X utf8 -m unittest discover -s tests -v`，281项，20.554s，退出0，44项平台/DT环境跳过 |
| Linux L1 | 相同命令，Linux ARM64 Ubuntu24.04 / Python3.12，281项，11.991s，退出0、零跳过；设置`MISEED_WIRED_DTB`为本次最终DTB，`MISEED_DT_SCHEMA`为本次processed-schema.json，PATH使用固定schema venv |
| ARM64 L2 | 现有`miseed-boot-l2:local`容器内执行下列命令，退出0；GCC13.3.0，完整Image/DTB/静态rescue/boot.img生成，无编译或时钟告警 |
| DT/schema | `/build/pd-pps-dtschema/bin/python tools/validate_kernel_dt.py --source /build/pd3a-adc5-source-f45 --build /build/pd3a-adc5-build-f45 --dtb /workspace/.work/display90-l2-native-20261116/umi-csot.dtb`，退出0；定向dt_binding_check、CHECK_DTBS、最终DTB及4项DT回归通过、零跳过 |
| 产物一致性 | `python3 .work/display90-reference/verify_artifacts.py`，退出0；源码/本地输入/来源锁一致，原生GPIO及配置正确，打包Image/DTB/initramfs与输入逐字节一致；新schema拒绝缺失disprate-gpios和任意xiaomi,on-seq属性 |

```sh
MISEED_WORK_ROOT=/build MISEED_EXTRACTED_SOURCE=/build/pd3a-adc5-source-f45 \
MISEED_BUILD_ROOT=/build/pd3a-adc5-build-f45 \
MISEED_OUTPUT_ROOT=/workspace/.work/display90-l2-native-20261116 \
MISEED_NATIVE_DSI=1 MISEED_JOBS=4 bash tools/build_kernel_l2.sh
```

上述容器由 `docker --host unix:///Users/ben/.colima/miseed-boot-l2/docker.sock run --rm --mount type=bind,source=/Users/ben/Project/Code/MiSeed,target=/workspace --mount type=volume,source=miseed-boot-l2-miseed-boot-l2-arm64,target=/build --workdir /workspace miseed-boot-l2:local bash -c '<对应命令>'` 运行。既有源码缓存先经构建器对固定Git tree核验，再重新stage；新输出目录独立，不覆盖既有硬件镜像。

实际panel函数经合成transport验证59个DCS失败点、短写、压缩/PPS失败、reset方向失败、刷新选择方向错误（EIO/EBUSY/EPROBE_DEFER）、失败后的reset及重试；90Hz选择只允许在全部命令及128ms等待后执行。实际get_modes函数验证分配失败、单一preferred模式及约90.000Hz；这些是L1模拟，不是DRM设备或GPIO实测。编译DT反例覆盖缺失GPIO50、错误引脚/控制器/极性及错误面板变体；GPIO申请错误传播另经源码审查，不把方向错误注入当作probe真机证据。

| 产物 | SHA-256 |
| --- | --- |
| Image | `681ac6446616cc607a4cc74e837696f511e3431d708cb82cbf5b6d1bc3af1492` |
| umi-csot.dtb | `8be099adb994ba2f7e44ebfa3c72568896a36b55f72474fe9789bf80647a0573` |
| rescue-initramfs.cpio.gz | `a075731e2dceb8b58458f421127a7b8dab0cb6aa23f68162cab83d17f0186df5` |
| resolved.config | `5f05b01bc4dde7d9d66b91cd7d8345234cd7dade26dbfd9bfd56f5f3e054f211` |
| boot.img | `9e4c09ef815822528d7f8358a7bb132daa83d786c1b32650560adfcf97b6b9c3` |
| build-source-lock.json | `d1301c00ac5fab4ed5a45e713210a0692c0a8f7ccf03114dedf2b1cb02ac195e` |

日志及核验脚本在忽略目录`.work/display90-reference/`；L2日志SHA-256 `0bec6fc90a4be9ce603314aed15f75801869552d40adeddcc360c07d6bddc3d9`，schema日志 `bac4f9758999f9e6638649cc5f5603cc68b7a9639dfa9d2494b60cff89d62149`，Linux全量日志 `0db27a2b6082f2593ec686d9551d843be719eb8f1c1fb48b3af3dd8ba6fbca78`。未将原始日志或产物加入Git。

结论：🟢本次90Hz实现、来源、L0/L1/L2及定向schema；🟡本地binding尚待上游评审，已有启动条纹、独立供电/PM与根身份交接例外继续开放；🔴本次90Hz L3、开发者以外专项复核及同HEAD远端CI未完成，仍阻断把90Hz标为本机已验证或合并验收通过。无线RX/TX、Debian、触控等未实现能力不因本轮构建成功改变状态。未操作手机、未提交/推送。

## PR交付

上述离线验证结束后，维护者授权提交到[PR #7](https://github.com/Ben8368/MiSeed/pull/7)。改动转入其现有分支`codex/fixed-pd-sampling`，保留原有有线PD修复历史，新增独立90Hz提交；不合并、不操作手机。PR描述分别列出最终281项回归、原生显示L2/schema与各自L3缺口，远端CI以推送后的精确HEAD为准。
