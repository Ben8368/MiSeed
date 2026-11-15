# 原生显示白条续查：fbdev 修复与像素采集

## 范围与事实

维护者要求 debug 上一份提交的原生显示花屏。宿主 Darwin arm64 / zsh；工作分支 `codex/linux-standard-interfaces`，基线 `f349fec9f6f1562eead915bdc61ae4252f85226f` 加本轮未提交改动。保留已有充电修复与 L3 文档；以下先记录离线准备，后续授权的一次RAM验证见[硬件记录](../hardware/2026-11-15-umi-native-display-framebuffer-l3.md)。未提交或推送。

[上一轮 L3](../hardware/2026-11-13-umi-native-display-l3.md) 已证明 CCF 修复后的文字、背光与 Fastboot 回退；最终仍有顶部（横屏右侧）约一行高度的白色花屏。禁用 simpledrm 的试验出现彩色花屏后持续黑屏，已撤回，不能复用为修复。维护者本轮无法确认白条是瞬态还是持续存在，两种情况均保留。旧 Windows 原始采集目录不在当前 Mac 工作区，引用归档中的已核实结果，不声称重读了旧原始日志。

## 固定源码排查

从既有 Linux Git 缓存的精确对象 `f45ed76d6791cf72ff1265ee93511175bbbc2f18` 提取 fbcon、DRM、MSM、面板桥与内存绘制代码，哈希和脚本在 `.work/display-debug-20261115/`。

