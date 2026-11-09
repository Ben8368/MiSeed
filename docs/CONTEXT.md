# 当前状态

日期：2026-11-09。工作分支 `codex/umi-pd-pps-fastcharge`，草稿 PR #3。已 fetch 远端 HEAD 为 `49b1f406`，当前测试镜像是该基线加未提交修正。已修正 RUN 跨芯片 VBAT 与下向 VBUS 固定压差门控；启泵门控及绝对限值/温控/合约保护保留。镜像 `7da71525…980f619` 已通过 106 项回归、完整 L2 和 RAM/USB/短时充电 L3。见[本轮记录](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。

当前镜像 `0d0ab728…d4f3b5e` 已通过 Linux 116 项、Windows 原生 18 项回归及完整 L2，并在 RAM 启动。首轮充电在 PPS 电流请求时收到保留控制类型 0x16，软复位后启泵前以 -71 回退。重新插入充电器后，同一 boot_id 的完整 RAM 导出 1756 条、覆盖 0，TCPM 导出 4946 行、complete=1：一次发送失败/0x16 后重试成功，启泵约 82.3 秒、250 条 RUN 样本，电池净充电最高 3.263 A；末尾 Type-C 断开先于泵故障 -34，符合维护者拔线时序，不证明内部无瞬态故障。见[本轮记录](archive/hardware/2026-11-09-umi-pps-windows-l2.md)。短时 PPS 已实测，报文来源、长期稳定性及完整保护仍未验收。

## 近期优先级

1. 核对 PPS 电流请求时 0x16/发送失败的来源；不放宽协议校验或供电保护。
2. 验证更长时间 PPS、异常停止/恢复及保护；拔线事件与内部故障分列，先完整导出 RAM/TCPM 日志再重启。
3. 提交并更新草稿 PR #3，跟进精确 HEAD CI 与独立专项复核；不刷写、不合并。见 [PPS](../kernel/power/PPS.md)、[RISK-004](RISK_REGISTER.md#risk-004-p0-供电发布)。
