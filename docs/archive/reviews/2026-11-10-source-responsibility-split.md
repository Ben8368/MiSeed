# 源码职责拆分（2026-11-10）

基于 `8b6a9c2b54b1bd9c17d0efe0f153802a45e09912` 工作树，保留前轮未提交的篇幅门禁改动。宿主 Windows 10.0.26200 / x64、PowerShell 7.6.9；Linux 验证使用 WSL Ubuntu-24.04 / x86_64。源码与验证阶段未提交、推送、发布或执行设备操作；后续提交授权见末节。

## 范围与接口

| 能力 | 标准入口与消费者 | 本地差异 |
| --- | --- | --- |
| 电量计 | 固定 Linux power_supply；Debian 标准属性消费者待集成 | 仅拆开 FG 寄存器/SRAM、代际读数与核心注册，不改属性/单位 |
| 有线 SMB5 | power_supply；有线协调仍有已登记的内部互锁 | 硬件、标准属性、策略/互锁、生命周期独立编译；同一锁和所有权字段 |
| 有线 BQ/PPS | power_supply、Type-C/TCPM、thermal | 状态机、保护采样、标准属性、thermal、生命周期独立编译；保护参数和错误路径不变 |
| 救援入口 | 既有 bring-up 控制台与只读诊断；产品路径仍为 UEFI → Linux → Debian | 按文件/设备/USB/有线/NFC/日志职责拆分，记录器单实例并保留 fork 共享 memfd |

不引入 Android HAL/Binder、私有 ABI 或供电参数；未涉及无线 RX/TX。现有 TD-003–007 的接口/真机验收范围不变，既有有线 PD/PPS 稳定 L3 结论保持原证据范围。本次拆分须单独取得 L2，不将旧镜像证据迁移为新镜像验收。

## 构建与测试组织

- 原 `qcom_fg`、`qcom_pm8150b_charger`、`umi-bq25970` 模块名保持不变，Kbuild 组合实际 `.o`；没有 `.c` 相互 include 的产品构建。内部头仅承载共享类型、常量和跨文件声明。
- 救援程序由八个独立 C 单元链接，RAM 日志实现从头文件移到单一 `charge_log.c`；共享描述符显式声明，避免每个文件各自维护静态副本。
- 测试按移植接线、FG、SMB5 ABI、有线策略、救援控制台/日志分组；C 救援测试桩独立。合成测试仍编译原函数并拦截所有救援单元的系统调用，不运行 PID 1 或访问真实设备。
- `tools/source_layout.py` 统一移植清单和 Kbuild 组合；来源锁覆盖所有新单元、内部头与布局工具。保留上游提交、原路径/哈希、作者与许可证，派生路径和本地哈希另记；缓存核查仅允许对应文件及精确构建规则。

## 🚦 Audit Report

原 6 项篇幅红灯已消除，90 个源码/构建输入文件中最大 590 行；8 项黄灯归 TD-008。不通过缩短标识符、删除注释或任意编号切段绕过门禁。本轮 L0/L1、ARM64 L2 已完成；合并前仍需开发者以外专项复核，新镜像 L3 未授权/未执行。有线保护逻辑不因拆分而放宽。

## 验证

命令在 `C:\MiSeed` / WSL 的 `/mnt/c/MiSeed` 执行。Windows 为 Python 3.14.2，WSL 为 Python 3.12.3；ARM64 GCC 固定为 Ubuntu 13.3.0-6ubuntu2~24.04.1。

| 命令 | 退出码 | 结果 |
| --- | ---: | --- |
| `python -X utf8 tools/check_repo.py` | 0 | 无篇幅红灯、文本/文档预算/链接/来源锁错误；8 项黄灯均已登记 |
| `git diff --check` | 0 | 已跟踪改动无空白错误；新文件另核对 UTF-8/LF、尾部空白和末尾空行 |
| `python -X utf8 .work/compare_split_bodies.py` | 0 | 对比本地原始快照与拆分文件，144 个原函数无缺失且函数体逐字节不变；原文件可从基准 HEAD 重取 |
| Windows：`python -X utf8 -m unittest discover -s tests -v` | 0 | 179 项，120 通过、59 跳过；仅在进程 PATH 加载 w64devkit C 编译器 |
| Windows：`python -X utf8 -m unittest discover -s tests -p test_pps.py -v` | 0 | 最后一次 C 调整后 4 项 PPS 回归均通过 |
| WSL：`python3 -X utf8 -m unittest discover -s tests -v` | 0 | 最终 179 项，176 通过、3 个 DT/schema 专项因未提供指定 DTB/schema 输入而跳过 |
| WSL：`shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh` | 0 | ShellCheck error 级通过 |
| WSL：下述完整 L2 命令 | 0 | Image、DTB、modules、八单元静态救援程序、initramfs、boot.img 全部生成；13 个供电对象与关键链接符号均按脚本检查 |

