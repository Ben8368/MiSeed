#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly kernel_url="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git"
readonly kernel_mirror_url="https://github.com/gregkh/linux.git"
readonly kernel_tag="v6.18.60"
readonly kernel_tag_object="16829bf498b811706e98e480879e3fe1754d797e"
readonly kernel_revision="f45ed76d6791cf72ff1265ee93511175bbbc2f18"
readonly reference_revision="cf83073887899f485a5c606f9842c5e691ea796c"
readonly boot_url="https://github.com/misaka-link/umi-linux/releases/download/v2026-09-27/boot-umi-csot-ovf-wifi6-gpu-oc-final.img"
# Same GitHub release asset; API endpoint avoids failing public release redirects.
readonly boot_api_url="https://api.github.com/repos/misaka-link/umi-linux/releases/assets/592897637"
readonly boot_sha256="a7e4d341e35a663683bf41a8fe99d472bc6b8568aa1b54b05901dc7e9f2d842a"
readonly work_root="${MISEED_WORK_ROOT:-${TMPDIR:-/tmp}/miseed-boot-l2}"
readonly native_dsi="${MISEED_NATIVE_DSI:-0}"
[[ "$native_dsi" == 0 || "$native_dsi" == 1 ]] || { echo "MISEED_NATIVE_DSI must be 0 or 1" >&2; exit 1; }
readonly output_root="${MISEED_OUTPUT_ROOT:-${repo_root}/.work/boot-l2}"
readonly source_cache="${work_root}/linux-stable.git"
source_tree="${MISEED_EXTRACTED_SOURCE:-${work_root}/linux-source-${kernel_revision}}"
build_tree="${MISEED_BUILD_ROOT:-${work_root}/build-${kernel_revision}}"
source_stage=""
fallback_source_tree=""
fallback_build_tree=""
readonly template="${work_root}/reference-boot-v2.img"
readonly input_dts="${repo_root}/kernel/umi-csot.dts"
readonly config_fragment="${repo_root}/kernel/umi-l2.config"
export KBUILD_BUILD_TIMESTAMP="2026-11-05 00:00:00 UTC"
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=MiSeed
export KBUILD_BUILD_HOST=Ubuntu2404

fail() { printf 'error: %s\n' "$*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"; }
cleanup_fallback_trees() {
    local path
    for path in "$source_stage" "$fallback_source_tree" "$fallback_build_tree"; do
        [[ -n "$path" && -d "$path" && ! -L "$path" ]] || continue
        case "$path" in
            "${work_root}"/linux-source.*|"${work_root}"/build-clean.*) rm -rf -- "$path" ;;
        esac
    done
}
trap cleanup_fallback_trees EXIT

require python3
if [[ "$native_dsi" == 1 ]]; then
    [[ -n "${MISEED_OUTPUT_ROOT:-}" ]] || fail "native DSI requires a distinct explicit MISEED_OUTPUT_ROOT"
    distinct_output="$(python3 -c 'from pathlib import Path; import sys; print(int(Path(sys.argv[1]).resolve() != Path(sys.argv[2]).resolve()))' \
        "$output_root" "$repo_root/.work/boot-l2")"
    [[ "$distinct_output" == 1 ]] || fail "native DSI requires a distinct explicit MISEED_OUTPUT_ROOT"
fi
# Capture recursive make diagnostics too: make 4.3 may return 0 on clock skew.
make() { python3 "$repo_root/tools/run_build_command.py" -- make "$@"; }
python3 "$repo_root/tools/check_build_lock.py" --root "$repo_root" \
    --kernel-url "$kernel_url" --kernel-revision "$kernel_revision" \
    --kernel-mirror-url "$kernel_mirror_url" --kernel-tag "$kernel_tag" \
    --kernel-tag-object "$kernel_tag_object" \
    --reference-revision "$reference_revision" --boot-url "$boot_url" \
    --boot-sha256 "$boot_sha256"

