# 2026-11-05 文档与 CI 初始化验证

## 环境与范围

宿主 Windows 10.0.26200 x64，PowerShell 7.6.5，Python 3.14.2。工作目录 C:/UmiSeed 无 Git 元数据；未创建仓库、commit、远端或 PR。Git 临时测试仓库仅供回归夹具使用。

本轮重建需求、架构、设备与来源文档，增加治理/提交工具及 GitHub Actions 定义。未下载/构建产品源码，未连接手机。既有协作规则保留，Markdown 统一为 UTF-8/LF。

## 验证结果

| 命令 | 退出结果 | 覆盖范围 |
| --- | --- | --- |
| `python -B -X utf8 tools/check_repo.py` | 0，PASS | L0 文本/文件链接/预算/ADR/候选清单 |
| `python -B -X utf8 -m unittest discover -s tests -v` | 0，17 项通过 | L1 正反例，含临时 Git 范围、原生 trailer 解析与被强制跟踪的忽略文件 |
| `go run github.com/rhysd/actionlint/cmd/actionlint@v1.7.12 -color .github/workflows/ci.yml` | 0，无诊断 | workflow YAML、表达式与 action 输入静态检查 |
| `git -c core.autocrlf=false diff --no-index --check <原始快照> C:/UmiSeed` | 1，无诊断 | no-index 表示存在差异；无空白错误，另由 L0 检查所有文本 |

原始快照为修改前放在仓库外的 `umiseed-before-00c9a6cbfcda41d88618b64339a2708c`。Git 的 no-index 模式启用差异退出码，1 不等于通常仓库模式的 diff-check 0；因此保留实际退出值。Action 引用经上游 tag 查询核对：checkout v6.0.2 `de0fac2e4500dabe0009e67214ff5f5447ce83dd`，setup-python v6.1.0 `83679a892e2d95755f2dac6acb0bfd1e9ac5d548`。

检查脚本及回归无网络/设备访问；actionlint 的 Go 工具获取使用网络，测试临时 Git 提交不会写入工作目录历史。未运行 Shell 脚本；没有产品编译或硬件测试。

## 未覆盖与合并门禁

Linux/Windows 托管 runner、Python 3.11、GitHub 事件与分支规则尚未实跑；无 L2/L3。CI 与治理属高风险，尚需开发者以外人员专项复核及真实 PR 同 HEAD success，见 RISK-006。未选择项目许可证，不授权第三方代码/固件分发。
