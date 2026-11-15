# Umi 原生显示 L3：sysmem 标志修复后的启动瞬态

| 项目 | 本轮记录 |
| --- | --- |
| 日期 / 操作者 | 2026-11-15；维护者目视确认，Codex按本轮明确授权执行 |
| 范围 | 一次RAM启动、标准CSOT背光0→1500、只读状态/内核日志/FDT/像素采集，返回Fastboot；不刷写、不回Android、不改有线PD/PPS或无线RX/TX参数 |
| 设备 / 环境 | 唯一Fastboot `product=umi`、`unlocked=yes`、`is-userspace=no`；既有Umi/CSOT变体；Darwin arm64 / zsh，不记录序列号 |
| 输入 | 分支 `codex/linux-standard-interfaces`，基线HEAD `f349fec9` 加本轮未提交修改；内核固定 `f45ed76d6791cf72ff1265ee93511175bbbc2f18` |
| 镜像 | `.work/display-debug-l2-native-20261115/boot.img`，SHA-256 `9dfaaf05c98abab6142f8efcbbc629812af5f67b9c46939ceb6d16c76f792a90` |
| 来源锁 | 构建锁 SHA-256 `bac0043a64350f761a36d5d9dbfd656211773392fe0d29688054bae271df53a6`；启动前逐项核对工作树输入及DTB/config/镜像 |
| 原始材料 | `.work/display-debug-20261115/`，`l3-evidence.json`保存文件长度与哈希，长原始日志/运行FDT不入库 |

## 执行

设备开始处于旧MiSeed RAM环境。先只读导出版本和mounts，确认rootfs/devtmpfs/proc/sysfs/debugfs/configfs、没有真实存储挂载；随后发送bootloader并确认返回唯一Umi Fastboot。

`boot_once.py --execute`：身份/哈希检查通过，唯一一次 `fastboot boot` Sending/Booting均OKAY，退出0，约0.432s。该脚本持久记录本次已尝试，不自动重试。USB CDC随后重新出现，读取到新命令表含display-frame、Linux6.18.60与Umi/CSOT banner。

基线用准备好的Mac采集器导出version、display、完整dmesg、DTB和mounts，退出0。挂载仍仅RAM/虚拟文件系统。运行FDT共2419节点、552151字节，SHA-256 `246cffaa6be3157fe9f3fde81522487a6def6fd38ccbe2e823ff00b40f5e3f3b`；严格审计仍FAIL，只有既有根compatible/model变化，没有live-hardware/RAM新差异。

| 时间（boot秒） | 事件 |
| --- | --- |
| 1.791673 | simpledrm初始化 |
| 1.826556 | fbcon首次切到146×33 |
| 2.398751 | 退回dummy console |
| 2.650647 | 原生CSOT DSC及59条DCS完成 |
| 2.651531 | 初始亮度1、transfer_rc=0 |
| 2.669680 | 原生fbcon切到146×33 |
| 2.709774 | msmdrmfb建立 |
| 2.805355 | 运行/init |
| 94.459128 / 94.470071 | 授权的背光0→1500，两个transfer_rc均0；标准读回1500、rc=0 |

原生DRM状态为DSI-1、1080×2340@60、235655kHz、XR24 little-endian、pitch4352，src/dst均完整1080×2340且offset0。基线与点亮后完整日志均未见PLL lock failed、时钟WARNING、显示SMMU context fault、DSI DMA timeout/FIFO错误；此前sys_fillrect/sys_imageblit的虚拟地址空间警告已消失。3da0000 GPU SMMU deferred/error-110及早期FG依赖告警仍存在，不据此报告整机零告警。

## 失败项与目视结果

`display-frame`返回 `FRAMEBUFFER_ERROR fb0 unavailable`、`FRAMEBUFFER_END rc=-1`。本轮 resolved.config 只有FB_CORE，没有FB_DEVICE，运行/sys/class/graphics中仅fbcon；因此没有/dev/fb0，未执行任何像素读取、没有有效缩略图。这是接机前漏查的采集前提，不是已得到像素验证；设备操作前的合成测试没有覆盖该配置缺口。