for tool in git make tar sha256sum python3 aarch64-linux-gnu-gcc aarch64-linux-gnu-nm dtc fdtget fdtput fdtoverlay; do require "$tool"; done
require nproc
readonly jobs="${MISEED_JOBS:-$(nproc)}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "MISEED_JOBS must be a positive integer"
[[ -f "$input_dts" && -f "$config_fragment" ]] || fail "board DTS or config fragment is missing"
[[ -f "$repo_root/kernel/power/umi_smb5_diagnostics.h" && -f "$repo_root/kernel/charge_log.h" ]] || fail "power log source inputs are missing"
[[ "$(aarch64-linux-gnu-gcc -dumpfullversion)" == "13.3.0" ]] || fail "expected aarch64-linux-gnu-gcc 13.3.0"
[[ "$(make --version | sed -n '1s/.* //p')" == "4.3" ]] || fail "expected GNU make 4.3"
grep -q '^CONFIG_ARCH_QCOM=y$' "$config_fragment" || fail "config fragment must enable ARCH_QCOM"

mkdir -p "$work_root" "$output_root"
if [[ ! -f "$template" ]]; then
    python3 "$repo_root/tools/download_locked.py" --url "$boot_url" \
        --fallback-url "$boot_api_url" --sha256 "$boot_sha256" --output "$template"
fi
[[ "$(sha256sum "$template" | awk '{print $1}')" == "$boot_sha256" ]] || fail "locked Android header-v2 template SHA-256 mismatch"

if [[ -n "${MISEED_KERNEL_SOURCE:-}" ]]; then
    [[ "$(git -C "$MISEED_KERNEL_SOURCE" rev-parse HEAD)" == "$kernel_revision" ]] || fail "Linux source revision mismatch"
    [[ -z "$(git -C "$MISEED_KERNEL_SOURCE" status --porcelain)" ]] || fail "Linux source tree must be clean"
else
    python3 "$repo_root/tools/fetch_kernel_source.py" --cache "$source_cache" \
        --kernel-url "$kernel_url" --mirror-url "$kernel_mirror_url" \
        --tag "$kernel_tag" --tag-object "$kernel_tag_object" --revision "$kernel_revision"
fi
readonly source_repository="${MISEED_KERNEL_SOURCE:-$source_cache}"
[[ "$(git -C "$source_repository" rev-parse "${kernel_revision}^{commit}")" == "$kernel_revision" ]] || fail "Linux revision mismatch"

source_verified=false
if [[ -d "$source_tree" ]] && python3 "$repo_root/tools/verify_kernel_source.py" \
        --repository "$source_repository" --revision "$kernel_revision" --source "$source_tree"; then
    source_verified=true
fi
if [[ "$source_verified" != true ]]; then
    source_stage="$(mktemp -d "${work_root}/linux-source.XXXXXX")"
    git -C "$source_repository" archive "$kernel_revision" | tar -x -C "$source_stage"
    printf '%s\n' "$kernel_revision" > "$source_stage/.miseed-source-revision"
    if [[ -e "$source_tree" || -L "$source_tree" ]]; then
        printf 'warning: unverified source cache preserved; using fresh source and build trees\n' >&2
        source_tree="$source_stage"
        fallback_source_tree="$source_stage"
        source_stage=""
        fallback_build_tree="$(mktemp -d "${work_root}/build-clean.XXXXXX")"
        build_tree="$fallback_build_tree"
    else
        mv -- "$source_stage" "$source_tree"
        source_stage=""
        if [[ -e "$build_tree" || -L "$build_tree" ]]; then
            fallback_build_tree="$(mktemp -d "${work_root}/build-clean.XXXXXX")"
            build_tree="$fallback_build_tree"
        fi
    fi
fi

readonly qcom_dts_dir="$source_tree/arch/arm64/boot/dts/qcom"
# The L2 environment has libfdt tools; exercise overlay failure cases here too.
(cd "$repo_root" && python3 -m unittest discover -s tests -p test_abl_isolation.py -v)
readonly anchor_dtsi="${output_root}/umi-csot-abl-anchors.dtsi"
readonly reference_dtb="${work_root}/reference-boot-v2.dtb"
readonly dtbo_targets="${repo_root}/kernel/umi-csot-dtbo-targets.txt"
python3 "$repo_root/tools/apply_power_port.py" --source "$source_tree"
python3 "$repo_root/tools/apply_pd_diagnostics.py" --source "$source_tree"
python3 "$repo_root/tools/apply_nfc_port.py" --source "$source_tree" --project-root "$repo_root"
python3 "$repo_root/tools/extract_boot_v2_dtb.py" \
    --image "$template" --sha256 "$boot_sha256" --output "$reference_dtb"
