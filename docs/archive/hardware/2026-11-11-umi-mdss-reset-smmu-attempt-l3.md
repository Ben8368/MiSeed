# 2026-11-11 HEAD 2bdb266 MDSS reset / SMMU 真机尝试（停止）

本记录归档历史载荷的显示测试；2026-11-13核对现存材料并整理，未重新操作设备，不作为新充电策略`661ea31`的L3证据。

## 环境、授权与载荷

| 项 | 本次事实 |
| --- | --- |
| 日期 / 主机 | 2026-11-11；Windows 10.0.26200 x64、PowerShell、Python 3.14、pyserial 3.5、Platform Tools 37.0.1 |
| 授权 | 用户要求按最新源码完成真机测试，并确认设备、既有绿联 45W 有线 PD/PPS 组合均就绪。本轮先只接电脑 USB 数据线；未接外部充电器 |
| 设备 | 唯一 Fastboot 目标 `product=umi`、`unlocked=yes`、`is-userspace=no`；沿用此前指定的 CSOT 变体，未读取面板 IC ID，不保存设备序列号 |
| 仓库 / 固定输入 | `codex/native-dsi-dt-power`，HEAD `2bdb2666e2557aa1d1309845bca57938967621b0`；Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`，GCC 13.3.0；产物 build-source-lock 与仓库 manifest SHA-256 一致 |
| RAM 镜像 | `.work/native-dsi-l3-prep-20261111/boot.img`，SHA-256 `955649a4926229cc1e492a6a9b59523935d19ce126eae1dee0ad5a018f742d77`；DTB SHA-256 `ad85268b167c187832e269aa3877836aa609fb552023c5574feac6e7ba3daead` |
| 镜像类型 | 原生显示配置：`native-dsi-profile=1`，`CONFIG_DRM_MSM=y`、`CONFIG_DRM_MSM_DSI=y`、`CONFIG_DRM_PANEL_XIAOMI_UMI_CSOT=y`；同时保留`CONFIG_DRM_SIMPLEDRM=y`，不能因此称为仅simpledrm镜像 |
| 配置 / 离线证据 | resolved.config SHA-256 `dc7ced5f2222b690a3c314c12734f1ca9be9af512f9bf7174d8e427baeda9c63`；L2 构建、237 项回归（0 跳过）、DT schema 与仓库检查通过 |

## 实际操作与发现

1. 只读检查唯一 Fastboot 设备与 `product/unlocked/is-userspace` 均通过。执行 `fastboot boot .work/native-dsi-l3-prep-20261111/boot.img`，Sending/Booting 均 OKAY，退出 0；未执行 flash、erase、分区写入或格式化。
2. 唯一 MiSeed USB CDC 端口 COM6。只读显示/kmsg/FDT 采集器完整结束，退出 1，原始响应均保留。运行树 2413 节点；严格 FDT 检查仅报告 root `compatible`、`model` 被 ABL 覆写，未报告 live-hardware 或 RAM 错误；整体仍为 FAIL，不写成标准根交接通过。
3. 快照显示 DSI-1 为 connected/enabled，报告 1080×2340@60，CSOT panel driver 与 backlight 节点绑定；brightness/actual_brightness 均为 1，max=2047，bl_power=0。没有取得用户目视反馈，不能据 DRM 状态宣称面板实际点亮。
4. 最早的完整 kmsg 出现 `DSI PLL(0) lock failed, status=0x00000000`、`PLL(0) lock failed`，随后 `dsi0_phy_pll_out_dsiclk already disabled` 与 `clk_core_disable` WARNING，栈含 `mdss_probe`。另见 `arm-smmu 3da0000.iommu` deferred probe timeout / error -110。没有发现旧 framebuffer SID 0x820/0xc20 context fault、DSI DMA timeout 或 FIFO 下溢；但 MDSS/PLL 初始化失败使本轮不能关闭 reset/SMMU 交接问题。未执行 `display-on`。
5. 保全材料后经诊断台 `bootloader` 返回 Fastboot，再执行 `fastboot reboot`；ADB 只读确认 `ro.product.device=umi`。回退完成，未执行刷写、擦除或主动块设备写入。

## 有线 watchdog 与证据边界

本次显示载荷未接绿联充电器，未发起有线PD/PPS或watchdog测试；因显示初始化错误按停止条件结束，不能据此判定有线保护是否通过。后续独立载荷已完成watchdog正常刷新及PPS热保护/固定PDO回退L3，见[有线实机记录](2026-11-11-umi-wired-pd-pps-recovery-l3.md#2026-11-11-local-head-bq25970-watchdog-and-pps-thermal-l3)；到期停泵、到期后恢复和其他硬件保护注入仍未覆盖。无线RX/TX未测试。

原始采集仅保留于忽略目录 `.work/native-dsi-l3-device-20261111/read-only/`：

| 文件 | SHA-256 |
| --- | --- |
| `display-before.txt` | `0eb6f21a0a5de9021db6c1812b19941fc6bf216800aec6c3a2137d0422a96d27` |
| `dmesg.txt` | `0824e08f0c6ed77cebae37c389e158559a2a64f9d15d87ea364f60af82949a45` |
| `runtime-fdt.dtb` | `8fafa7bac2a4fb1e3d1acbdeff95e2cbb4e995be067d62442fa119520257bbf1` |
| `runtime-fdt.txt` | `d726b2552686b9e8cb5951f501c9e60465c4f0f21cdc35842508ddcd69771593` |

🚦 历史HEAD `2bdb266`的本载荷确认RAM启动、USB只读采集及Android回退L3；面板实际点亮未确认，MDSS PLL初始化失败，SMMU probe超时，严格根交接仍FAIL。旧镜像既有点屏L3保留；本次不关闭MDSS reset/SMMU验收，也不提供新充电策略L3。

## 归档核对（2026-11-13）

本地镜像、DTB、resolved.config与四份采集文件共7个SHA-256均与上述记录一致；原始材料继续留在忽略目录。研究文档原先标为“simpledrm镜像”，按profile及实际内核配置纠正为原生显示配置。完整硬件事实集中于本记录，研究文档保留摘要和链接；当前阶段及开放项分别由CONTEXT、RISK-010维护。
