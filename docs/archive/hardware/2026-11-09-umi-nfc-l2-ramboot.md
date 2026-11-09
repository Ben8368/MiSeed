# Umi NFC L2 / RAM boot 验证

日期：2026-11-09

设备：Xiaomi Mi 10 `umi`，原 Android 系统；Fastboot 报告 `unlocked=yes`、`is-userspace=no`。不记录设备序列号。

启动方式：Fastboot RAM boot；未执行 `flash`、分区写入、格式化或块设备写入。

## 构建

- Kernel：Linux 6.18.60，AArch64，GCC 13.3.0。
- 镜像：`.work/boot-l2-arm64/boot.img`，SHA-256 `d7861009286999af18e37ec9be27d5d7ad4ab921264a9f482726159cb97b6170`，7,815,168 字节。
- Umi DTB：`.work/boot-l2-arm64/umi-csot.dtb`，SHA-256 `896688420465aa8b4ebef182d77018fc02984c8dc5ca17d86b763fcbe528d2d0`。
- NQ-NCI 对象：`.work/boot-l2-arm64/nq-nci.o`，SHA-256 `fc19e07c9354cf46a51d5cc755da3cc1851280d4f47b9a71cd13cc13541f0c0e`。
- `tools/build_kernel_l2_macos.sh` 成功；构建门禁运行的 ABL overlay 回归用例 6 项通过，NQ-NCI 对象及设备树检查通过。

## 设备实测

Fastboot 身份核验通过，`fastboot boot` 对上述镜像返回 `Sending`、`Booting` 均 `OKAY`。Linux 诊断控制台确认：

- QUP wrapper `9c0000.geniqup` 绑定 `geni_se_qup`。
- NFC I2C 控制器 `984000.i2c` 绑定 `geni_i2c`；I2C adapter `i2c-1` 已注册。
- NFC 设备 `1-0028` 绑定 `nq-nci`，compatible 为 `qcom,nq-nci`。
- `/dev/nq-nci` 存在，mode `0600`。
- `help`/`status` 诊断控制台可用。未向 NFC HAL 发起读卡、HCE、eSE 或 OMAPI 事务。

完成验证后通过 Fastboot `reboot` 返回原 Android；ADB 唯一设备在线且 `sys.boot_completed=1`。因此验证未留下设备运行在 MiSeed Linux 的状态，也未改变持久分区。

## 结论边界

这次结果证明 NQ-NCI 内核驱动、Umi I2C/GPIO 设备树、设备节点与一次真机 RAM boot probe 可工作。驱动 probe 会经共用 VEN 对 NFCC 和 eSE 断电/上电，并执行 reset/version 检查。MiSeed 诊断 initramfs 尚无 Android NXP HAL、eSE middleware、Android NFC service/framework、标签配置或已审查授权的厂商固件；不据此宣称完整 Android NFC/eSE 用户体验。

本任务未修改有线充电或无线充电的驱动、设备树配置或电源参数。