python3 "$repo_root/tools/extract_abl_anchors.py" \
    "$reference_dtb" "$anchor_dtsi" "$input_dts" "$qcom_dts_dir" "$dtbo_targets"
install -m 0644 "$input_dts" "$qcom_dts_dir/umi-csot.dts"
install -m 0644 "$anchor_dtsi" "$qcom_dts_dir/umi-csot-abl-anchors.dtsi"
grep -Fqx 'dtb-y += umi-csot.dtb' "$qcom_dts_dir/Makefile" || printf '\ndtb-y += umi-csot.dtb\n' >> "$qcom_dts_dir/Makefile"
grep -Fqx 'DTC_FLAGS_umi-csot += -@' "$qcom_dts_dir/Makefile" || printf 'DTC_FLAGS_umi-csot += -@\n' >> "$qcom_dts_dir/Makefile"

display_args=()
[[ "$native_dsi" == 0 ]] || display_args+=(--native)
python3 "$repo_root/tools/apply_display_port.py" --source "$source_tree" "${display_args[@]}"
printf '%s\n' "$native_dsi" > "$output_root/native-dsi-profile"
mkdir -p "$build_tree"
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- allnoconfig
"$source_tree/scripts/kconfig/merge_config.sh" -m -O "$build_tree" \
    "$build_tree/.config" "$config_fragment"
if [[ "$native_dsi" == 1 ]]; then
    "$source_tree/scripts/kconfig/merge_config.sh" -m -O "$build_tree" \
        "$build_tree/.config" "$repo_root/kernel/display/umi-native-dsi.config"
fi
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
# Regenerate compiler inputs explicitly: host clock skew can otherwise leave
# allnoconfig's auto.conf newer than the merged .config and skip this step.
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- syncconfig
for option in BATTERY_MISEED_QCOM_FG CHARGER_MISEED_PM8150B; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "runtime power driver must be built-in: ${option}"
done
for option in DRM DRM_SIMPLEDRM DRM_FBDEV_EMULATION DRM_CLIENT_DEFAULT_FBDEV FB_CORE FRAMEBUFFER_CONSOLE MODULES POWER_SUPPLY IIO QCOM_SPMI_ADC5 GPIOLIB NET NFC NFC_NCI NFC_SN100X; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "power compile test lost CONFIG_${option}"
done
for option in CHARGER_MISEED_UMI_PPS I2C_QCOM_GENI TYPEC_TCPM TYPEC_QCOM_PMIC REGULATOR_QCOM_USB_VBUS USB; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "PPS compile coverage lost CONFIG_${option}"
done
for option in MEMFD_CREATE FILE_LOCKING; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "RAM recorder lost CONFIG_${option}"
done
for option in ARCH_QCOM ARM64_VA_BITS_48 ARM64_PA_BITS_48 PM PINCTRL_MSM PINCTRL_SM8250 COMMON_CLK_QCOM SM_GCC_8250 QCOM_CLK_RPMH QCOM_RPMHPD PM_OPP QCOM_GDSC HWSPINLOCK QCOM_SMEM QCOM_LLCC INTERCONNECT INTERCONNECT_QCOM INTERCONNECT_QCOM_SM8250 INTERCONNECT_QCOM_OSM_L3 IOMMU_SUPPORT ARM_SMMU SPMI QCOM_PDC SPMI_MSM_PMIC_ARB MFD_SPMI_PMIC POWER_RESET_QCOM_PON BINFMT_ELF BLK_DEV_INITRD RD_GZIP DEVTMPFS PROC_FS SYSFS OF_OVERLAY QCOM_GENI_SE SERIAL_QCOM_GENI SERIAL_QCOM_GENI_CONSOLE SERIAL_EARLYCON CMDLINE_FORCE FRAMEBUFFER_CONSOLE_ROTATION FONTS FONT_TER16x32 QCOM_COMMAND_DB QCOM_RPMH USB_GADGET USB_DWC3 USB_DWC3_QCOM USB_DWC3_GADGET USB_CONFIGFS USB_CONFIGFS_ACM U_SERIAL_CONSOLE PHY_QCOM_USB_SNPS_FEMTO_V2 REGULATOR REGULATOR_FIXED_VOLTAGE REGULATOR_QCOM_RPMH I2C I2C_QCOM_GENI GPIOLIB NET NFC NFC_NCI NFC_SN100X; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "resolved config lost CONFIG_${option}"
done
grep -Fqx 'CONFIG_ARM64_VA_BITS=48' "$build_tree/.config" || fail "resolved config is not using 48-bit virtual addresses"
grep -Fqx 'CONFIG_ARM64_PA_BITS=48' "$build_tree/.config" || fail "resolved config is not using 48-bit physical addresses"
grep -Fqx 'CONFIG_CMDLINE="console=ttyMSM0,115200n8 console=tty0 loglevel=8 keep_bootcon earlycon pd_ignore_unused clk_ignore_unused regulator_ignore_unused fbcon=font:TER16x32,rotate:3 rdinit=/init"' "$build_tree/.config" || fail "resolved config lost the forced diagnostic command line"

