> 历史补充：仅追溯需求，不是实现或硬件证据。

# Recovery 与语言选型需求补读

2026-11-05 通过 read_thread 读取引用对话全部五个用户回合及助手正文。此前[背景记录](2026-11-05-background.md)只覆盖四个用户回合，本记录补充最后的 Recovery 请求，不改写原记录。

用户明确询问增加 Recovery，并将平台概括为 UEFI、Linux 内核、Recovery 三部分，要求结合全项目选择开发语言。对话中的目录、版本、菜单和 CLI 都是建议，不是已经存在的实现。

新增设计采用共享内核、分离 initramfs 的 Recovery，语言分工与运行行为分别落实在 ARCHITECTURE 和 RECOVERY，长期理由见 [ADR-0004](../../ADR/0004-recovery-runtime.md)。没有按旧助手建议直接指定 fork 的短 SHA、推定 ST 触控或创建可刷入的安装包。

本次只补查官方机制文档：[Linux initramfs](https://www.kernel.org/doc/html/latest/filesystems/ramfs-rootfs-initramfs.html)、[Android adb](https://developer.android.com/tools/adb)、[ADB 手册](https://android.googlesource.com/platform/packages/modules/adb/+/refs/heads/main/docs/user/adb.1.md)。它们支持 initramfs 用户空间入口、指定序列号与 shell 查询接口，不证明本机兼容性。没有执行外部脚本、导入第三方源码或连接手机。
