# 技术债

只记有意保留的实现妥协；未知假设归 RISK_REGISTER。关闭后按 GOVERNANCE 归档。

## TD-001：构建环境闭包不完整（P2，2026-11-05）

取舍：首个 Boot L2 固定 Linux、DTS、配置、header 模板及直接工具版本；Ubuntu runner 和 apt 传递依赖尚未逐包哈希锁定。

影响：首个 Umi/CSOT boot L2 本机结果见 [Boot L2 复核](archive/reviews/2026-11-05-first-board-l2.md)。真实 CI job 已接线但尚无同 HEAD 远端结果。直接 apt 包版本已钉住，传递包哈希和 runner 镜像 digest 未闭合；UEFI/rootfs 尚无构建 job，PR 门禁仍 UNVERIFIED。

偿还：闭合传递依赖与 runner digest、取得同 HEAD 远端 L2 结果、独立复建比对，再分别接入 UEFI/rootfs 构建和产物校验。
