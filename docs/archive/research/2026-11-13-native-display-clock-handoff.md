# 2026-11-13 原生显示 CCF 交接审查（WP2）

目标：Umi CSOT 原生 DRM/KMS L3；本轮用户授权最新源码 RAM 启动、调试和重复验收，失败仅回 Fastboot。

## 接口与固定依据

功能 → Linux Common Clock Framework 父节点解析/引用计数 → 既有 clk 内核 API，无新 UAPI/binding → DRM/KMS、fbcon/后续 Debian；不依赖 Android 服务。

Linux 固定 `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，`drivers/clk/clk.c` SHA-256 `a30805dc12b79b20f41dfaf20ce18aa04aa6afb332317b7c1310df3d2b2cd235`。保留该文件 GPL-2.0 与 Canonical、Linaro 原始版权头。

本机原始材料：`.work/native-display-l3-20261113/attempt-1/`，基线 `b10d522`；镜像 `c6f7d24ad0a4d3aa75607308ba55b9ad4ed66cd571ce3ecf6cd204251dcbbec6`。2.226160s PLL lock failed，随后两次 clk WARNING；栈均经 `clk_core_reparent_orphans_nolock` → `devm_of_clk_add_hw_provider` → `dsi_phy_driver_probe`。3.434732s 面板59条命令成功，不能抵消早期错误。

## 候选与实施边界

- 保留 MDSS reset：旧扫描必须在 DPU translated domain 接管前终止；删除 reset 会重新引入已记录 SID fault，不能作为修复。
- 不删除 PCLK 的 `CLK_OPS_PARENT_ENABLE`：实际硬件父源切换仍需此约束。
- 不提前猜测 PLL 电气/时序参数，不绕过 SMMU，也不改有线/无线供电策略。
- 选用 CCF 通用修正：尚未 prepare 的 orphan 只在 enable lock 下补软件父关系；硬件父源已由 `__clk_init_parent` 读取，并未执行 set_parent。不调用会临时 prepare/enable 父源的 helper。已 prepare 的 orphan 保留原引用迁移路径；rate/accuracy/req_rate 重算保持不变。
- 固定代码中 `CLK_OPS_PARENT_ENABLE` 使 before helper 无条件尝试启用父时钟；它忽略 prepare 失败，after 再清理而产生 WARNING。本次只修无需硬件操作的未准备 orphan 分支，未声称解决所有 CCF 失败传播。

补丁由 `tools/apply_clock_handoff.py` 锁定原文件哈希与唯一锚点，仅原生 profile staging 应用；缓存核验接受原始或精确补丁。退出/评审：WP2 向 CCF 维护者提交通用 orphan 解析修复，覆盖未准备、已准备/critical、父节点缺失和失败路径；本地 L1/L2/L3 不等于上游合入。电源、设备树供电与 DSI 命令表不改。

## 验证计划

合成 C 测试证明：未 prepare 的 orphan 不启动父 PLL；准备过的时钟继续迁移计数；精确 staging 幂等，未知修改/重复锚点/符号链接拒绝。固定源码 ARM64 L2、定向 schema、完整离线回归通过后，使用新哈希镜像 RAM 启动；先查 PLL/WARNING/SMMU/DMA/FIFO，再做标准 backlight 熄亮与目视出字。失败保全现场并回 Fastboot。最终实测另归硬件验收记录。

## 已撤回试验：取消原生配置的重复控制台接管

用户确认最终文字清晰，但两轮仍见约0.5秒乱码或先竖后横瞬态。第三轮日志显示simpledrm在1.826484s接管、2.367668s退回dummy、原生在2.622958s接管；其约0.54秒首阶段与描述相符，仅为候选因果。固定fbcon.c在startup/init均先设initial_rotation再配置blitting，命令行已rotate:3，暂不修改旋转算法。

功能→DRM/KMS/fbcon首个消费者→标准DT status/simple-framebuffer→fbcon与后续Debian KMS。本轮只在原生dtsi将继承framebuffer标为disabled，保留完整预留显存与no-map；原生DSI/MDSS reset和默认simpledrm配置不变。避免在ABL画面上先写一轮日志、再拆除并重绘；并非删除SMMU/reset或增加寄存器写入。代价为原生probe前没有屏幕内核日志，但1MiB时间戳日志与USB采集保留。

最终DTB门禁核对disabled和原splash预留范围/no-map，正反例覆盖重新启用、缺失状态、预留范围漂移及no-map丢失。重建后先证明内核/配置/initramfs与CCF修复轮一致，再查运行时没有simpledrm绑定、只有一次原生fbcon切换；PLL/SID/DMA/FIFO与背光继续检查。目视复验前不宣布花屏已修。若仍乱码，再沿首帧内容和面板点亮时序定位。


本次试验结果：用户未见旋转瞬态，但仍彩色花屏且随后黑屏，因此上述试验已撤回，不能判定simpledrm重复接管为花屏根因。CCF修复保留。固定源码 `sys_fillrect`/`sys_imageblit` 在缺失FBINFO_VIRTFB时仅警告，仍调用绘制函数；尚无证据可把该警告等同于黑屏原因。


续查边界：当前panel probe使用GPIOD_ASIS，直到prepare才执行已锁定的复位序列；59条DCS中display-on在prepare内。两者都是后续时序候选，当前未改，也没有证据可宣布根因。固定MSM atomic tail在modeset enables之后才flush_commit，因此仅把display-on搬到panel enable也不能证明“第一帧已完成”；禁止以这种未经证实的重排替代首帧证据。撤回试验后的boot/DTB/config/initramfs已与清晰出字轮逐字节匹配，先完成恢复出图的对照反馈。


最终恢复版反馈为手机顶部（横屏右侧）约一行日志高度的白色花屏，类似先前“旋转日志”；维护者要求本轮L3收尾并推送PR6，后续再分析。保留CCF修复与此待查线索，未继续修改复位或DCS时序。精确载荷及最终Fastboot回退见硬件验收记录。