维护者明确反馈：启动阶段仍有与此前相同的手机顶部（横屏右侧）白条/彩条，进入正常日志显示后没有问题。**sysmem标志修复消除了已知告警，但没有消除启动花屏；稳定出字正常。** 原因范围收窄到启动/接管阶段，仍不能仅凭时间线认定simpledrm或首帧DCS为根因。

收集完成后发送现有bootloader命令，确认唯一Fastboot、product=umi及Bootloader模式，退出0；未执行第二次RAM启动，未刷写或启动Android。发现采集准备缺项后先结束真机操作，再回离线修复。

| 证据 | SHA-256 |
| --- | --- |
| 基线完整dmesg | `82fefb3d1f948eaf8e39e9b3946324bdd87c75d29931052ed318ad8e2b185382` |
| 点亮后完整dmesg | `81c66d36f1f59f42b83b4a07efffc6c840a9ad74b5f51e1c784cce610e64dcb5` |
| 像素采集失败响应 | `7092424b13f4869f121a843f3f34bc301fb26ccead2b07bcc3ae27cfeafeb8c5` |
| 最终Fastboot确认 | `4565a313200eb82b16e619b9175c89f680e27dae5addc7874f9037d926ed52ca` |

🟢 本镜像原生初始化、标准背光、稳定出字、Fastboot回退及sysmem警告消失；🔴 启动无花屏验收仍失败，像素采集未通过。后续补FB_DEVICE门禁，并在保留simpledrm/MDSS reset/CCF修复/面板时序的条件下准备延迟fbcon接管对照，见[源码续查](../reviews/2026-11-15-native-display-framebuffer.md)。首帧、独立供电/PM、根交接、专项复核仍开放。

## 第二次授权 RAM：延迟控制台对照失败

维护者另行明确授权再启动一次对照镜像，沿用背光/只读采集/回Fastboot边界。精确镜像 `.work/display-deferred-l2-native-20261115/boot.img`，SHA-256 `0689e04f1dc1ac1d702c11117b995d8b6e389f5d756589f74846a8ca65934d8d`；锁 `0f22d0697767b8ab55d02fba1c8d27093101db915e98a980b2839fa4c4d7ae80`。两版DTB、initramfs和rescue ELF逐字节一致；本版启用标准FB_DEVICE及延迟fbcon，原生命令行loglevel=0，未改面板DCS/复位/亮度初值。

唯一Umi/解锁/Bootloader身份与所有输入哈希通过后，一次Sending/Booting均OKAY，退出0，约0.436s；USB正常出现。实际/proc/cmdline为loglevel=0。日志证明simpledrm驱动仍在0.082719s初始化，0.087441s延迟接管；MSM fbdev于0.286677s建立且继续延迟；0.357447s启动/init，0.365984s首次文本触发接管；0.453424s DSC/59命令完成，0.453705s初始亮度1传输成功，0.500692s切到146×33。只出现这一次fbcon切换，没有此前的早期simpledrm文字接管。

维护者反馈：启动仍短暂花屏，并新增一条在竖屏中间横穿的彩条，随后看不到日志。此时尚未执行1500点亮，反馈对应初始亮度1；不能将其写成“1500点亮后也黑屏”。启动目视已失败，未继续提高亮度或重复启动，只读保存现场后立即返回唯一Umi Fastboot，退出0。

本次FB_DEVICE确实生效，/sys/class/graphics有fb0；像素导出成功，`id=msmdrmfb`、1080×2340、virtual同尺寸、offset0、32bpp、stride4352、var.rotate=0。完整293行及END rc=0；物理方向135×293缩略图39555像素，只采到黑色33827点和灰色0xaaaaaa共5728点，内容为正常灰黑文字。此图晚于启动、步长8且非原子，不能证明更早瞬态不存在，也不能单凭图像解释当时低亮度黑屏。

