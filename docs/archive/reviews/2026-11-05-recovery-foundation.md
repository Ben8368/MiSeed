# Recovery 与只读采集基础验证

日期：2026-11-05。环境：Windows 10.0.26200（OS 报告值）、OS/进程 X64、PowerShell Core 7.6.5、Python 3.14.2、Git 2.53.0.windows.2。工作目录 `C:\UmiSeed` 无 .git；未创建提交/远端/PR，不能提供 HEAD 或远端结果。

## 范围与审查

- 补读引用对话第五个用户回合，保留[需求补充记录](../research/2026-11-05-recovery-followup.md)。旧 fork 的功能与版本声明没有转化成本项目证据或来源锁。
- 产品/架构明确共享内核、独立 Recovery initramfs 和 C/Rust/Python/Shell 分工；新增 Recovery 行为契约与 ADR-0004，更新工作包与人类导航。
- 新增 `tools/collect_device.py`：指定 adb 设备、固定只读命令、逐项错误状态、未解决项，输出限于忽略的 local/ 并拒绝覆盖；不采集原厂分区内容、不提权、不启动/写入设备。
- 新增 8 项离线测试，通过模拟 subprocess 验证正常采集、错误设备/状态、缺失属性、权限失败、超时、缺少 adb、非法命令/选择参数、输出边界/覆盖与退出码。实际未执行 adb。
- 源码改后、完整验证前已输出 🚦 Audit Report。自查命令白名单、无宿主 shell 拼接、错误输出不泄露设备选择参数、设备自述与硬件能力分离；后续补拒绝控制字符并重跑相关回归。
- 未改 CI、原有来源候选、许可证或硬件能力矩阵；未导入外部源码，未把未来构建/刷写 CLI 写成可运行接口。

## 已运行命令

以下均在 `C:\UmiSeed` 执行：

| 命令 | 退出码 | 结果与证据范围 |
| --- | ---: | --- |
| `python -X utf8 tools/check_repo.py` | 0 | L0：编码、LF、空白、链接、预算、ADR、候选 schema 通过 |
| `python -X utf8 -m unittest discover -s tests -v` | 0 | L1：完整 27 项通过，包括 8 项采集器回归；Git 测试只使用临时夹具仓库 |
| `python -X utf8 tools/collect_device.py --help` | 0 | CLI 真实入口可解析；没有连接手机 |
| `python -X utf8 -m unittest discover -s tests -p test_collect_device.py -v` | 0 | 参数控制字符修正后，相关 8 项回归通过 |
| `git -c core.autocrlf=false diff --no-index --check -- <before> <after>` | 1 | 外部快照间存在改动，stdout/stderr 均为空，无空白诊断；保留实际退出码，不冒充普通仓库检查 |

差异检查的路径分别为 `<TEMP_DIR>\before` 与 `<TEMP_DIR>\after`。before 在改动前保存；after 排除 .git/.work/out/local/.venv/__pycache__ 后从当前目录复制。仅本次命令临时设置 core.autocrlf=false，没有修改 Git 配置；仓库检查另验证实际文件 UTF-8/LF。

## 未覆盖与边界

没有在本机执行真实采集，也未在 Linux 或 Python 3.11 上运行本轮代码；托管后的既有 CI 矩阵可验证这些工具环境，不能倒填为已通过。
采集器只记录 Android 返回的元数据；字段完整不代表来源可信、固件哈希/面板/触控已核实、能临时启动或可恢复。部分失败输出依然保留未解决项，不能升级成本机兼容性结论。
Recovery 运行时、包验证/签名信任根、安装与断电恢复均未实现，没有 L2 编译或 L3 启动/写入/保护证据。现有 RISK-001 至 RISK-007 与 TD-001 不因本轮工具通过而关闭；未来高风险实现仍须专项复核。
只读工具输出为本地资料，公开前需人工脱敏；输出文件名也不应含设备序列号。自有许可证未决，仍由维护者处理 RISK-005。
