# Fastboot 空枚举：主机失败点与连接流程修复

日期：2026-11-16；宿主 Darwin 25.6.0 arm64 / zsh；分支 `codex/fixed-pd-sampling`，基线 `e875b24cd1955db7a5e0b3d64d63ac8ba0fa322c`。维护者要求排查脚本、工具或流程导致的反复识别失败。初始主机诊断未 RAM 启动、刷写、修改 USB 配置/供电或执行有线 PD 充电；后续 PD 验收准备会话另有一次 RAM 启动，详见“后续主机权限对照”。无线 RX/TX 未操作。

## 实测与判断

| 检查 | 结果 | 能支持的结论 |
| --- | --- | --- |
| 直接运行锁定的 Google Fastboot 37.0.2，绕过启动脚本 | arm64、Rosetta x86_64 均退出 0、0 台 | 空结果独立于启动脚本的行解析；不能据此排除所有工具兼容性问题 |
| 主机权限下 `ioreg -a -l -r -c IOUSBHostDevice` | Android，VID/PID `18d1:d00d`，480 Mb/s；无接口节点 | USB 设备节点存在，Fastboot 所需接口尚未发布；不把注册表节点当作当前协议通信成功 |
| IOKit 旧 USB 类匹配、创建插件、QueryInterface | 各成功，匹配目标 1 台 | Fastboot 使用的旧 IOKit 发现与插件路径可用 |
| `GetConfiguration` | `0xe00002ed` / device not responding | 配置控制请求失败；尚不能锁定线、端口、macOS 或 bootloader 中哪一项造成 |
| `GetConfigurationDescriptorPtr` | `0xe00002d8` / not ready | 配置描述符未就绪 |
| `CreateInterfaceIterator` | 成功，0 个接口 | 无可供 Fastboot 枚举/打开的接口；重复空扫未改变此状态 |
| 注册表客户端 | Google Chrome、ChatGPT | 只证明客户端存在，未证明独占或冲突；未终止进程 |
| `sudo -n fastboot devices -l` | 需密码，未运行 root 枚举 | root 对照未完成，不能写成通过；未索取密码 |

[Apple GetConfiguration 文档](https://developer.apple.com/documentation/kernel/iousbdevice/1810333-getconfiguration)说明其会向物理设备发送控制读取请求。[AOSP macOS Fastboot 实现](https://android.googlesource.com/platform/system/core/+/android16-release/fastboot/usb_osx.cpp)通过 USB 接口迭代器查找 class/subclass/protocol `ff/42/03`。本轮证据将失败点缩小到 USB 配置/接口就绪之前，未确定更底层诱因，也未证明物理重插可以恢复。

## 流程缺陷与修复

- 先前以 `fastboot devices` 空结果直接要求重插，随后重复扫描 23 次约 46.7 秒，没有新增分层证据；ARM/x86 工具对照和权限排查在维护者已接线后进行，拉长验收窗口。
- 初次使用的 `ioreg -a -p IOUSB` 未带 `-l`，缺少 VID/PID 属性不能解释为没有 USB 设备。改用完整属性与指定类检查。
- 新增 `tools/check_fastboot_connection.py`：快速路径仅调用一次 Fastboot；空结果在 macOS 再读一次 USB 树，默认每条命令限时 2 秒、无轮询，输出分层状态并隐藏标识。`ready_for_identity` 仅表示可开始核对身份。
- 当前忽略目录中的 `ram_boot.py` 已接入该检查，失败即保存脱敏报告并退出，不生成启动尝试；身份 getvar 限时从 15 秒减为 3 秒。镜像和 111 项构建输入未改，主机准备锁在验证后重绑当前差异。
- 接机前完成离线验证与工具准备。接机后一轮快速检查即可分流：可访问则核对身份并执行原定操作；不可访问则释放维护者和设备，不在接线期间反复准备或空扫。

## 证据与验证

只读 C 探针源码、二进制及精确输出保留在忽略目录 `.work/fastboot-host-diagnosis-20261116/`；不读取或输出序列号，不调用 open/seize、SetConfiguration、ResetDevice 或写入 API。以 `xcrun clang -Wall -Wextra -Werror -Wno-deprecated-declarations ... -framework IOKit -framework CoreFoundation` 编译，退出 0；主机权限下执行退出 0。此探针用于主机诊断，不是手机产品构建或 L3 充电证据。

新入口 12 项离线正反例全部通过、零跳过，覆盖单台快速路径、多台/畸形输出、无接口/有接口、权限失败、工具失败/缺失、超时与标识隔离；启动入口另有 2 项隔离检查通过：连接失败不生成启动尝试，哈希失败不访问设备；仓库检查通过（既有源码篇幅黄灯保留），`git diff --check` 通过。实际单次主机调用在后续未发现目标的状态下返回 `not_detected`、退出 6、耗时 0.021 秒；这只验证无目标分流与耗时，不表示此前无接口故障已恢复。后续若要定位更底层诱因，应在工具已备齐后做单变量恢复对照（连接路径或正常重进 bootloader），记录配置/接口状态变化；不先宣称某条线、Chrome 占用或 Fastboot 版本是根因。

## 后续主机权限对照

同日 PD L3 准备会话中，设备再次处于 Bootloader Fastboot。受限环境下同一检查器得到 `fastboot_count=0`、USB 可见性 `ioreg exit=1`、状态 `usb_visibility_unverified`；在获主机 USB 访问权限的环境重跑一次后，得到唯一 Fastboot 设备（`fastboot_count=1`、`ready_for_identity`）。随后只读身份为 `product=umi`、`unlocked=yes`、`is-userspace=no`。这证明该会话的沙箱空枚举不能作为未连接结论；Fastboot 枚举及 USB 可见性核对须在有主机权限的环境进行，状态不明时停止，不空扫。

维护者要求启动已准备的固定 PD 验收镜像。`fastboot boot` 对 9,500 KB 载荷返回 Sending/Booting `OKAY`、退出 0；该结果仅证明 Bootloader 接受载荷。随后未出现 `/dev/cu.usbmodem…`，Fastboot 已退出，USB 结构化输出不可解析，未取得 Umi/CSOT Linux 控制台 banner，也未开始 PD 充电验收；维护者断开设备充电。本次不据此判断内核启动失败或成功，不重试。镜像 SHA-256 为 `ceafb34bba3fc6e45920cc9cd84fa67e793c0aa00669fae9e7c33cefe7afaeaf`；身份和启动输出不含设备序列号。
