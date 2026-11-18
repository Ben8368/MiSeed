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

90Hz timing/select data: colorgarden/umi-mainline commit
`531c16b9e50f0351ad0b95051c13a705ad8e223b`, contributors including color.
`dts/xiaomi-umi-panels.dtsi` (BSD-3-Clause) supplies H 16/8/8, V 4/4/8,
90Hz and active-high GPIO50. Its unchanged common DTS also leaves GPIO50 usable.
`patches/panel-xiaomi-umi.c` (GPL-2.0-only) describes a vendor ~128ms
post-init wait before selecting 90Hz; the reference implementation shortened
it to 20ms. MiSeed uses the vendor delay pending L3 confirmation.
The fixed native mode now uses these timings (235788kHz, 1112x2356 totals).
MiSeed requires the GPIO and propagates acquisition/direction errors; it does
not import the reference private sysfs, arbitrary DT command data interface,
optional-error fallback or unrelated touch/power changes. This is a single
90Hz mode, not runtime 60/90 switching. GPIO50 high is not a measurement.
Original attribution above still applies to common commands and DSC data.

The panel-side 90Hz B4/2F/26 tuples come from the Xiaomi/LineageOS
`android_kernel_xiaomi_sm8250` commit `71b13e62f057a649b77fe4062feb73ee72ad609c`,
`arch/arm64/boot/dts/vendor/qcom/dsi-panel-j2-mp-42-02-0b-dsc-cmd.dtsi`,
`timing@1` on-command. The source has no file license override; kernel COPYING
states GPL v2 only (syscall note does not apply to this kernel-internal data).
The tuples live in a separate GPL-2.0-only header. The preceding page selection
and subsequent sleep-out retain their order. No LOS driver, private binding,
dynamic modes, DSI clock overrides or unverified DCS readbacks imported.
Exact hashes and attribution are in sources.lock.json. L3 remains separate.

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

Native-profile staging also fixes idle orphan adoption in the pinned Linux
`drivers/clk/clk.c` (GPL-2.0): resolving a software parent link must not power
an unconfigured DSI PLL through CLK_OPS_PARENT_ENABLE. Prepared orphans keep
the existing reference migration. Original CCF copyright notices are retained;
this local change has not been accepted upstream. See the 2026-11-13 clock
handoff review in docs/archive/research.

Native-profile MSM fbdev staging backports Dmitry Baryshkov's upstream fix
`ea9dadeac79cef509a4b8b4a3e3b39a741e63313` (GPL-2.0-only),
"drm/msm: mark the fbdev framebuffer as system memory".
The original file's Red Hat / Rob Clark notices are retained. The single added
FBINFO_VIRTFB flag matches its existing GEM virtual mapping and sysmem fb ops;
it does not change memory allocation, rotation, DSI commands or scanout timing.
Reference: https://lore.kernel.org/r/20260730-drm-msm-fbinfo-virt-v1-1-a27099a6dc58@oss.qualcomm.com
