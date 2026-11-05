# 2026-11-05 文档精简验证

环境：Windows 10.0.26200 x64 / PowerShell 7.6.5 / Python 3.14.2，无 .git。
范围：双入口、职责去重、背景归档与活动文档预算；workflow 未改动。

## 结果

| 命令 | 结果 |
| --- | --- |
| `python -B -X utf8 tools/check_repo.py` | 0，PASS：文件、链接、预算、ADR 与来源清单 |
| `python -B -X utf8 -m unittest discover -s tests -v` | 0，19 项通过，含活动文档超限与归档豁免反例 |
| `git -c core.autocrlf=false diff --no-index --check <原始快照>/tree C:/UmiSeed` | 1，无空白诊断；no-index 差异退出码 |

原始快照：仓库外 `umiseed-docs-before-rrcc4o8p`。活动文档（根两入口 + docs 顶层）18 → 15 篇、21,587 → 13,323 字符，减少 38.3%；AI 开局 AGENTS + CONTEXT + LESSONS 为 2,079 → 1,507 字符，减少 27.5%。历史调研已归档，不计入日常加载。

## 未覆盖

预算检查不判断语义重复；已人工按 GOVERNANCE 职责表复核。无远端 CI、L2/L3 或开发者以外专项复核；本轮不提交、发布或合并。
