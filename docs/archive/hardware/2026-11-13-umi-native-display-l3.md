# 2026-11-13 原生显示 L3：CCF 交接修复与重复启动

## 收尾结论

维护者于2026-11-13要求本轮L3收尾、推送PR6，剩余显示缺陷后续分析修复。原生初始化、文字显示、标准背光控制及Fastboot回退已有L3证据；CCF修复消除了失败轮的PLL lock failed与时钟WARNING。最终恢复版仍在手机顶部（横屏右侧）出现约一行日志高度的白色花屏，表现接近此前“旋转日志”，与第四轮彩色花屏后持续黑屏不同。启动无花屏未通过，不宣称整个显示功能无缺陷。

禁用simpledrm试验已完整撤回，保留CCF修复；最终载荷与已确认文字清晰的镜像逐字节相同。面板独立供电/PM、严格根属性交接与CCF上游评审仍开放。

## 授权、环境与范围

用户要求先备齐材料，再用最新源码镜像完成原生显示 L3。本会话只回退 Fastboot，不启动 Android；已明确授权重复 RAM 启动、调试与验收。Windows 10.0.26200 x64 / PowerShell 7.6.9；Ubuntu 24.04 WSL2 x86_64 / GCC 13.3.0；Platform Tools 37.0.1。唯一 Fastboot `product=umi`、`unlocked=yes`、`is-userspace=no`，沿用指定 CSOT 设备；不保存序列号，未读取面板 IC ID。

只使用电脑 USB 数据连接；无 flash/erase/分区写入，无 Android 回退。未改变供电策略，不做外部有线 PD/PPS 或无线 RX/TX 验收。面板 rails 仍继承 ABL，不宣称独立上电/PM。

## 精确输入与 A/B

基线 `b10d522a98b61dcdff4970d333731670f9d8e093`，分支 `codex/linux-standard-interfaces`。修复轮为该 HEAD 加本轮未提交 CCF 补丁，不能称为纯 HEAD 镜像；来源锁和本地输入哈希保存在各输出目录。固定 Linux `f45ed76d6791cf72ff1265ee93511175bbbc2f18`。

| 输入 | SHA-256 |
| --- | --- |
| 失败轮 boot.img | `c6f7d24ad0a4d3aa75607308ba55b9ad4ed66cd571ce3ecf6cd204251dcbbec6` |
| 修复轮 boot.img（两次同一镜像） | `75adfc24493264f4a52624a7f849a02bcc03cf674f1b4b0de905e0bd41249e24` |
| 两轮共同 DTB | `5329ec0c6a92abf710ed06173a0792c05ef075adf6a7ced019b79c1cb69aa1b3` |
| 两轮共同 resolved.config | `bfb74ba74df26b0beea259a7adec10e38fb6c7e265c9f4b046af76ac0405e999` |
| 修复来源锁 | `14010bd1cf7e44bd83c41e92b94468ef0b9b23c8687842976f3ae41de19f9b98` |
| 修复后 drivers/clk/clk.c | `649640d7ae7e1aa084035bcb3b077d4fa97fd9d3262ace3e9b60e893dffa4532` |

原生 profile=1，MSM/DPU/DSI/7nm PHY/CSOT 全内建，同时保留早期 simpledrm。DTB、resolved.config、initramfs、rescue-init 均逐字节一致；只改变内核 CCF orphan 交接。修复原因/接口/候选审查见[研究](../research/2026-11-13-native-display-clock-handoff.md)。

## 实测

| 项 | 失败轮 | 修复后两次 |
| --- | --- | --- |
| Fastboot RAM 启动 / USB | OKAY / COM6 | 均 OKAY / COM6 |
| PLL / CCF | 2.226160s PLL lock failed；disable/unprepare 两处 WARNING | 两次完整启动快照均无该错误、WARNING |
| 面板初始化 | 3.434732s DSC/59命令成功 | 2.614750s、2.606475s DSC/59命令成功 |
| 原生 fbcon | 3.522228s msmdrmfb | 2.690118s、2.678468s msmdrmfb |
| 模式/缓冲 | 原生驱动建立 | DSI-1 connected/enabled，1080×2340@60，235655kHz，XR24/pitch4352 |
| 显示 IOMMU / DSI 错误 | 未见旧SID fault | 两次未见 context fault / DMA timeout / FIFO诊断 |
| 亮度控制 | 按停止条件未操作 | 两次1500读回成功、DCS transfer_rc=0；第二轮另测1500→0→1500均成功 |
| 回退 | 保存日志后 Fastboot | 第二轮已回 Fastboot，再启动同一镜像；第三轮点亮后用户确认文字清晰；后续调试已回Fastboot |