最终dmesg无PLL/时钟WARNING、显示SMMU/DSI DMA/FIFO或sysmem虚拟地址告警；运行树严格审计仍只有根compatible/model两项FAIL，没有live-hardware/RAM新增问题。运行FDT SHA-256 `0f0c9301977f8c54960d3a60810af7fbcc588e01c5bfc6fd1c07cd994fb1022d`。

| 第二次证据 | SHA-256 |
| --- | --- |
| 基线dmesg | `2424f4795dcece095d51d46b131563975a798e90dffb8d9882bcf919dee66121` |
| 最终dmesg | `8bca87a5f72bbc842ce080de6a9a58fc16575d173a4089aff44f9c9087114043` |
| 成功像素导出 | `878c00b527851ad5183be38151ed97d43659414564bccf71f9f00aa5b91215a4` |
| 缩略图PNG | `c359e6fa5bf616df1edd375e77b21facd9534f6a727bdf4b3f5c0a1068291f2a` |

材料留 `.work/display-deferred-20261115/`。结论：延迟接管本身没有解决启动花屏，已撤回quiet/deferred配置，保留已验证的FB_DEVICE与sysmem修复。不能把单次控制台接管、无错误日志或正常后采像素当作启动无花屏通过。

## 第三次授权 RAM：初始背光 0，稳定点亮通过

精确镜像 `.work/display-initial-off-l2-native-20261115/boot.img`，SHA-256 `7bbf4bf281d12d2cf72df6d184a339363a6691d7425ae1b11cbc9389166f8a36`；来源锁 `0647a5373f763f22dc0e76e0a8722ebb9f70feb13de918e80ba6ee13ae20eb7f`，config `5f05b01bc4dde7d9d66b91cd7d8345234cd7dade26dbfd9bfd56f5f3e054f211`。普通loglevel=8与原fbcon接管恢复，FB_DEVICE保留，面板默认亮度由1改0；DTB、initramfs、rescue ELF与前两版一致。此前L2/schema/265项Linux零跳过回归通过，完整证据见源码续查。

首次身份核对中，Fastboot远端三项getvar均返回unknown command，客户端却退出0；未发送镜像，也未创建启动标记。脚本原本已因缺失身份字段停止，后补充拒绝输出中的FAILED/error，即使客户端返回0也停止。维护者确认FASTBOOT图标并手动重启回Fastboot，再明确要求重试；本次身份均通过，唯一一次Sending/Booting均OKAY，约0.433s。未在身份异常时盲发镜像。

基线日志：1.823987s早期fbcon，2.373642s退回dummy，2.615226s CSOT DSC/59条DCS完成，2.616177s初始brightness=0、transfer_rc=0，2.633845s原生fbcon，2.766956s运行/init。维护者没有看清启动过程，因此这一轮**启动瞬态未验证**，不能记作已消除白条。

按既有标准路径在140.450056s请求0、140.461343s请求1500，两个transfer_rc均0，标准读回1500、rc=0。维护者明确确认：日志文字正常，没有白条/彩条。完整293行像素导出成功，布局仍为1080×2340、32bpp、stride4352、offset0；缩略图39555点仅含黑色31453点及灰色0xaaaaaa共8102点。该后采缩略图仍不替代早期目视。

最终日志无PLL/时钟WARNING、显示SMMU/DSI DMA/FIFO或sysmem告警。运行树严格审计仍仅根compatible/model两项FAIL，运行FDT SHA-256 `5d737e57480fadf80e9dfe60bd279fe9ae49feb75a5a0d4cf53aa970cee6cfc2`；未新增live-hardware/RAM差异。保全材料后通过现有bootloader命令返回唯一Umi Bootloader Fastboot，退出0。没有刷写、进入Android或执行充电测试。