完整 L2 命令（仅生成本机产物）：

```sh
export MISEED_JOBS=12 MISEED_WORK_ROOT=/tmp/miseed-boot-l2
export MISEED_EXTRACTED_SOURCE=/tmp/miseed-boot-l2/source-split-source-20261110
export MISEED_BUILD_ROOT=/tmp/miseed-boot-l2/source-split-build-20261110
export MISEED_OUTPUT_ROOT=/mnt/c/MiSeed/.work/source-split-l2
bash tools/build_kernel_l2.sh
```

首轮 L2 检出 PPS 属性描述符的结束符误分到 thermal 单元，已归回供电属性文件后重跑成功，描述符字段及函数体未改变。初轮仓库/回归还检出两个旧路径链接，已迁移；一次并行复测出现临时副本 `copytree` 目录已存在错误，最终复测通过。

构建期间观察到 WSL `make` 的 Clock skew 提示。保持脚本的 `KBUILD_BUILD_TIMESTAMP="2026-11-05 00:00:00 UTC"`、`KBUILD_BUILD_VERSION=1`、`KBUILD_BUILD_USER=MiSeed`、`KBUILD_BUILD_HOST=Ubuntu2404`，再次执行以下命令，退出 0，Image SHA-256 与完整构建一致。复核仍提示生成文件的时间短暂靠前；该宿主时钟局限未闭合，不隐去该提示。已核对全部新供电源文件与 staged 文件逐字节一致，13 个对象实际编译及链接检查成功：

```sh
make -C /tmp/miseed-boot-l2/source-split-source-20261110 \
  O=/tmp/miseed-boot-l2/source-split-build-20261110 \
  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j12 Image qcom/umi-csot.dtb modules
```

Windows 额外加载现有原生 DTC/libfdt 工具时，6 个 overlay 用例报 `FDT_ERR_BADSTRUCTURE` / blob 读取不足；未将该运行写为通过，未修改这些工具或跳过判定。最终原生运行不加载该工具目录，6 项在 WSL 实测通过；Windows 原生 DT 工具问题仍未闭合。

| 输入/产物 | SHA-256 |
| --- | --- |
| 来源锁 `manifests/sources.lock.json` | `33d60dcb0ea254eb338b52edc620fb9f16e9b294fc586332c0d30cb0534dfb23` |
| Image | `99bc5cbd5b09bc82eb565b301732a2cf8d5ccf388e98bb42cd1e69d57af48500` |
| 救援 ELF | `c6bf8de931a1705590ffba28e45e0716a1c9d72f4b872b75388f0816234eac15` |
| DTB | `0b85be50c6eb2df3a83e9d688d799fefb46ccf967b7ead79f6efe8d3b7abd8dc` |
| resolved.config | `08b70762778823dac5ffdbd0c72f7516c9d69c0dc0f18c4d4d7b93f4a3634030` |
| boot.img（9,617,408 bytes） | `aa5a46831df1373cd4cc0fafea7689af374ce273630a2c8d0451a4094bd1b7f7` |

原始日志与产物保留于忽略目录 `.work/source-split-*`，不提交；以上证据仅针对本地工作树，远端 CI、独立专项复核及新镜像 L3 尚未执行。

## PR #4 提交授权补记

2026-11-10，维护者要求将全部变更提交到 PR #4。预检远端原有 3,579 行新增，完整更新预计 9,543 行，超过 AI_RULES 的 7,000 行限制；维护者明确选择“允许本次 PR #4 例外超限，提交全部改动”。该授权仅适用于本次 PR #4 的篇幅例外及提交/推送，长期规则保持，未授权合并、发布或设备操作。最终新增行数与精确 HEAD 由提交后 PR 元数据复核。