if [[ "$native_dsi" == 1 ]]; then
    for option in DRM_MSM DRM_MSM_DPU DRM_MSM_DSI DRM_MSM_DSI_7NM_PHY SM_DISPCC_8250 DRM_PANEL_XIAOMI_UMI_CSOT; do
        grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "native display lost CONFIG_${option}"
    done
fi
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    -j"$jobs" Image qcom/umi-csot.dtb modules

[[ -s "$build_tree/drivers/nfc/sn100x.o" ]] || fail "SN100T NCI driver object missing"
install -m 0644 "$build_tree/drivers/nfc/sn100x.o" "$output_root/sn100x.o"

# Runtime drivers must be built-in; the rescue image needs no module loader.
# Kbuild expands built-in composite targets into their constituent objects;
# a standalone umi-bq25970.o is only materialized for the module case.
# Check both built-in constituents here and their linked symbols below.
readonly -a power_drivers=(qcom_fg_core qcom_fg_io qcom_fg_measure
    qcom_pm8150b_core qcom_pm8150b_hw qcom_pm8150b_policy qcom_pm8150b_supply qcom_pm8150b_thermal qcom_pm8150b_pd
    umi_pps umi_pps_hw umi_pps_supply umi_pps_thermal umi_pps_driver bq25970 bq25970_i2c)
power_objects=()
for driver in "${power_drivers[@]}"; do
    [[ -s "$build_tree/drivers/power/supply/${driver}.o" ]] || fail "runtime power object missing: ${driver}"
    install -m 0644 "$build_tree/drivers/power/supply/${driver}.o" "$output_root/${driver}.o"
    power_objects+=("$output_root/${driver}.o")
done
install -m 0644 "$source_tree/.miseed-pd-diagnostics.json" "$output_root/pd-diagnostics-inputs.json"
install -m 0644 "$build_tree/drivers/usb/typec/tcpm/qcom/qcom_pmic_typec_pdphy.o" "$output_root/qcom_pmic_typec_pdphy.o"
cp "$build_tree/.config" "$output_root/resolved.config"
aarch64-linux-gnu-nm "$build_tree/vmlinux" > "$output_root/vmlinux.symbols"
for symbol in qcom_fg_probe smb5_probe smb5_register_thermal umi_pps_probe qcom_pmic_typec_probe qcom_pmic_typec_pdphy_isr sn100x_probe nci_register_device bq25970_update bq25970_adc bq25970_verify; do
    grep -Eq " [tT] ${symbol}$" "$output_root/vmlinux.symbols" || fail "runtime power driver not linked: ${symbol}"
done

if [[ "$native_dsi" == 1 ]]; then
    grep -Eq ' [tT] umi_csot_probe$' "$output_root/vmlinux.symbols" || fail "native panel not linked"
fi
# Require the standard KMS provider, not just an enabled config fragment.
[[ -s "$build_tree/drivers/gpu/drm/sysfb/simpledrm.o" ]] || fail "simpledrm object missing"
grep -Eq ' [tT] simpledrm_probe$' "$output_root/vmlinux.symbols" || fail "simpledrm not linked"
if grep -q '^CONFIG_FB_SIMPLE=' "$build_tree/.config"; then fail "legacy simplefb competes with simpledrm"; fi
if grep -Eq ' [tT] power_observer_probe$' "$output_root/vmlinux.symbols"; then fail "synthetic observer driver linked"; fi