| 第三次证据 | SHA-256 |
| --- | --- |
| 基线dmesg | `36b8eeaf73b4365f901f263ed0bcf6186371e5377eed15f08009f934a3e6f360` |
| 最终dmesg | `687cdb7ff37fe431f4e98711884bea88cde086ef634034835610cfccb8177fda` |
| 点亮响应 | `f11e67e8874d3cf2be960606dfbc07752f86030378d1e299d553b8ad79768123` |
| 像素导出 | `89d079df38caceb4bf7b97a478e430179428ad483a5e7d2cc2cfa03a94f5ffdd` |
| 缩略图PNG | `20a525326ce5cdcd2218843b734ba7c52d67ec5aa8c790da85c394fa387c20a7` |

材料留 `.work/display-initial-off-20261115/`，`l3-evidence.json`记录文件大小/哈希及像素计数。结论：初始0→标准1500的稳定出图通过；启动无花屏尚无本轮目视证据，需另行授权观察同一镜像。

## 第四次授权 RAM：同镜像确认启动仍花屏

维护者确认已准备持续观察，并单独授权同一初始0镜像再执行一次RAM启动/只读采集/标准点亮/回Fastboot。沿用第三次精确boot/config/来源锁；启动前逐项哈希及工作树输入通过。一次Sending/Booting均OKAY，约0.427s，未刷写；新启动标记和证据独立留在 `.work/display-initial-off-repeat-20261115/`。

维护者明确反馈：从FASTBOOT消失到黑屏之间**仍有白条/彩条**。初始0策略没有消除启动瞬态，不能沿用第三次“未看清”作为通过。既然启动观察已失败，本轮只读保全现场，未再请求1500；第三次已经确认的稳定1500出图结论继续保留。

日志时间线：1.827176s早期fbcon，2.370380s退回dummy，2.606536s DSC/59条DCS完成，2.607387s亮度0传输成功，2.627317s原生fbcon，2.762460s运行/init。未见PLL/时钟WARNING、显示SMMU/DSI DMA/FIFO或sysmem告警；像素293行完整，1080×2340、32bpp、stride4352、offset0。缩略图39555点含黑色31455点、灰色0xaaaaaa共8100点，仍是后采正常文字，不可补作启动证据。

运行FDT SHA-256 `c539812093a6ca471b0c625bf712323d4886022ab7ed8d58b52597c747c99b91`；严格审计仍只有根compatible/model两项FAIL。继承simple-framebuffer实值1080×2340、stride4320、a8r8g8b8；原生MSM stride4352来自独立GEM缓冲，二者不同不能直接认定配置错误。采集后发送bootloader并确认唯一Umi Bootloader Fastboot，退出0；没有提高背光、重试启动、进入Android或充电验证。

| 第四次证据 | SHA-256 |
| --- | --- |
| 基线dmesg | `48b1971daf0116efdf31e8969ce230f8d357cb6b21f90cbd784c2da7f6a87eec` |
| 像素导出 | `f4562073ffab359200b2733c1cf500caefc7c2197745adb59dfa57af0c762f08` |
| 缩略图PNG | `8245058106ab754311a13671797a392227573df45240b2b797f310a25c97ede5` |

结论：初始0与延迟控制台两条可见性策略均未解决启动白/彩条，实验均撤回，恢复原生亮度初值1及原控制台路径；FB_DEVICE、sysmem标志修复和只读采集保留。**启动无花屏仍失败，稳定文字与标准1500点亮已经通过。** 当前设备已回Fastboot，后续真机轮次须重新备齐针对性材料并单独授权。

## 维护者补充观测：先花屏，后出现可见日志

2026-11-15维护者进一步明确：花屏发生在进入可见日志之前，要求定位重点前移，不继续以增加后段功能推进。本段是对既有现象的补充反馈，没有新RAM启动、镜像或真机采集；不能把“可见日志之前”等同于“内核尚未执行”，也不能仅凭此确定早于哪一次fbcon切换。后采像素、稳定日志及1500点亮继续只证明各自阶段。

此前提出的内存追踪镜像RAM轮次尚未获得本轮明确授权，不执行该方案；已完成的L2材料保留作离线工具，不称为花屏修复。后续定位聚焦固件扫描退出、早期SMMU/时钟及MDSS/DSI接管，具体实现须依据早期调用关系和可验证的对照，而非增加rescue功能。

