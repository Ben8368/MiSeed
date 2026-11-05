# 当前状态

确认日期：2026-11-05。阶段：WP2 首个 Umi/CSOT Android v2 `boot.img` 本机 L2 构建并通过段回读；Hosted CI 首次因 30 分钟上限取消，构建已切为最小板级配置，等待 PR 同 HEAD 复核；哈希见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。维护者尚未 RAM 启动，L2 不代表设备已启动。apt 传递包哈希和 runner digest 未闭合。
分支 `codex/board-l2-closure` 面向 `main` 的 PR #1 已创建并推送，当前 CI 修复待提交。设备/许可调查见 [WP1](archive/research/2026-11-05-wp1-input-review.md)；身份/NV 禁访、BL 重锁禁止。分支保护延期到项目跑通且仓库公开后再设置核验。

## 近期优先级

1. 取得 PR 当前 HEAD 的 GitHub CI 全部最终结果；然后维护者用已归档哈希的 `boot.img` 自行进行 RAM 启动测试并回报结果，后续闭合 apt 传递包哈希/runner digest并独立复建。
2. 保持能力声明按 L0/L1、L2、L3 分级；不把 host 端产物或上游实机报告写成 MiSeed 已启动，且在维护者完成 RAM 测试前不扩大设备能力声明。
3. 分支保护按维护者决定延期到项目跑通且仓库公开后；本机 panel ID/触控仅在相应驱动能力声明或 L3 前核验。RAM 测试由维护者执行；设备写入、身份/NV 访问仍禁止，BL 重锁绝对禁止。

## 最近验证

[首个 Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)；此前结果见 [WP1 输入调查验证](archive/reviews/2026-11-05-wp1-input-review.md) 与 [新远端导入验证](archive/reviews/2026-11-05-miseed-import.md)。
