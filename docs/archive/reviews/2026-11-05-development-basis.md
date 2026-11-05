# 需求与开发依据文档校核

日期：2026-11-05。环境：Windows 10.0.26200（OS 报告值），OS/进程 X64，PowerShell Core 7.6.5。工作目录 `C:\UmiSeed` 没有 .git；不提供虚构的分支、HEAD、PR 或远端 CI 结果。

## 本轮范围

仅修改 Markdown 文档；工具、测试、来源候选、CI 与历史 ADR 均保持原样。

- PRODUCT 将引用对话五个用户回合映射到 RQ-01–RQ-07，明确用户要求与项目解释；对话助手给出的版本、短 SHA、硬件组合与 CLI 不自动进入配置。
- implementation-plan 明确 M1 集成启动、M2 完整 Linux MVP、M3 安装发行、M4 条件式 EDL；补充 WP1 的身份、回退、来源锁、构建与验证计划交付要求，关联需求编号。
- ARCHITECTURE 补全构建/打包/设备执行边界与产物清单字段；语言选型仍是项目设计，不虚构工具链版本或构建能力。
- PROVENANCE 要求功能级来源比较记录，关联需求/工作包及风险；既有 fork 必须核对完整身份和原验证范围，不继承成 MiSeed 验收。
- platforms 强调“最新版”不可作为固件身份，禁止照搬其他设备配置；未更新任何硬件能力结果。
- README 更新开发入口；GOVERNANCE 仅补充 PRODUCT 的需求追溯职责；CONTEXT 记录本轮阶段与证据入口，没有宣布 WP0 完成。

## 人工核对

需求来源沿用前轮读取的完整引用对话及两份需求归档；本轮不增加外部事实或重新宣称候选项目能力。原对话第 1 轮为设备/系统目标，第 2 轮为已有 fork，第 3 轮为命名与发行，第 4 轮为独立多来源集成，第 5 轮为 Recovery 与语言选型。
检查需求编号覆盖、里程碑范围、依赖一致性与事实归属：具体设备参数仍归 platforms，当前进度仍归 CONTEXT，接口仍归 ARCHITECTURE，运行行为仍归 RECOVERY，工作完成条件仍归 implementation-plan。
M1 不冒充包含 Debian 登录的完整 MVP；类似官方线刷包不冒充官方签名、MiFlash 兼容或 EDL 授权；本地只读调研不冒充托管门禁已完成。

## 验证

在 `C:\UmiSeed` 运行：

```text
python -X utf8 tools/check_repo.py
git -c core.autocrlf=false diff --no-index --check -- "<TEMP_DIR>\before" "<TEMP_DIR>\after"
```

第一项退出码 0，文本编码/LF、空白、链接、文档预算、ADR 和来源候选检查通过。第二项退出码 1，stdout/stderr 均为空，无空白错误诊断；no-index 快照间存在改动，保留实际返回值，不写成普通仓库 `git diff --check` 通过。
before 是修改前的外部原始快照，after 是本轮文档的外部快照；复制时排除 .git/.work/out/local/.venv/__pycache__。另以同路径 `git -c core.autocrlf=false diff --no-index --stat` 确认差异仅涉及 9 个 Markdown 文件，退出码 1。临时 Git 参数没有修改配置。

## 未覆盖

本轮仅验证文档（L0），不重跑未改动工具的 L1 回归，不产生 L2 编译或 L3 真机证据；未连接手机、写盘、修改供电或导入第三方内容。现有风险和技术债不因文档明确而关闭，许可证、来源锁、本机契约与回退方案仍须实际交付。