## 第五次授权 RAM：早期面板复位仍花屏，回 Android

维护者明确授权早期复位候选，并确认可持续观察。最初只读Fastboot预检返回unknown command，未发送镜像；随后维护者报告已自行重启设备、现处于Fastboot，纠正并新增约束：**以后调试结束正常重启回Android，不长期停留Fastboot**。该最新要求替代本轮旧计划中的Fastboot收尾，规则写入AI_RULES。没有将最初的查询失败算作RAM启动，也未用它放宽身份检查。

本轮重新预检精确镜像、DTB、config、来源锁及全部当前输入通过；脚本确认唯一product=umi/unlocked=yes/is-userspace=no，独占attempt文件只允许一次发送。候选 `.work/display-panel-reset-l2-native-20261115/boot.img` SHA-256 `2e6f8442f54cfa8822226754e20f1e14c19b7b0286d24984ba15c3113ec08b3f`；DTB `a3159f9c3693f82064e20f498cd39c485d28539c761be89bda45abc4ed90fc93`；config `5f05b01bc4dde7d9d66b91cd7d8345234cd7dade26dbfd9bfd56f5f3e054f211`；锁 `92aef9cae6c7c861b49ac872992b0fc324c693479eb7f77a55fbb95667893714`。一次Sending/Booting均OKAY，总计0.434秒，不刷写。

维护者明确反馈：**仍有白条或彩条，随后日志正常**。因此该候选启动无花屏验收失败；按计划不再请求1500，不重试启动。仅从唯一已核对的MiSeed USB控制台只读采集version/cmdline/mounts/display/dmesg/DTB。

运行FDT SHA-256 `d6f0ce860d0b6e5421e792d0afb68e57aff801950c96cc0115ae0aa94c441fca`。完整live_hardware、RAM检查无新增差异，严格审计仍只有root compatible/model两项FAIL。最终运行树保留MDSS init/空default、GPIO12物理低状态以及面板active-low GPIO12引用；未见pinctrl激活/引脚申请失败、PLL lock、显示SMMU context fault、DSI DMA timeout/FIFO或sysmem告警。FDT和无错误日志不等于测到了真实引脚电平，不据此精确断定花屏早于哪个硬件时刻。

时间线：1.828253s早期fbcon，2.384612s退回dummy，2.626693s DSC/59条DCS完成，2.627550s亮度1传输成功，2.646121s原生fbcon。3da0000 GPU SMMU超时等既有非显示项单列，不能把它冒作本轮显示故障。本轮没有新增像素或1500目视证据，既有稳定能力记录保留。

采集后通过已确认USB控制台发送一次正常 `reboot`，没有发送bootloader。ADB只读确认单台umi、Android release16/SDK36、`sys.boot_completed=1`，完成回Android；不记录设备标识。没有再次进入Fastboot、重复RAM启动、充电测试或存储操作。

证据与逐文件大小/哈希在 `.work/display-panel-reset-20261115/l3-evidence.json`，包含boot/attempt、六份基线采集、runtime.dtb/audit及return-android日志。已按候选精确哈希保存三个实验输入副本；仅撤回本轮DTS、配套门禁和测试，恢复此前源码与来源锁，其余既有改动不动。**早期复位方案未解决本机花屏，不能保留为有效修复。** 恢复后离线验证见源码续查；没有新镜像L3、提交或推送。

## 收尾补充（无新增设备操作）

维护者补报相较上次提交可能新增中间横条，原为顶部瞬时白条，均在正常日志前短暂出现；根因未确定，不能将此前中部彩条只归到某一候选。维护者接受该启动瞬态为本PR非阻断延期项，要求收尾推送PR6。所有失败时序候选已撤回，未上机追踪代码已移除；最终源码/镜像的离线验证见[最终收尾](../reviews/2026-11-15-native-display-framebuffer.md#最终收尾复核与延期2026-11-15)。最终版未再上机，本段不新增L3通过结论。手机此前已回Android16。