readonly dtb="$output_root/umi-csot.dtb"
# Work on a copy: every build starts with dtc's original symbol identities.
install -m 0644 "$build_tree/arch/arm64/boot/dts/qcom/umi-csot.dtb" "$dtb"
python3 "$repo_root/tools/isolate_abl_targets.py" "$dtb" "${display_args[@]}"
# The Umi L3 test image intentionally enables the sink-only PD/PPS profile.
for label in umi_pps pm8150b_typec i2c15 qupv3_id_2 pm8150b_vbus; do
    node="$(fdtget "$dtb" /__symbols__ "$label")"
    [[ "$(fdtget "$dtb" "$node" status)" == okay ]] || fail "Umi PD/PPS test node is not enabled: ${label}"
done
readonly nfc_node="$(fdtget "$dtb" /__symbols__ umi_nfc)"
[[ "$(fdtget "$dtb" "$nfc_node" compatible)" == "nxp,sn100t" ]] || fail "Umi SN100T DT compatible mismatch"
[[ "$(fdtget -t x "$dtb" "$nfc_node" reg)" == "28" ]] || fail "Umi SN100T I2C address mismatch"
for pin in enable-gpios:53 firmware-gpios:6e; do
    property="${pin%%:*}"
    expected_gpio="${pin#*:}"
    read -r _ gpio _ <<< "$(fdtget -t x "$dtb" "$nfc_node" "$property")"
    [[ "$gpio" == "$expected_gpio" ]] || fail "Umi SN100T GPIO mismatch: ${property}"
done
[[ "$(fdtget -t x "$dtb" "$nfc_node" interrupts)" == "6f 4" ]] || fail "SN100T level-high IRQ mismatch"
if grep -Eq ' [tT] (nqx_probe|nfc_ioctl)$' "$output_root/vmlinux.symbols"; then
    fail "legacy NFC private ABI linked"
