# Native CSOT display sources

Board data: colorgarden/umi-mainline commit `5fa2e0048f89b82355d2a9de89c1d74ab9910959`.
`dts/xiaomi-umi-panels.dtsi` (BSD-3-Clause) supplies 59 ordered DCS packets,
1080x2340/60 Hz timing, 682 MHz link and DSC 1.1/540x12/8bpc/8bpp settings.
`dts/sm8250-xiaomi-umi.dts` and `dts/sm8250-xiaomi-umi-common.dtsi`
(BSD-3-Clause) supply reset GPIO12 and controller/PHY rail references.
Attribution: colorgarden/umi-mainline contributors; reference notices retained.
The generic reference panel driver is not copied. MiSeed uses a fixed variant
DRM panel with standard backlight, without arbitrary DT commands or register sysfs.
Reference source reports are not MiSeed hardware verification.

Uncertain physical-dimension units are not imported. Panel rails remain inherited;
this is not independent cold-power sequencing. Default rescue stays simpledrm;
native display is opt-in. Variant identity, reset/DSC/brightness, suspend/resume
and handoff require L3. Reference file hashes are in sources.lock.json.

The locked Linux 6.18.60 DSI host receives native-profile-only failure logging:
DMA timeout IOVA/state/clocks and FIFO status before its existing clear.
The patch does not alter transfers, IRQ acknowledgement, resets or IOMMU policy.

Native-profile MDSS_CORE reset wiring follows Linux commit
`8b4faf419d601cead68d7821618369e2c0338b86` by Luca Weiss (BSD-3-Clause),
`arch/arm64/boot/dts/qcom/sm8250.dtsi`. The locked baseline already provides
the reset controller and MDSS reset sequence, but omits this DT reference.
MiSeed also makes that existing sequence return assert/deassert errors and
release its reset handle on failure (GPL-2.0, original Linux Foundation notice
retained in the staged source). This stops probe before a failed takeover;
it does not disable the SMMU or add identity mappings. Umi behavior requires L3.
