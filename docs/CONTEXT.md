# 当前状态

日期：2026-11-08。分支 `codex/umi-pd-pps-fastcharge`，基于 main `109225e`；工作进入[草稿 PR #3](https://github.com/Ben8368/MiSeed/pull/3)，未合并。Lineage 来源固定 `71b13e62f057a649b77fe4062feb73ee72ad609c`。新增官方源码对照、DTBO 隔离、UART-only GPIO、PD PHY 接线和补采，见[续查](archive/research/2026-11-08-pps-topology-followup.md)。

WSL 95 项回归全过；Windows 51 通过、44 跳过；ARM64 L2 通过。最终 boot SHA-256 `b2068513…1770f0`；29 个参考 DTBO 独立叠加后主线 SPMI/TLMM 未变。TLMM 源码仅开放 GPIO34/35；PPS、Type-C/I2C15、VBUS 仍禁用，板级参数缺失拒绝 probe。**没有本次 PMIC/TLMM 或 PPS 新 L3，不可声称快充可用。** CI 按精确 PR HEAD 核验。

已采身份和剩余缺项见[平台](platforms.md)，不重复索取。部分 Mac 原始日志未迁移。本轮未连接手机或修改供电；旧受限充电 L3 不覆盖快充或新拓扑。

## 近期优先级

1. 独立复核 DTBO 隔离及 UART-only TLMM；另行授权后验证当前镜像的启动、唯一 SPMI/PON、USB 往返，RISK-010 / TD-002 未关闭。
2. 优先复用 Android 既有日志与匹配板级资料，补齐泵 ID、GPIO8/68、采样/热敏参数及 watchdog/VOUT_OVP 完整语义；RISK-004 保留。
3. 获得明确授权及回退方案后，分阶段补齐 PD/PPS、60/600 秒、SOC 与保护故障真机证据；不继承来源内核验收。

## 最近验证

[本轮续查与验证](archive/research/2026-11-08-pps-topology-followup.md)；[既有受限 L3](archive/reviews/2026-11-08-fastcharge-ci.md)；[来源锁](../manifests/sources.lock.json)。