fi
# Verify both Type-C supplies by phandle, not by similarly named nodes.
typec_node="$(fdtget "$dtb" /__symbols__ pm8150b_typec)"
for pair in vdd-pdphy:vreg_l2a_3p1 vdd-vbus:pm8150b_vbus; do
    supply_node="$(fdtget "$dtb" /__symbols__ "${pair#*:}")"
    [[ "$(fdtget -t x "$dtb" "$typec_node" "${pair%%:*}-supply")" == \
       "$(fdtget -t x "$dtb" "$supply_node" phandle)" ]] || fail "Type-C supply mismatch: $pair"
done
[[ "$(fdtget "$dtb" "$typec_node/connector" power-role)" == sink ]] || fail "Type-C must remain sink-only"
readonly symbols="$(fdtget "$dtb" /__symbols__ arch_timer 2>/dev/null || true)"
[[ "$symbols" == "/timer" ]] || fail "DTB lacks overlay symbols (__symbols__.arch_timer=$symbols)"
readonly anchor_count="$(fdtget -l "$dtb" /abl_ovf | wc -l)"
readonly symbol_count="$(fdtget -p "$dtb" /__symbols__ | wc -l)"
(( anchor_count > 0 )) || fail "DTB has no placeholder anchors under /abl_ovf"
(( symbol_count >= 152 )) || fail "DTB has only $symbol_count overlay symbols"
python3 "$repo_root/tools/extract_abl_anchors.py" \
    "$reference_dtb" "$anchor_dtsi" "$input_dts" "$qcom_dts_dir" "$dtbo_targets" "$dtb"
[[ "$(fdtget "$dtb" / model)" == "Xiaomi Mi 10 (CSOT, MiSeed L2)" ]] || fail "unexpected board DTB model"

readonly rescue_bin="$output_root/rescue-init"
readonly initramfs="$output_root/rescue-initramfs.cpio.gz"
readonly kernel_image="$build_tree/arch/arm64/boot/Image"
readonly -a rescue_sources=("$repo_root/kernel/rescue_init.c"
    "$repo_root/kernel/rescue_fs.c" "$repo_root/kernel/rescue_usb_diag.c"
    "$repo_root/kernel/rescue_devices.c" "$repo_root/kernel/rescue_power.c"
    "$repo_root/kernel/rescue_nfc.c" "$repo_root/kernel/rescue_nfc_l3.c"
    "$repo_root/kernel/rescue_nfc_netlink.c"
    "$repo_root/kernel/rescue_usb.c" "$repo_root/kernel/rescue_dt.c"
    "$repo_root/kernel/rescue_display.c" "$repo_root/kernel/rescue_thermal.c"
    "$repo_root/kernel/charge_log.c")
aarch64-linux-gnu-gcc -Os -static -s -Wall -Wextra -Werror \
    -o "$rescue_bin" "${rescue_sources[@]}"
python3 "$repo_root/tools/make_initramfs.py" --init "$rescue_bin" --output "$initramfs"
python3 "$repo_root/tools/pack_android_boot_v2.py" \
    --template "$template" --template-sha256 "$boot_sha256" \
    --kernel "$kernel_image" --ramdisk "$initramfs" --dtb "$dtb" \
    --output "$output_root/boot.img"

cp "$repo_root/manifests/sources.lock.json" "$output_root/build-source-lock.json"
printf '\nMiSeed boot L2 input and artifact hashes:\n'
sha256sum "$input_dts" "$anchor_dtsi" "$repo_root/tools/extract_abl_anchors.py" \
    "$repo_root/tools/extract_boot_v2_dtb.py" "$repo_root/tools/download_locked.py" "$repo_root/tools/isolate_abl_targets.py" "$repo_root/tests/test_abl_isolation.py" "$config_fragment" \
    "${rescue_sources[@]}" "$repo_root/kernel/charge_log.h" "$repo_root/kernel/rescue_internal.h" \
    "$repo_root/kernel/rescue_nfc_netlink.h" \
    "$repo_root/kernel/power/umi_smb5_diagnostics.h" "$repo_root/tools/make_initramfs.py" \
    "$repo_root/kernel/power/qcom_fg_core.c" "$repo_root/kernel/power/qcom_fg_io.c" \
    "$repo_root/kernel/power/qcom_fg_measure.c" "$repo_root/kernel/power/qcom_fg_internal.h" \
    "$repo_root/kernel/power/qcom_pm8150b_core.c" "$repo_root/kernel/power/qcom_pm8150b_hw.c" \
    "$repo_root/kernel/power/qcom_pm8150b_policy.c" "$repo_root/kernel/power/qcom_pm8150b_supply.c" \
    "$repo_root/kernel/power/qcom_pm8150b_thermal.c" \
    "$repo_root/kernel/power/qcom_pm8150b_internal.h" "$repo_root/kernel/power/qcom_pm8150b_regs.h" \
    "$repo_root/kernel/power/Kconfig" "$repo_root/kernel/power/umi_charge_policy.h" \
    "$repo_root/kernel/power/umi-pm8150b.dtsi" "$repo_root/tools/apply_power_port.py" \
    "$repo_root/kernel/power/bq25970.c" "$repo_root/kernel/power/bq25970.h" \
    "$repo_root/kernel/power/umi_pps.c" "$repo_root/kernel/power/umi_pps_policy.h" \
    "$repo_root/kernel/power/umi_pps_hw.c" "$repo_root/kernel/power/umi_pps_supply.c" \
    "$repo_root/kernel/power/umi_pps_thermal.c" "$repo_root/kernel/power/umi_pps_driver.c" \
    "$repo_root/kernel/power/umi_pps_internal.h" "$repo_root/tools/source_layout.py" \
    "$repo_root/kernel/power/umi_smb5.h" "$repo_root/kernel/power/umi-pps.dtsi" \
    "${power_objects[@]}" \
    "$repo_root/tools/apply_pd_diagnostics.py" "$repo_root/kernel/power/pmic_pdphy_receive.inc" \
    "$repo_root/tools/tcpm_fixed_profile.py" "$repo_root/kernel/power/tcpm_fixed_profile.inc" \
    "$output_root/pd-diagnostics-inputs.json" "$output_root/qcom_pmic_typec_pdphy.o" \
    "$repo_root/tools/pack_android_boot_v2.py" "$template" \
    "$build_tree/.config" "$kernel_image" "$initramfs" "$dtb" "$output_root/boot.img"
printf '\nLinux revision: %s\nReference report revision: %s\n' "$kernel_revision" "$reference_revision"
printf 'Android header template SHA-256: %s (metadata only; payloads replaced)\n' "$boot_sha256"
printf 'Compiler: %s\nBuild output: %s\n' "$(aarch64-linux-gnu-gcc --version | sed -n '1p')" "$output_root"