`3da0000.iommu` 的 deferred probe timeout 仍存在；runtime DT 明确其为 Adreno SMMU，GPU consumer 禁用。MDSS 的 iommus 实际指向 `15000000.iommu`，不能将二者混为显示 SMMU。GPU 依赖/功能本轮不验。

严格 FDT 审计仍 FAIL：仅 root compatible/model 被 ABL 覆写，两次无 live-hardware/RAM 差异；既有 TD-006 不豁免。`sys_fillrect/sys_imageblit` 的虚拟地址标志警告及 refgen dummy regulator 仍存在，未冒充本轮全部内核告警清零。

## 验证与结论

修复后 ARM64 Boot L2、定向 binding/最终DTB schema与4项DT反例通过；WSL 258项回归全过、无跳过。Windows相关41项中24执行通过、17因平台能力跳过，C测试已在WSL执行。仓库检查、diff空白检查通过；既有源码黄灯沿用 TD-008。无提交/推送/远端CI/合并。

🟢 原生显示驱动绑定、初始化、模式、标准背光控制、两次启动无上述PLL/CCF错误及Fastboot回退已有本轮L3证据。用户随后确认文字清晰。🔴 启动仍花屏：第一次像竖屏第一行日志随后转横屏；第二次约0.5秒乱码，因此无雪花验收未通过。熄亮视觉未单独确认；面板独立供电/PM、GPU和Debian消费者未验。🔴 整体根属性交接仍FAIL，未宣称关闭。

## 材料

原始日志与产物仅留忽略目录 `.work/native-display-l3-20261113/`；失败轮 `attempt-1`，修复轮 `clock-fix/attempt-2*`、`attempt-3*`，熄亮 `clock-fix/{off,on,dmesg-after-cycle}.txt`。各采集目录 evidence.json 保存全部原始文件哈希；verified-materials.json、patch-input-hashes.json、ab-comparison.json、构建/schema/回归日志同目录。

| 证据 | SHA-256 |
| --- | --- |
| 失败轮dmesg | `4ad40e2afa68593aaf0a518dd49a9410b2fbac44621601a9a04a40e700bd29b2` |
| 修复第二轮点亮后dmesg | `c9604f7446abf86d4820b84c6b54b27602699404e83ec42ded239f04b9f8e393` |
| 修复第三轮点亮后dmesg | `876e0bf12021fa88812dcb237c086beddc621d1106df695b227fe08aea43e094` |

## 用户目视反馈与续查

2026-11-13 用户确认最终文字清晰，但两次启动仍有瞬态：一次像先竖屏显示首行日志再横屏，另一次约0.5秒乱码。该反馈证明 CCF 修复解决的PLL/计数错误并非全部视觉问题；不得据无fault日志关闭花屏。下一轮核查 ABL/simpledrm→MSM fbcon 绘制/旋转及首帧点亮顺序，继续仅 RAM 启动与 Fastboot 回退。

## 首帧单变量对照（同日续验）

收到上述目视失败反馈后按约定回Fastboot，再准备并启动新镜像。运行行为仅改原生DT的`/chosen/framebuffer@9c000000/status=disabled`，保持splash预留/no-map、MDSS reset、CCF修复与面板初始化。原生probe前屏幕不输出Linux日志，完整时间戳kmsg与USB仍可采集；默认simpledrm配置未改。

| 新输入 | SHA-256 |
| --- | --- |
| boot.img | `4ec24674b0e4eacaad96cb0fb6ae1eb1471a3e33acb7c7f59c9e3f690732ece7` |
| DTB | `eca9daa8b9fb2037a218783c83103478527381647d19419c49327f45c13bf406` |
| build-source-lock.json | `9c4af563a089dccfb5ba0f734b438bc1aada620bd4f31e09995c4af767457c8c` |

与上一CCF修复镜像逐字节核对：kernel payload、resolved.config、initramfs、rescue-init全部相同；解析DTB后唯一属性差异为上述status。比对材料在`.work/native-display-l3-20261113/first-frame/{ab-comparison,dtb-diff}.json`。

ARM64 Boot L2、定向schema/最终DTB与4项DT反例、WSL259项回归（无跳过）均通过。新增正反例确认disabled、预留范围一致、no-map，并拒绝缺失status、okay、丢失no-map及预留范围漂移。Windows相关24项执行10项通过、14跳过；C/libfdt覆盖已在WSL执行。仓库与diff检查通过。

