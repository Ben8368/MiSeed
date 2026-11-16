# 经验索引

DTO 合并须核根属性，见[记录](archive/research/2026-11-11-native-dsi-chip-boundaries.md)。Android 版本核 release/SDK/ROM；无 su 不证无 adbd root。Fastboot 枚举须用主机 USB 权限；沙箱空结果不证断开，见[实测](archive/reviews/2026-11-16-fastboot-host-enumeration.md)、[流程](HOST_TOOLS.md)。

## 真机已闭合结论的表述

已有日志与样本闭合的能力（例如 PD 与 PPS 分别约 10 分钟真机、充电中无故障）直接写作成就与证据范围；开放项只列真正未测的专项（保护注入、温控标度、主机导出、旧故障根因、发布门槛）。禁止用「不等同长期」「不代表稳定」「短时不能宣称」等套话回写否定已闭合结论；时长是否够用由维护者与关闭条件决定，不由代理自行加码。

## 构建 VM 的残留校频

停用来宾 NTP 后，内核仍可能保留 frequency 校正；同时检查宿主同步日志与 adjtimex 读回，用宿主单调时钟交叉采样，再按原时钟守卫重建。Colima 实例与 WSL 分别取证，见[复核](archive/reviews/2026-11-10-pd-pps-detach-clock.md)。
