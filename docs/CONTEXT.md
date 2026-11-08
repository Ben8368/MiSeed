# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge` / [草稿 PR #3](https://github.com/Ben8368/MiSeed/pull/3)。LOS 六路温控已 L1/L2；本轮按用户要求将 BQ watchdog 与 LOS 对齐为关闭，删除周期刷新，Linux 95/95、macOS 79 通过/16 跳过、ARM64 L2 成功；新 boot `97a5b9f9…e26498` 尚未上机，CI 另查。见[对齐与真机记录](archive/reviews/2026-11-08-los-watchdog-alignment.md)。

用户已授权 RAM 启动：旧 boot `6789b4e7…c9a882` 已在本机启动 Linux 6.18.60，ACM、PDC/SPMI/PON、只读遥测正常，导出 734 条连续日志。手机仍在该 RAM 镜像、充电抑制；未做插拔、回退往返或 PPS L3。29 个参考 DTBO 隔离检查不替代本机全部拓扑验收。Type-C/I2C15/泵/VBUS 仍禁用，GPIO 仅开放34/35。

本机泵 ID `0x10`、0x2b=`0xd0`（2 mΩ 配置，非实物测量）；LOS 动作映射/FCC 已查清。TS 阈值在源码中存在但片内保护关闭，不能统称资料不存在；不再以新增 watchdog 的刷新/到期证明阻断 LOS 复用。绿联45W屏显充电器 APDO 未测。身份见[平台](platforms.md)。

## 近期优先级

1. 核对原 LOS 的板级有效配置和温控/限流完整依赖，避免把原系统未启用的额外机制设为必备条件；RISK-004 保留实际未验证范围。
2. 继续主线温度标度、PD/泵/主充协调与 GPIO8/68、PMIC/TLMM 验证；新镜像回退往返未测，RISK-010 / TD-002 未关闭。
3. 用户仍要求 PD/PPS 快充落地及插拔取日志；当前仅源码/编译与上述诊断启动，不能宣布快充已启用。