唯一Fastboot设备身份核对后RAM启动OKAY，COM6完整采集：运行树仍有framebuffer节点但disabled；内核无simpledrm初始化/退出，只有2.506151s一次`Console: switching ...146x33`。2.457587s CSOT DSC/59命令完成，2.577283s原生msmdrmfb建立；DSI connected/enabled、1080×2340@60、XR24/pitch4352。7.541674s亮度1500 transfer_rc=0，标准读回1500。完整启动快照未见PLL lock failed、WARNING、显示context fault或DSI DMA/FIFO诊断。

严格FDT审计仍仅root compatible/model差异FAIL。初始/点亮采集在`first-frame/attempt-4*`，原始哈希见evidence.json；点亮后dmesg SHA-256为`20e891dce347f5818b40b96ba1531b7dcdf4fc563a41a6f040dc820c232b1db4`，运行DTB为`de6203b618ec1e7f1fd68461c462af40db6fd9b9e94ad62b3c33926dd35a9e19`。

本轮L3确认重复控制台接管已消除，原生链路/背光正常；新镜像花屏目视结果待用户反馈，不提前关闭视觉验收。当前保留点亮画面供观察，未启动Android；如需继续重建仍先退Fastboot。


## 第四轮目视失败与回退

用户确认：本轮没有先竖屏后横屏现象，但仍有彩色花屏，之后屏幕持续黑屏、不显示日志。单次接管轮未观察到旋转，尚不能据此证明因果；花屏未解决且出现可见出图回归；本轮L3失败。

已保全 `first-frame/black-screen` 的显示状态、未过滤dmesg与最终FDT，随后经MiSeed USB命令回Fastboot，product=umi。未回Android、未刷写分区。故障dmesg SHA-256 `1e916ead6f38da74fb98cff2109600b2e43ed0038b90c9c94d92ee42e65f3158`；FDT与第四轮一致，严格审计仍仅root compatible/model失败。

撤回禁用simple-framebuffer的DTS覆盖及其门禁/测试，保留已验证CCF修复。初始化、连接状态和背光读回不能代替目视出图；继续检查fbdev绘制和DSI首帧顺序。


## 撤回试验后的复建

从当前工作树重新执行原生构建，`restore-console` 的 boot.img、DTB、配置、rescue-init与initramfs均与此前文字清晰的CCF修复轮逐字节相同；boot SHA-256仍为 `75adfc24493264f4a52624a7f849a02bcc03cf674f1b4b0de905e0bd41249e24`，来源锁SHA-256仍为 `14010bd1cf7e44bd83c41e92b94468ef0b9b23c8687842976f3ae41de19f9b98`。此轮专门验证撤回禁用simpledrm后是否恢复出图；不把恢复镜像称为花屏修复。


复建L2、定向binding/DTB schema及四项DT反例通过。全量258项中257项通过，唯一失败为新增状态导致CONTEXT超1200字符；精简后重跑test_checks全部20项通过，仓库检查及diff空白检查通过，未重跑未受影响测试。既有源码黄灯保留，未提交/推送。

第五轮已从Fastboot RAM启动上述精确镜像并采集 `restore-console/attempt-5` 与 `attempt-5-light`。simpledrm和原生两个fbcon接管均出现，59条DCS完成，亮度1500传输成功；没有PLL lock failed、时钟WARNING、显示SID fault、DSI DMA timeout/FIFO错误。点亮后dmesg SHA-256 `d7d13533e9bf8a3f6ed6736be95fb3d4250cc72be0bb4b5539fce6999e251e91`，运行时FDT SHA-256 `4a6f82d03574a77fc668b1e2d17846a95485098f084f2d6b946e0b2190788d42`；严格审计仍只有compatible/model两项失败。该次采集后取得维护者目视反馈，最终状态见收尾结论与下节。


## 最终采集与安全结束

收到最终目视反馈后，采集 `restore-console/closeout` 显示状态、未过滤dmesg与最终FDT，再发送bootloader并核实Fastboot product=umi，回退成功。最终dmesg SHA-256 `303cf47105163ff27a404277c3513a890b7ec939bf91982afe6bda1642765a31`；运行时FDT仍为 `4a6f82d03574a77fc668b1e2d17846a95485098f084f2d6b946e0b2190788d42`，严格审计仍仅根compatible/model两项失败。未回Android、未刷写或修改真实块设备，未导出旧RAM镜像。原始日志与镜像仅留本地忽略目录。后续只针对剩余白色首帧异常继续调查，本轮不再增加试验改动。


收尾复核：最终文档状态下WSL仓库测试20项、CCF测试3项全部通过；仓库文本/链接/预算/来源锁及diff检查通过。最终构建输入逐项匹配恢复版来源锁，最终日志无前述PLL/时钟WARNING/显示SID/DMA/FIFO错误。维护者明确允许本次收尾忽略PR行数和文件数限制，授权提交并推送PR6；合并/发布未授权。