1. **确认缺陷：MSM fbdev 缺少系统内存标志。** `msm_fbdev_driver_fbdev_probe()` 将 GEM 虚拟映射放入 `screen_buffer`，使用 deferred sysmem ops，却没有设置 `FBINFO_VIRTFB`，与旧 L3 的 `sys_fillrect/sys_imageblit` 警告吻合。上游已有 Dmitry Baryshkov 的一行修复，提交 `ea9dadeac79cef509a4b8b4a3e3b39a741e63313`；[原始补丁](https://lists.openwall.net/linux-kernel/2026/07/30/1538)、[提交记录](https://git.zx2c4.com/wireguard-linux/commit/?id=ea9dadeac79cef509a4b8b4a3e3b39a741e63313)。原源码 SHA-256 `eac2e61de75789f513f58d7ec00fd574f920d8dad6d7e35c4558b95fe9579822`，回移后 `feb43e37bf654d9a57565da4441a48b71c38a76d5be8e073aac9d4202f1a6b69`。绘制 helper 仍会执行实际绘制，因此缺标志不能单独解释白条或前次黑屏。
2. **未复现静态边缘漏清。** 固定 `ccw_clear()` / `ccw_clear_margins()` 在 1080×2340、TER16x32、rotate:3 下，146列×33行及4px/24px边缘覆盖整个可见缓冲，无越界。合成桩首次因未使用变量编译失败，修正桩后实际执行通过；只证明此几何条件，不证明启动顺序、颜色、damage 或真实首帧正确。源码中 startup/init 都在配置 blitting 前设置 initial_rotation，未找到足以修改旋转算法的依据。
3. **保留首帧候选。** 面板 prepare 内包含 display-on，MSM atomic tail 在 modeset enables 后才 flush_commit。仅移动 0x29 到 panel enable 并不保证首帧已提交；本轮不猜测复位、DSC、DCS 延时或顺序。旧 simpledrm 双接管也不能凭时间接近就定为根因。

## 接口审查与实施

| 功能 | 标准接口 / 消费者 | 本地差异 |
| --- | --- | --- |
| framebuffer 内存类型 | DRM MSM GEM / fbdev sysmem ops / fbcon，后续 Debian KMS | 原生 profile 回移上游 `FBINFO_VIRTFB`；无新 ABI/binding |
| 像素与布局读取 | fbdev `FBIOGET_FSCREENINFO`、`FBIOGET_VSCREENINFO`、只读 pread | rescue `display-frame` 仅诊断，归 TD-006；不作为 Debian 产品依赖 |
| 显示状态与日志 | DRM/backlight sysfs、已有只读 display/dmesg/FDT 导出 | 复用既有路径，不修改用户态/内核保护策略 |

`tools/apply_msm_fbdev.py` 锁定原始文件哈希与唯一锚点，只在原生 profile 添加一行标志；默认 profile 恢复原文，缓存验证只接受原文或精确补丁。来源与许可登记于来源锁、display/NOTICE，保留原文件 Red Hat / Rob Clark 版权与 GPL-2.0-only；上游作者 Dmitry Baryshkov 不改写。

新增 `display-frame`：只打开 `/dev/fb0` 的字符设备，只读 ioctl/pread；严格核对 `msmdrmfb`、1080×2340、32bpp RGB 布局、stride、虚拟尺寸、offset 与缓冲上界，错误或短读停止且不打印 COMPLETE。拒绝其他 fb 身份；不 mmap、不写像素、不设模式、不 blank、不操作背光或寄存器。输出真实布局及135×293、步长8的RGB缩略图，约240KiB文本；CPU解码与GPIO/DSC无关。

缩略图为物理方向；fbdev `var.rotate` 不等于 fbcon 软件 rotate:3。协议明确 `atomic=0`，不把绘制期间的非原子读取或8px采样当作完整无花屏证明；小于采样步长的细线可能漏采。若采图已经出现对应白条，可沿 fbcon/内存绘制继续定位；若未出现，还须考虑采样遗漏、启动瞬态以及面板/首帧链路，不能直接断定 DSI 故障。

## 验证

- `python3 -X utf8 -m unittest discover -s tests -p 'test_msm_fbdev.py' -v`：3项通过，退出0，覆盖原生/默认切换、重复应用、未知修改、缺失/重复锚点与符号链接拒绝。
- Linux `test_framebuffer.py`：2项通过，退出0；真实采集C函数对模拟 ioctl/pread 验证颜色、4352字节stride、293行顺序、EINTR、非MSM身份、短缓冲/错误格式/offset溢出/短读/读取失败。完全使用合成内存，没有设备访问。
- `.work/display-debug-20261115/check_rotation_geometry.py`：退出0，固定清屏函数的上述几何覆盖通过。缩略图 PNG 解析脚本的完整/缺行/错序/失败/重复采集反例通过；Mac采集脚本 `--help` 预检通过，未打开串口。
- macOS Python3.11 全量263项，退出0，44项平台/DT环境跳过；Linux ARM64 Ubuntu24.04 全量263项，11.330s，退出0、零跳过。Linux完整回归在schema生成完成后使用固定schema与新DTB运行。
- `MISEED_WORK_ROOT=/build MISEED_EXTRACTED_SOURCE=/build/display-debug-source-20261115 MISEED_BUILD_ROOT=/build/display-debug-build-20261115 MISEED_OUTPUT_ROOT=/workspace/.work/display-debug-l2-native-20261115 MISEED_NATIVE_DSI=1 MISEED_JOBS=4 bash tools/build_kernel_l2.sh`：既有 `miseed-boot-l2:local` 容器内退出0；Linux固定版本如上，GCC13.3.0 / make4.3。完整原生ARM64内核、rescue、链接与打包通过，无时钟或编译警告。
- `/build/pd-pps-dtschema/bin/python tools/validate_kernel_dt.py --source /build/display-debug-source-20261115 --build /build/display-debug-build-20261115 --dtb /workspace/.work/display-debug-l2-native-20261115/umi-csot.dtb`：退出0，定向binding、CHECK_DTBS、最终DTB及4项DT反例通过、零跳过。
- `.work/display-debug-20261115/verify_l2.py`：退出0；全部本地输入符合构建锁、staged MSM补丁逐字节匹配、boot.img内核/DTB/initramfs与对应产物逐字节一致，rescue内含新命令；simpledrm与原生panel配置均保留。`shellcheck --severity=error tools/build_kernel_l2.sh`：退出0。仓库检查与 `git diff --check` 退出0；现有源码篇幅黄灯沿TD-008。

## 本轮载荷与后续 L3

产物在 `.work/display-debug-l2-native-20261115/`，未覆盖旧显示/PD镜像。新镜像含当前工作树此前的有线PD监测修复与本轮采集器，不能称为纯 `f349fec9` 或只改变显示标志的真机单变量对照。

| 输入 / 产物 | SHA-256 |
| --- | --- |
| 来源锁及构建副本 | `bac0043a64350f761a36d5d9dbfd656211773392fe0d29688054bae271df53a6` |
| boot.img | `9dfaaf05c98abab6142f8efcbbc629812af5f67b9c46939ceb6d16c76f792a90` |
| DTB（与旧清晰出字版一致） | `5329ec0c6a92abf710ed06173a0792c05ef075adf6a7ced019b79c1cb69aa1b3` |
| resolved.config（与旧版一致） | `bfb74ba74df26b0beea259a7adec10e38fb6c7e265c9f4b046af76ac0405e999` |
| rescue initramfs | `a075731e2dceb8b58458f421127a7b8dab0cb6aa23f68162cab83d17f0186df5` |

L2日志SHA-256 `874361e0f2e8b895deea99a57be434a4528ae48d9a4e268fd23f881c627cb5fd`；schema日志 `17a4c14530f9152871b3dee5b56a8725913ab6a3b6b4d110df6adeea01f5360b`；Linux回归日志 `3dddc94b73dfa7856412a1c90b6ae3f3fc95a8b4f244c6ed76acd696e391ad64`。原始日志、源码提取、核验JSON、Mac采集/PNG解析脚本及详细接机计划都留 `.work/display-debug-20261115/`，不入库。

后续 L3 需本轮明确授权一次RAM启动、按既有标准接口点亮/熄屏和回Fastboot；先核对唯一Umi/CSOT、解锁状态与上述哈希，再采集display/dmesg/FDT/像素，并由维护者确认启动瞬态和点亮后的顶部/横屏右侧。启动瞬态可能早于后采缩略图，必须保留目视时间范围。遇PLL/SMMU/DMA/FIFO异常、背光传输失败或黑屏立即停止并回Fastboot，不重复启动、不改供电参数、不绕过SMMU。根compatible/model的既有严格审计FAIL继续保留；不启动Android、不刷写。

🚦 🟢 本轮软件缺陷修补、L0/L1/L2/schema与来源边界；🟡 白条候选和像素采样限制；🔴 新L3、开发者以外专项复核与同HEAD CI未完成。白条根因、首帧、面板独立供电/PM仍归RISK-010/TD-006，不能据已编译或已消除告警条件关闭目视验收。

## 一次 RAM 验证后的对照准备

上述首版镜像经本轮授权L3确认sysmem警告消失、稳定文字正常，但启动白/彩条仍有；像素采集因FB_DEVICE未启用失败。已先保存现场并回Fastboot，没有在连接状态下重建或重复启动。精确输入、时间线与目视结果只在上述硬件记录维护。

下一版仅调整原生诊断控制台profile并补齐标准fbdev接口：`CONFIG_FB=y`、`CONFIG_FB_DEVICE=y`、`CONFIG_FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER=y`，原生强制命令行改为`loglevel=0`。固定Kconfig的延迟接管依赖FB=y、FRAMEBUFFER_CONSOLE和DUMMY_CONSOLE；FB框架并不意味着启用legacy simplefb，后者继续拒绝。simpledrm驱动、MDSS reset、CCF修复、DSC、DCS和背光初值均保留。

依据固定fbcon的deferred_takeover：framebuffer注册时先不接管，首次dummy-console文本通知后通过worker注册现有fb。停止早期printk向屏幕输出，目的是让现有rescue在/init阶段输出tty0诊断时才触发接管，而不是禁用固件DRM驱动。现有rescue只镜像`miseed-power`消息，不是完整dmesg重放；完整内核日志仍保留在1MiB日志环和USB只读导出。本版是诊断对照，不据源码推断已经无花屏。

新增`check_display_profile.py`在L2编译前验证完整命令行、FB_DEVICE与延迟接管依赖；打包器`--native`使用同一命令行定义，默认profile保持loglevel=8。回归覆盖原生/默认混用、缺失FB_DEVICE/依赖、重复配置、legacy simplefb及打包头错误。此门禁专门防止首版采集前提漏查再次发生；新镜像L2/schema/目视结果在完成后追加，不沿用首版结果。

对照版离线验证已完成：独立source/build/output目录执行同一锁定构建，L2退出0；定向binding/CHECK_DTBS/最终DTB及4项反例退出0；完整Linux265项、11.410s、零跳过，Mac265项通过但44项平台/DT环境跳过。打包核验确认native强制命令行loglevel=0、FB/FB_DEVICE/deferred takeover均为y，且fb_fops、fbcon_output_notifier、fbcon_register_existing_fbs已链接。两版DTB、initramfs、rescue ELF逐字节一致；面板时序和硬件描述未改。ShellCheck error级、仓库检查及diff检查退出0。

对照输出 `.work/display-deferred-l2-native-20261115/`：boot.img SHA-256 `0689e04f1dc1ac1d702c11117b995d8b6e389f5d756589f74846a8ca65934d8d`，resolved.config `ca027d484c53eb8950588e3252c427f66f49a6cfeeffefa0b9af1d499ec71ae7`，来源锁与构建副本 `0f22d0697767b8ab55d02fba1c8d27093101db915e98a980b2839fa4c4d7ae80`。核验、完整日志、唯一一次启动脚本、采集/回退/PNG工具及接机计划在 `.work/display-deferred-20261115/`；启动脚本默认只核验材料，不访问设备。此前的一次RAM授权已执行并回退，第二次对照需再获明确授权；此处尚不计为对照L3通过。

## 延迟接管失败后的可见性对照

第二次授权L3已执行并回Fastboot，时间线与目视失败归上述硬件记录。延迟接管不能消除启动彩条；FB_DEVICE接口和像素读取则已成功。**已撤回**原生loglevel=0、FB=y及deferred takeover配置，恢复普通loglevel=8/原fbcon路径与原打包命令行。门禁现在明确拒绝残留的失败deferred配置，保留FB_DEVICE检查。

后采像素为正常灰黑文字，不能解释更早的彩条；下一步只测试原生初始背光1→0，使DRM自动enable之后也保持暗态，等原生状态和帧缓冲检查完成再由既有display-on请求1500。这使用已有backlight/DRM接口，无新增ABI、硬件阈值或延时，DCS表、DSC、GPIO复位及供电均不改。它是首帧可见性策略，尚不宣称根因已闭合；默认0之前的固件显示/更早瞬态仍需目视验证。诊断本来就依赖标准1500请求确认点亮，Debian消费者另验。

C回归从真实probe提取backlight初值，验证prepare后默认0、显式1500请求及失败时不发送亮度，保留原复位/59命令反例。L2复用已按固定Git树完整验证的源码/构建缓存，输出仍为独立目录；未删除旧镜像或跳过缓存身份检查。新的配置/打包门禁、源码与镜像核验均要求普通loglevel=8、FB_DEVICE=y且无deferred。

初始0版的L2、定向binding/最终DTB schema/4项DT反例、完整Linux265项（11.705s，零跳过）、Mac265项（44项环境跳过）、ShellCheck error级及仓库/diff检查均通过。构建与打包核验确认面板源码与当前工作树一致、原始普通命令行恢复、DTB/initramfs/rescue仍与首版逐字节相同。输出 `.work/display-initial-off-l2-native-20261115/`：boot.img `7bbf4bf281d12d2cf72df6d184a339363a6691d7425ae1b11cbc9389166f8a36`，resolved.config `5f05b01bc4dde7d9d66b91cd7d8345234cd7dade26dbfd9bfd56f5f3e054f211`，锁 `0647a5373f763f22dc0e76e0a8722ebb9f70feb13de918e80ba6ee13ae20eb7f`。启动/采集/像素解析/回退与接机计划在 `.work/display-initial-off-20261115/`，已做无设备访问预检；第三次RAM验证仍需新授权。此版启动后、显式点亮前保持黑屏是预期，需先区分“是否闪彩条”，再在1500下验证稳定出图。

## 初始 0 对照失败及保留范围

第三次授权RAM已验证初始0、标准0→1500、稳定文字/无白彩条、完整像素与Fastboot回退；维护者未看清早期启动，不计启动目视通过。第四次单独授权的同镜像观察确认从FASTBOOT消失到黑屏仍有白/彩条，保全只读证据后回Fastboot，未再次点亮1500。精确时间、载荷、哈希及反馈统一见上述硬件记录。

初始0没有消除故障，已撤回为原来的1；失败的quiet/deferred配置也保持撤回，DCS表/复位序列/DSC与供电不改。保留sysmem修复（L3告警消失）、标准FB_DEVICE（L3像素成功）、只读采集器及防止漏查的配置门禁。测试仍从真实probe提取初值，分别覆盖恢复后的默认1、显式0和1500，避免再次只用测试桩中的初值冒充真实probe配置。未把诊断策略保留为未经证实的花屏修复。

### 更早交接窗口的源码核对

固定Linux `msm_mdss.c`：`mdss_probe()`先调用`msm_mdss_init()`，其首项为`msm_mdss_reset()`；成功后才`of_platform_populate()`创建DPU/DSI等子设备。本地面板probe使用GPIOD_ASIS，直到prepare才执行已知三段复位、DCS初始化及亮度请求。因此面板默认亮度只影响后段，不能控制父MDSS复位/子设备probe之前仍由固件维持的显示状态。该调用关系是已核对的源码事实；花屏恰好发生于该窗口仍是候选，缺少与目视瞬态同步的阶段证据。

继承simple-framebuffer运行实值1080×2340、a8r8g8b8、stride4320，与1080×4一致；原生MSM独立GEM缓冲stride4352，不能把不同缓冲的stride直接比较成错误。不据此猜改pitch、DSC、复位延时、SMMU或寄存器。提前复位面板还涉及父/子设备与GPIO所有权，不能仅移动prepare、把GPIO挂到MDSS或新增私有绑定就视作主线接口修复。后续应将可见瞬态与固件退出、MDSS reset、面板prepare/首帧提交阶段对齐，再选择有因果证据的交接改动。

当前源码的启动无花屏未闭合；稳定文字、标准1500及已消除的sysmem告警保持通过。实验镜像与失败证据保留在.work，恢复后的最终L2核验另列，不用失败候选镜像的哈希代表恢复后的源码。

### 恢复后的最终离线验证

- 面板驱动已逐字节恢复HEAD版本，默认亮度1；FB_DEVICE仍启用，quiet/deferred不在最终配置中。没有在最终源码保留初始0实验。
- 最终原生ARM64 L2退出0；GCC13.3.0、固定Linux与既有已验证缓存，源码/打包核验通过。`boot.img`为 `ef803cc853769aabcd000ae480654c93c5e6b4a258a7d9eedbe45df60c03177f`，来源锁 `5b51ce5c2e1b8ea4f2cc5f907064d30812db79348db186572278c3c3c1667ae8`，config `5f05b01bc4dde7d9d66b91cd7d8345234cd7dade26dbfd9bfd56f5f3e054f211`。DTB、initramfs/rescue与此前已核验产物逐字节一致；复用该DTB/schema执行回归，不声称重新运行schema生成。
- Linux ARM64完整265项、11.603s、零跳过，退出0；Mac原生面板6项通过。ShellCheck error级、仓库预算/文本/链接/来源锁与diff检查通过，既有源码黄灯沿TD-008。产物核验首次因Python导入路径未配置而未执行，补齐PYTHONPATH后实际执行通过，没有更改源码来绕过核验。
- 产物 `.work/display-final-l2-native-20261115/`；核验和日志 `.work/display-final-20261115/`。L2日志SHA-256 `e0ad256c4d1f14788f4cb00c391748413b2a957cc4be56f454eb492f500bf384`，Linux回归日志 `35f959f87da1f744c1aa59e2a078dcdc5c4739c85f173acae71a3e7a7f2faab7`，打包核验日志 `cf986e7c5515f48b50dbcedaccdf54cdc632c06ab313ab3f6ad5f50b16924b54`。

最终恢复版没有追加真机启动，不以失败候选的L3替代该精确镜像L3；此前稳定文字/1500能力和本轮启动失败均继续如实记录。手机保持Fastboot，未提交、推送或合并。

## 继续定位：将早期交接与首帧分开取证

2026-11-15 再核对固定源码与四次原始 dmesg，仅离线读取，不访问设备。27份提取源码的SHA-256全部符合既有 `source-hashes.json`；源码调用顺序断言和四轮时间线提取退出0，结果留在 `.work/display-localize-20261115/evidence.json`。本节没有新驱动改动或新L2/L3，不重新声明旧测试结果。

### 已确认的调用关系

- `msm_mdss_init()` 在获取UBWC数据、ICC路径和时钟资源之前执行 `msm_mdss_reset()`；成功返回后，`mdss_probe()` 才创建子设备。因此复位并不意味着DSI/面板已经具备接管条件。后续资源申请若延迟探测，源码允许再次进入该路径；旧日志没有复位计数，不能声称本机发生了重复复位。
- `umi_csot_prepare()` 才开始操作面板GPIO12；probe使用GPIOD_ASIS。DCS表自身已有亮度0命令，随后退出睡眠、等待100ms、发送0x29及剩余命令；默认backlight初值0的实验只改变后续标准亮度请求，不能覆盖此前固件显示及MDSS/DSI接管。
- `msm_atomic_commit_tail()` 先 `commit_modeset_enables()`，随后才 `flush_commit()`；DPU的flush调用 `dpu_crtc_commit_kickoff()`。panel桥在pre-enable调用prepare，在enable调用 `drm_panel_enable()`/标准backlight。因此现有0x29、prepare完成和亮度日志均不证明本次DPU帧已开始传输；仅把0x29移至panel enable仍早于该flush。该调用顺序是事实，显示旧内容或首帧缺失导致白条仍是假设。

### 已有观测及缺口

第四轮相对开机时间如下；fbcon切换是软件控制台事件，不能当作面板第一帧完成事件。

| 时间 / 秒 | 日志事件 |
| --- | --- |
| 1.792617 / 1.827176 | simpledrm注册 / 早期fbcon切入 |
| 2.370380 | 控制台退回dummy |
| 2.606536 / 2.607387 | 面板59条DCS完成 / 标准亮度0成功 |
| 2.627317 / 2.667976 | 原生fbcon切入 / msmdrmfb注册日志 |

四轮都没有MDSS assert/deassert、面板复位开始、首个DPU kickoff/完成与目视花屏的同步标记。quiet/deferred轮的init为0.357447s、面板prepare完成为0.453424s；普通日志轮分别约2.76s、2.61s，不能跨镜像沿用同一个绝对时间来判断花屏阶段。后采正常像素继续只证明采集时内容。

下一次应先准备阶段取证镜像及完整采集/回退材料，再请求对应RAM轮次授权。优先使用标准启动追踪和既有MSM/DPU tracepoints，记录MDSS reset、DSI/面板prepare、0x29、首个kickoff及传输完成，并与屏幕录像对齐；须核实目标函数可追踪、事件实际启用和日志未丢失。官方机制见[Linux启动追踪](https://docs.kernel.org/trace/boottime-trace.html)。最终config `5f05b01b…f211` 中FTRACE、BOOT_CONFIG、KPROBES均关闭，不能声称当前镜像直接加命令行就可采到这些数据；新取证配置仍需L2与打包核验。

判别依据：若异常早于面板prepare，优先追固件扫描退出、MDSS/DSI接管；若在0x29之后、首帧完成之前，优先追可见性与首帧传输；若首帧完成后仍异常，则进一步核对帧内容/布局和DSI/DSC输出。追踪时序只能缩小窗口，根因仍须针对性单变量对照闭合。当前不能把MDSS reset、simpledrm、stride差异或0x29任一项直接定为根因；不追加猜测性的延时、GPIO/寄存器或供电修改。

本次仅更新定位记录及RISK-010跟进项；`python3 -X utf8 tools/check_repo.py`、`git diff --check`均退出0，既有源码黄灯沿TD-008。未修改驱动、构建配置或镜像，没有新真机操作、提交或推送。

## 可选内存启动追踪：离线材料已备

维护者继续询问能否解决后，本轮准备区分上述窗口的取证镜像，没有把候选原因写成修复。接口为Linux 6.18.60的bootconfig/启动追踪、kprobe事件及MSM/DPU tracepoints；标准tracefs只读消费者导出，rescue命令仅为TD-006诊断入口，Debian产品不依赖该命令。没有新ABI/binding，也没有修改DCS表、DSC、复位顺序或供电参数。

`MISEED_DISPLAY_TRACE=1` 仅允许与原生profile一起使用；默认0不改配置或ramdisk。`tools/display_boot_trace.py` 配置FTRACE/KPROBES/KPROBE_EVENTS/BOOTTIME_TRACING/BOOT_CONFIG/FORCE，检查最终符号及tracepoint链接，用固定Linux自身的bootconfig工具校验并追加配置，核对原压缩cpio未改动。所有新增输入和测试纳入来源锁。

探针包含mdss_probe入/返回、通用reset assert/deassert及ARM64 x30调用来源、面板prepare入/返回、0x11/0x29/0x51命令；现有MSM/DPU事件记录flush、启动和tx-done。原msm_mdss_reset已内联，不能按不存在的符号探测。固定 `trace_boot_init()` 为core_initcall_sync，早于显示device_initcall；实际注册成功仍须L3检查，不以符号存在代替。

最初tp_printk候选已编译，但复核发现逐帧事件进入屏幕日志可能反馈触发重绘，未上机，最终移除。最终仅保留内存追踪，新增 `display-trace`：只读挂载tracefs，读取非消耗性trace视图、时钟、启用状态、探针定义及各CPU丢失统计；不读trace_pipe、不写控制。TRACE_COMPLETE只证明读取完成，事件有效性另查。读出与统计并非整体原子快照；启动阶段事件需完整保留。

### 验证与载荷

- 宿主macOS arm64/zsh、分支与基线如前。最终构建命令沿用上述固定容器/source/build缓存，输出改为 `.work/display-trace-final-l2-native-20261115`，另设 `MISEED_DISPLAY_TRACE=1`；退出0，无编译/时钟警告。内核/DTB/initramfs均与boot内字节一致，ARM64 rescue包含新命令，DTB与上一最终版一致，锁及全部本地输入对应当前字节。
- Linux ARM64完整270项、12.093秒、零跳过，退出0；包含缺配置、内联/缺失符号、未链接事件、坏bootconfig校验和/长度、缺采集文件/CPU统计及重复读取不改文件反例。复用未变化的DTB及既有schema，没有新DTS/binding改动，不声称重新生成schema。
- 默认关闭追踪的真实CLI配置/封装路径经临时目录验证，不改既有config/ramdisk；最终追加bootconfig由内核同源工具解析通过，独立解包再生成cpio后逐字节一致。Mac专项6项、ShellCheck error级及仓库/diff检查通过。首次仓库检查发现TECH_DEBT超预算，已压缩既有重复说明后复验；构建脚本302行黄灯登记TD-008。
- 完整记录、打包核验、无设备预检与解析器合成正反例在 `.work/display-trace-20261115/`。取证解析器拒绝缺事件、重复/缺帧、追踪注册错误、非MDSS复位调用者、未观测0x29及CPU丢失统计异常；合成通过不代表真机日志已采到。

| 最终产物 | SHA-256 |
| --- | --- |
| boot.img | `9530bc6514f0a735ad9a8a5bcc9ccf4cca655913d97af7f8654ce2e0c33e4c82` |
| config | `0b2e54ef9143b68f700f21912eb4f5b78003c2974e86304895a0e6e462b6b8c7` |
| 来源锁 | `b3bef6b7e197b760c1d80e3876548f11e5827160e7edb9df895c48e702007150` |
| initramfs含bootconfig | `2b37a0b785fb4873cb6d210c96df0c80f24fd9f73fab84fa04a4f53f556a4f8d` |

一次RAM启动、基线只读采集、一次既有标准1500请求作为录像锚点、回Fastboot的脚本及操作/停止/回退方案已备，见本地 `.work/display-trace-20261115/L3-plan.md`。材料已完成离线核验，尚未通知接机或执行新设备操作；下一步须维护者本轮明确授权。录像同步误差跨阶段边界时不硬判原因，追踪改变现象时也不把它当作修复。启动无白/彩条仍未通过，探针真机注册、根因和独立专项复核仍开放；未提交或推送。

## 维护者纠正时间范围后的早期复核

维护者明确花屏在可见日志之前，观测归[硬件记录](../hardware/2026-11-15-umi-native-display-framebuffer-l3.md#维护者补充观测先花屏后出现可见日志)。暂停推进前述RAM取证方案，不把后段诊断功能、后采像素或1500点亮当作该故障修复；这里仅停止该方案执行，显示故障定位继续。

再读固定Linux源码：`really_probe()`在驱动probe之前执行bus dma_configure；SM8250的MDSS父节点本身带iommus。必须把驱动probe前的资源交接纳入范围，不能只从本地panel prepare向后看。但Qualcomm SMMU已有 `qcom_smmu_def_domain_type()`，其匹配表明确包含 `qcom,sm8250-mdss`，命中返回IDENTITY；`qcom_smmu_cfg_probe()`还读取有效SMR并设置对应BYPASS软件状态。因此日志里的全局Default domain type: Translated不能证明MDSS当时已切入翻译域。

同样，`preserved 0 boot mappings`来自 `arm_smmu_rmr_install_bypass_smr()`的IORT RMR条目计数，不是Qualcomm原SMR状态的统计；不能据此宣称显示固件映射丢失。上述都是固定源码事实，当前没有新增硬件状态读回或更早的目视同步证据。早期调用关系仍待核对固件扫描终止与面板可见状态，不直接改SMMU/bypass或删除必需的MDSS reset，也不增加新的诊断功能。

## 同类案例检索：启动先花屏后正常日志

2026-11-15按维护者要求检索上游邮件、QTI文档及同机项目。仅研究和文档更新，没有新增诊断功能、驱动改动或设备操作。下表分清作者报告、修复机制和对本机的适用边界。

| 来源 / 日期 | 原始报告与机制 | 对本机的意义 |
| --- | --- | --- |
| [同机umi-mainline TESTLOG §9](https://github.com/colorgarden/umi-mainline/blob/e38cb76b41c9d33bf5145732381dbf55f5f7cf1b/docs/TESTLOG.md)，2026-10-24 | 作者记录CSOT DSC面板“先花一下（ABL cont-splash → modeset 切换），然后正常打印内核日志”；该轮59包初始化成功，没有underrun/DSI timeout/ESD/abort，且跳过了GPIO12面板复位 | 与维护者反馈高度相似，优先查ABL扫描到原生modeset的切换；这是对方观测及归因，没有无花屏修复或单变量因果闭合。其6.12路径与本机6.18不同，不能直接照搬结论 |
| [Qualcomm DPU初始化复位补丁](https://lkml.iu.edu/2204.2/06017.html)，2022-04-21 | Bjorn Andersson说明bootloader已启动显示，Linux若只重配部分DPU，可能留下两条数据通路同时推送接口，产生图像伪影；以整体MDSS reset恢复已知状态 | 是固件残留配置造成早期花屏的直接上游机制；本机已有该复位代码和DT引用，不能把重新加入同一补丁当作新修复 |
| [Sunxi display white flash](https://lists.infradead.org/pipermail/linux-arm-kernel/2018-January/555459.html)，2018-01-22 | 报告U-Boot已初始化显示并开启背光，Linux bind中复位TCON引起白闪。维护者指出保留画面需要恢复完整DRM硬件状态，不能只省掉复位 | 与“控制器重配时面板仍可见”这一候选相符；不同LCD硬件，只能类比交接顺序，不能据此删除本机MDSS reset |
| [Veyron Chromebook白闪讨论](https://lkml.rescloud.iu.edu/1511.1/01769.html)，2015-11-10 | 调整PWM背光probe减少开机白屏，但仍有短白闪；Thierry Reding说明有些面板需有效帧后才enable，部分还需若干帧 | 区分“准备面板”“送有效帧”“允许显示”；不能把PWM/LCD补丁、等待帧数直接用于本机AMOLED |

同机项目文件使用GitHub连接器读取，并用完整提交 `e38cb76b41c9d33bf5145732381dbf55f5f7cf1b` 重读核对正文一致。这里保留作者观测，不采纳其“幂等”“PLL告警无害”等未经本项目验证的延伸判断，也不导入repower私有接口。

另核实两条相关依据：

- [SM8250 MDSS_CORE原补丁](https://lkml.iu.edu/2509.3/04981.html)由Luca Weiss于2025-09-30提出，后续有[RB5 Tested-by](https://lists.openwall.net/linux-kernel/2025/09/30/1233)，并以 `8b4faf419d601cead68d7821618369e2c0338b86` [合入](https://lists.openwall.net/linux-kernel/2025/10/27/1991)。这支持本机保留已有reset，不构成本机启动无花屏证据。
- [QTI下游面板binding](https://android.googlesource.com/kernel/msm/+/refs/heads/android-msm-coral-4.14-android12-qpr1/Documentation/devicetree/bindings/drm/msm/mdss-dsi-panel.txt)定义 `delay_until_first_frame` 延迟亮度更新，以及图像显示后发送的post-panel-on命令。它证明下游显式区分首帧与点亮时机，不证明Umi必须采用某种新时序；不将这些私有DT属性放入本项目主线binding。

截至本次检索，没有找到该CSOT变体“同样的启动瞬态已由某个公开补丁消除”的闭合记录。同机证据反而表明，跳过面板复位也可能先花屏；不能把本机三段面板复位单独认定为全部原因。

本机优先假设仍是：固件继续显示旧画面时，Linux改变控制器/链路状态，直到有效原生帧到达前，面板暴露了无效或残留内容。第二候选为display-on与有效首帧顺序。两者均是依据案例及本地调用关系提出的假设；可见日志之前不等同于Linux/面板驱动执行之前。后续应核对原有扫描停止、面板显隐与有效帧的顺序，不增加rescue功能，不猜改电气参数或延时。

## 实施前审查：MDSS probe 前保持面板复位的候选

维护者要求借鉴案例继续修复本机；本轮实施一个针对早期交接的单变量候选，不继续添加rescue功能。固定内核核对后，直接给TLMM自身加默认状态不采用：该驱动在注册pinctrl之后才添加function表，自己启动时的hog状态不能当作可靠的早期消费者。也不使用占住GPIO而无法交给面板的gpio-hog。

选用现有标准 `PINCTRL_STATE_INIT`：仅原生板级profile在MDSS父设备声明init状态，将已知GPIO12配置为gpio输出物理低；default为空状态。驱动核心在 `pinctrl_bind_pins()` 中选择init，然后才配置DMA并调用MDSS probe/reset。MDSS probe完成时选择空default，退出临时mux归属而不写电平；GPIO12的逻辑复位消费者仍为面板 `reset-gpios`，面板保留GPIOD_ASIS及既有高1ms/低1ms/高10ms初始化。没有往MDSS添加reset-gpios，也不修改MDSS驱动获取面板GPIO。

功能→接口：接管前面板保持已知复位态→主线pinctrl init/default + gpiolib→通用pinctrl属性及既有qcom,sm8250-pinctrl binding→驱动核心自动消费，之后标准DRM/KMS/backlight；无新UAPI/binding、无用户态重发依赖。固定TLMM `MSM_GPIO_PIN_FUNCTION(gpio)` 与 `function_is_gpio` 允许gpio模式mux和后续GPIO请求共存；这里必须按当前6.18源码核对，不能沿用旧内核strict互斥的假设。标准init语义见[主线文档](https://docs.kernel.org/6.18/driver-api/pin-control.html)。通用机制无需本地框架补丁；板级排序仍需WP2独立专项复核后才能形成上游提交。

这是针对“MDSS/DSI重配时面板仍可见”的候选修补，不证明根因，也不能覆盖早于MDSS init状态选择的ABL/SMMU阶段。它不是先前失败的原生亮度0策略：生效点在MDSS probe前，面板prepare尚未开始；保留MDSS_CORE复位、原始DCS/DSC、亮度初值1、供电/驱动强度/上下拉和所有有线/无线配置。失败时保留黑屏而不是继续依赖固件画面；USB/Fastboot回退须新L3验证。不得据L2声称已消除花屏。

### 早期复位候选实施与离线结果

已仅在 `kernel/display/umi-native-dsi.dtsi` 加入上述GPIO12 init状态及MDSS init/空default引用。`isolate_abl_targets.py`新增最终FDT gate，要求正确provider/面板身份、物理低、仅GPIO12、active-low面板GPIO与空default，拒绝新增上下拉/驱动强度等非预定属性；默认profile不引用该片段。没有修改panel C、DCS表、MDSS/DSI/SMMU代码或任何充电配置。

- 源码修改后已输出🚦 Audit，再做完整验证。Linux `test_display_handoff.py` 5项通过：包含7类错误状态（错误脚、错误电平、额外pull、错序状态、default再次复位、反极性及非GPIO复用）和缺引用反例，保留原MDSS错误恢复回归。Mac原生面板6项通过。
- 从固定内核提取真实 `pin_request()`，用合成descriptor验证MDSS尚保有gpio模式mux时面板请求成功；非GPIO函数及不识别GPIO类别仍拒绝。驱动核心init先于DMA/probe、probe完成选择default，以及TLMM GPIO类别逐项源码核对通过。此L1不验证锁并发或物理电平，记录在 `.work/display-panel-reset-20261115/pinctrl-evidence.json`。
- 使用固定容器/GCC13.3.0/Linux缓存，沿本节前的命令，输出改为 `.work/display-panel-reset-l2-native-20261115`、`MISEED_NATIVE_DSI=1`、`MISEED_DISPLAY_TRACE=0`；完整ARM64 L2退出0，没有编译/时钟警告。
- 固定dtschema2025.8环境运行 `tools/validate_kernel_dt.py --source /build/display-debug-source-20261115 --build /build/display-debug-build-20261115 --dtb /workspace/.work/display-panel-reset-l2-native-20261115/umi-csot.dtb`：定向binding、板级CHECK_DTBS、最终DTB及4项DT反例退出0、零跳过。MDSS通用pinctrl属性和TLMM节点均在该schema范围内，没有新增binding。
- Linux ARM64完整271项、11.848秒、零跳过，退出0。最终boot的内核/DTB/initramfs逐字节对应产物；配置和内核二进制与上一非追踪最终版完全相同，原DCS/面板C字节相同，追踪关闭，来源锁及全部输入一致。rescue含前轮既有诊断改动，未将整包宣称为与最早L3镜像严格单变量。
- 材料及一次RAM/只读/条件1500/回Fastboot计划、独占启动标记脚本和无设备预检在 `.work/display-panel-reset-20261115/`；未接机或启动。此轮是早期复位候选，花屏消除和实际GPIO接管仍待新L3；若失败，保留失败事实，不能以稳定出字代替启动验收。

| 候选产物 | SHA-256 |
| --- | --- |
| boot.img | `2e6f8442f54cfa8822226754e20f1e14c19b7b0286d24984ba15c3113ec08b3f` |
| DTB | `a3159f9c3693f82064e20f498cd39c485d28539c761be89bda45abc4ed90fc93` |
| config | `5f05b01bc4dde7d9d66b91cd7d8345234cd7dade26dbfd9bfd56f5f3e054f211` |
| 来源锁 | `92aef9cae6c7c861b49ac872992b0fc324c693479eb7f77a55fbb95667893714` |

源码单元DTS56行、隔离门禁142行、显示交接测试201行，均在绿灯范围；既有黄灯沿TD-008。仓库/diff检查通过，未提交、推送或合并；启动无花屏、独立专项复核和精确HEAD CI仍未关闭。

### 第五次L3失败后的撤回

维护者授权的一次候选RAM启动已执行，明确仍先白/彩条后正常日志；完整现场、运行DT和Android回退统一见[第五次L3](../hardware/2026-11-15-umi-native-display-framebuffer-l3.md#第五次授权-ram早期面板复位仍花屏回-android)。没有提高到1500或追加启动。最新用户约束为调试结束回Android，已更新AI_RULES，旧计划Fastboot收尾不再执行；本次ADB确认Android16/SDK36、boot_completed=1。

候选最终FDT配置保留、无新增引脚申请/显示链路错误，但目视仍失败；这只否定当前方案已消除花屏，不能单凭FDT或日志定位花屏的物理时刻。已校验当前输入仍与候选锁一致，再保存候选输入副本；只将DTS、isolate_abl_targets及test_display_handoff三份输入恢复到此前版本，并同步来源锁。原内核与配置、DCS表、亮度1及先前已验证的fbdev修复保留；启动追踪关闭。没有为了“保留修复”留下失败的早期复位状态。

撤回后完整原生ARM64 L2退出0；定向binding/板级/最终DTB schema及4项DT反例通过，Linux完整270项零跳过、退出0。打包与输入逐字节核验通过，恢复后DTB/config/内核与此前非追踪最终版一致；rescue仍为前轮诊断版本。新基线boot.img为 `9a476b45e962f00dea72269e793bb44ebb1280564edd5469c4c90f79492d598c`，来源锁回到 `b3bef6b7e197b760c1d80e3876548f11e5827160e7edb9df895c48e702007150`；产物在 `.work/display-panel-reset-restored-l2-native-20261115/`。该恢复镜像未再上机，不复用失败候选L3宣称它已验证。文档预算/来源锁/链接与diff检查通过。手机已正常回Android，未提交或推送。

## 最终收尾复核与延期（2026-11-15）

维护者要求读取[安卓Linux显示闪屏排查](chatgpt-conversation://6af97d80-2ebc-83ea-b816-5508634b5789)，若没有可靠修法则收尾并推送PR6；明确接受启动瞬态不阻断本次PR，不再追加真机试验。该对话已读取，提出DSI/PHY重初始化、DPU首帧/FIFO、panel display-on与首帧排序、缓冲布局等候选，没有本机原始日志或确定补丁。它是排查线索，不是已验证结论；本机为command-mode DSC AMOLED，不能直接套用video-mode背光延后或先等待vblank的建议。

维护者补充：相对上次提交，可能从仅顶部一行瞬时白条变为另有中间横条，随后正常日志。本轮按**可能退化**保留，不用“很快消失”判定修复，也不能仅凭位置或时间相近归因某个补丁。此前延迟fbcon试验明确出现过中部彩条，但最新反馈不足以证明只发生在那个候选。

与收尾基线`f349fec9`逐字节比较：panel驱动、59条DCS命令表、原生显示DTS、ABL隔离工具一致。保留的显示变化是上游MSM fbdev的FBINFO_VIRTFB回移（已有告警消失L3）、FB_DEVICE及显式只读缩略图命令、正常控制台配置门禁和probe默认值测试；它们不改DSI/PHY、面板复位、DSC或首帧时序。缩略图仅在USB命令调用后采集，不能解释日志前瞬态。未获得能把新增中线确定归因到上述差异的证据，因此不继续猜改硬件参数或时序。

收尾撤除未上机的可选bootconfig/ftrace/kprobe实现、tracefs导出及其测试/来源锁输入；离线材料留本地忽略目录，前文追踪章节仅作历史。延迟fbcon、初始亮度0、早期GPIO复位候选均不进入最终源码。保留此前有线固定PD输入监测/旁路故障锁修复和对应证据，无线RX/TX未改。最终镜像仍须区分离线验证与L3，本轮不再操作设备；手机此前已确认回Android16。

PR6已进入收尾，远端基线为138文件/9259新增行；本次仅验收缺陷修复、诊断取证和必要记录，不加功能。PR已有维护者规模豁免记录，本次按其明确要求更新PR6，不据此声称合并/发布、供电保护或新HEAD审查已通过。最后离线验证及精确推送SHA在下文/PR更新时记录。

最终离线验证：ARM64 Linux6.18.60 L2退出0；定向binding/板级/最终DTB schema及4项反例通过；Linux全量265项、11.871秒、零跳过通过。宿主ShellCheck(error)与仓库/差异检查通过（容器未装ShellCheck，改用宿主完成）。打包/来源锁/输入核验通过，最终boot与前文非追踪恢复版逐字节一致：boot `ef803cc853769aabcd000ae480654c93c5e6b4a258a7d9eedbe45df60c03177f`，DTB `5329ec0c6a92abf710ed06173a0792c05ef075adf6a7ced019b79c1cb69aa1b3`，来源锁 `5b51ce5c2e1b8ea4f2cc5f907064d30812db79348db186572278c3c3c1667ae8`。最终版未追加L3。产物及原始日志仅留`.work/display-closeout-20261115/`与对应L2目录，不提交。

维护者再次要求确认工作区充电修改保留：以收尾前恢复构建的来源锁核对，固定PD驱动与策略两个源码输入的SHA-256均一致，没有撤回或重写；相关测试差异均保留且本次全量回归通过，充电修复随本次提交进入PR6。
