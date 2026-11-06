# 2026-11-06 小米 10 原厂 boot 分区与载荷研究

范围：只读分析用户放在 `.work/` 的两个本地原厂包，作为 `umi` 项目参考。未连接设备、读取手机分区、运行任何刷机脚本或改写镜像。包内字段只证明这些本地文件的内容，不证明下载来源、签名链、当前手机所装版本或本机 GPT。

## 输入与身份

| 包 | 本地 SHA-256 | 内部身份 |
| --- | --- | --- |
| `umi_images_OS1.0.3.0.TJBCNXM_20240320.0000.00_13.0_cn_b616f9f481.tgz` | `ADC21DD1010B06DAB5951F9328854AAA8D4C60F11DFF7E300869429DA6ADD389` | Fastboot 包；`partition.xml` / `rawprogram*.xml` / GPT |
| `miui_UMI_OS1.0.7.0.TJBCNXM_2cc115c0ee_13.0.zip` | `1BFD996623DAB1396BBB1B643BB8E4C2D21E46E52B08F9727F4841551FB84128` | OTA metadata：`pre-device=umi`、`V816.0.7.0.TJBCNXM` |

哈希仅用于标识本机分析输入，未完成官方发布页或签名链核验。

## 分区表交叉核对

Fastboot 包 `partition.xml` 声明扇区大小 4096 字节，`boot` 为 131072 KiB。`rawprogram4.xml` 将它映射到 `physical_partition_number=4`、`start_sector=65536`、`num_partition_sectors=32768`；换算为 `32768 × 4096 = 134217728` 字节，即 **128 MiB**。同包 `gpt_main4.bin` 中 `boot` GPT 项的起止 LBA 为 65536–98303，大小同为 128 MiB。三处数据一致。包注释将分区组称作 LUN 4；这是包级布局，不是本机读取结果。

Fastboot 包还给出 `recovery` 128 MiB（LUN 0）和 `dtbo` 32 MiB（LUN 4）。该包布局及 OTA zip 项目均未列出独立 `init_boot` 或 `vendor_boot`。Fastboot 脚本确实包含 `erase boot` 与 `flash boot` 命令；这里只静态阅读，没有执行。

## OS 1.0.7 `boot.img` 内容

OTA 中 `boot.img` 文件长 134217728 字节，恰为整个分区容量，但不能据此认为内核占满 128 MiB。解析 Android boot header v2（4096-byte page、1660-byte header）所得字段：

| 段 | header 声明字节数 | 约合 MiB |
| --- | ---: | ---: |
| kernel | 54,837,264 | 52.30 |
| ramdisk | 1,330,716 | 1.27 |
| second | 0 | 0 |
| recovery DTBO | 0 | 0 |
| DTB | 1,613,904 | 1.54 |

Header 中 kernel / ramdisk / DTB load address 分别为 `0x8000`、`0x01000000`、`0x01f00000`。镜像尾部有 AVB footer（vbmeta size 896 字节）；footer 的 `original_image_size` 为 57,794,560 字节，之后仍有大段分区填充。AOSP v2 布局按 4096-byte page 放置 header 与每个非空段；`kernel_size` 是镜像段字节数，不等于内存展开后占用。

## 内核空间估算与 MiSeed 限制

若保留 OS 1.0.7 的 ramdisk、DTB、无 second / recovery DTBO，并为相同大小的 AVB vbmeta/footer 预留空间，则可放入 boot 镜像段的理论 kernel 上限为：

```text
floor((128 MiB - 4096 - align(1,330,716,4096)
       - align(1,613,904,4096) - 896 - 64) / 4096) × 4096
= 131,260,416 bytes = 125.18 MiB
```

这是**固定其余段和 AVB 元数据尺寸后的存储上限估算**，不是 ABL 可加载上限，也没有证明改写后的 AVB 描述符或签名有效。加载地址、内存保留区、kernel 解压/展开尺寸和 ABL 实际放置策略仍需独立证据。

当前 MiSeed L2 的 `boot.img` 为 2,453,504 字节，header 段为 kernel 2,030,895、ramdisk 252,977、DTB 163,320 字节。现有 [打包器](../../../tools/pack_android_boot_v2.py)另有固定地址间距保护：kernel 输入必须小于 `0x01000000 - 0x8000 = 16,744,448` 字节（约 15.97 MiB）。这是 MiSeed 当前保守打包约束，低于按分区容量估出的存储空间；不要把 125.18 MiB 当作当前安全可启动目标。接近该保护值前，应先核实本机 ABL、load map 与解压后内存布局，再决定是否调整。

依据：[AOSP boot image header](https://source.android.com/docs/core/architecture/bootloader/boot-image-header)、[AOSP DTB image layout](https://source.android.com/docs/core/architecture/bootloader/dtb-images)、[AOSP `unpack_bootimg.py`](https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/unpack_bootimg.py)。
