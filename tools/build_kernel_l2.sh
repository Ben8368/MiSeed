#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly kernel_url="https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git"
readonly kernel_revision="f45ed76d6791cf72ff1265ee93511175bbbc2f18"
readonly reference_revision="cf83073887899f485a5c606f9842c5e691ea796c"
readonly boot_url="https://github.com/misaka-link/umi-linux/releases/download/v2026-09-27/boot-umi-csot-ovf-wifi6-gpu-oc-final.img"
readonly boot_sha256="a7e4d341e35a663683bf41a8fe99d472bc6b8568aa1b54b05901dc7e9f2d842a"
readonly work_root="${MISEED_WORK_ROOT:-${TMPDIR:-/tmp}/miseed-boot-l2}"
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
readonly observer_applier="${repo_root}/tools/apply_power_observer.py"
readonly observer_source="${repo_root}/kernel/power_observer.c"
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

for tool in git make tar sha256sum python3 aarch64-linux-gnu-gcc aarch64-linux-gnu-nm dtc fdtget fdtput fdtoverlay; do require "$tool"; done
require nproc
readonly jobs="${MISEED_JOBS:-$(nproc)}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "MISEED_JOBS must be a positive integer"
[[ -f "$input_dts" && -f "$config_fragment" ]] || fail "board DTS or config fragment is missing"
[[ -f "$observer_source" && -f "$observer_applier" && -f "$repo_root/kernel/charge_log.h" ]] || fail "power log source inputs are missing"
[[ "$(aarch64-linux-gnu-gcc -dumpfullversion)" == "13.3.0" ]] || fail "expected aarch64-linux-gnu-gcc 13.3.0"
[[ "$(make --version | sed -n '1s/.* //p')" == "4.3" ]] || fail "expected GNU make 4.3"
grep -q '^CONFIG_ARCH_QCOM=y$' "$config_fragment" || fail "config fragment must enable ARCH_QCOM"

mkdir -p "$work_root" "$output_root"
if [[ ! -f "$template" ]]; then
    python3 - "$boot_url" "$template" <<'PY'
import sys
from urllib.request import urlretrieve
urlretrieve(sys.argv[1], sys.argv[2])
PY
fi
[[ "$(sha256sum "$template" | awk '{print $1}')" == "$boot_sha256" ]] || fail "locked Android header-v2 template SHA-256 mismatch"

if [[ -n "${MISEED_KERNEL_SOURCE:-}" ]]; then
    [[ "$(git -C "$MISEED_KERNEL_SOURCE" rev-parse HEAD)" == "$kernel_revision" ]] || fail "Linux source revision mismatch"
    [[ -z "$(git -C "$MISEED_KERNEL_SOURCE" status --porcelain)" ]] || fail "Linux source tree must be clean"
else
    if [[ ! -d "$source_cache" ]]; then
        git init --bare "$source_cache"
        git -C "$source_cache" remote add origin "$kernel_url"
    fi
    if ! git -C "$source_cache" cat-file -e "${kernel_revision}^{commit}" 2>/dev/null; then
        git -C "$source_cache" fetch --depth=1 --filter=blob:none origin "$kernel_revision"
    fi
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
python3 "$observer_applier" --source "$source_tree"
python3 "$repo_root/tools/apply_power_port.py" --source "$source_tree"
python3 "$repo_root/tools/extract_boot_v2_dtb.py" \
    --image "$template" --sha256 "$boot_sha256" --output "$reference_dtb"
python3 "$repo_root/tools/extract_abl_anchors.py" \
    "$reference_dtb" "$anchor_dtsi" "$input_dts" "$qcom_dts_dir" "$dtbo_targets"
install -m 0644 "$input_dts" "$qcom_dts_dir/umi-csot.dts"
install -m 0644 "$anchor_dtsi" "$qcom_dts_dir/umi-csot-abl-anchors.dtsi"
grep -Fqx 'dtb-y += umi-csot.dtb' "$qcom_dts_dir/Makefile" || printf '\ndtb-y += umi-csot.dtb\n' >> "$qcom_dts_dir/Makefile"
grep -Fqx 'DTC_FLAGS_umi-csot += -@' "$qcom_dts_dir/Makefile" || printf 'DTC_FLAGS_umi-csot += -@\n' >> "$qcom_dts_dir/Makefile"

mkdir -p "$build_tree"
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- allnoconfig
"$source_tree/scripts/kconfig/merge_config.sh" -m -O "$build_tree" \
    "$build_tree/.config" "$config_fragment"
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
for option in BATTERY_MISEED_QCOM_FG CHARGER_MISEED_PM8150B; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "runtime power driver must be built-in: ${option}"
done
for option in MODULES POWER_SUPPLY IIO QCOM_SPMI_ADC5; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "power compile test lost CONFIG_${option}"
done
for option in CHARGER_MISEED_UMI_PPS I2C_QCOM_GENI TYPEC_TCPM TYPEC_QCOM_PMIC REGULATOR_QCOM_USB_VBUS USB; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "PPS compile coverage lost CONFIG_${option}"
done
for option in MEMFD_CREATE FILE_LOCKING; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "RAM recorder lost CONFIG_${option}"
done
for option in ARCH_QCOM ARM64_VA_BITS_48 ARM64_PA_BITS_48 PM PINCTRL_MSM PINCTRL_SM8250 COMMON_CLK_QCOM SM_GCC_8250 QCOM_CLK_RPMH QCOM_RPMHPD QCOM_GDSC HWSPINLOCK QCOM_SMEM QCOM_LLCC INTERCONNECT INTERCONNECT_QCOM INTERCONNECT_QCOM_SM8250 INTERCONNECT_QCOM_OSM_L3 IOMMU_SUPPORT ARM_SMMU SPMI QCOM_PDC SPMI_MSM_PMIC_ARB MFD_SPMI_PMIC POWER_RESET_QCOM_PON BINFMT_ELF BLK_DEV_INITRD RD_GZIP DEVTMPFS PROC_FS SYSFS OF_OVERLAY QCOM_GENI_SE SERIAL_QCOM_GENI SERIAL_QCOM_GENI_CONSOLE SERIAL_EARLYCON CMDLINE_FORCE FRAMEBUFFER_CONSOLE_ROTATION FONTS FONT_TER16x32 QCOM_COMMAND_DB QCOM_RPMH USB_GADGET USB_DWC3 USB_DWC3_QCOM USB_DWC3_GADGET USB_CONFIGFS USB_CONFIGFS_ACM U_SERIAL_CONSOLE PHY_QCOM_USB_SNPS_FEMTO_V2 REGULATOR REGULATOR_FIXED_VOLTAGE REGULATOR_QCOM_RPMH; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "resolved config lost CONFIG_${option}"
done
grep -Fqx 'CONFIG_ARM64_VA_BITS=48' "$build_tree/.config" || fail "resolved config is not using 48-bit virtual addresses"
grep -Fqx 'CONFIG_ARM64_PA_BITS=48' "$build_tree/.config" || fail "resolved config is not using 48-bit physical addresses"
grep -Fqx 'CONFIG_CMDLINE="console=ttyMSM0,115200n8 console=tty0 loglevel=8 keep_bootcon earlycon pd_ignore_unused clk_ignore_unused regulator_ignore_unused fbcon=font:TER16x32,rotate:3 rdinit=/init"' "$build_tree/.config" || fail "resolved config lost the forced diagnostic command line"

make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    -j"$jobs" Image qcom/umi-csot.dtb modules

# Runtime drivers must be built-in; the rescue image needs no module loader.
for driver in qcom_fg qcom_pm8150b_charger umi_pps; do
    [[ -s "$build_tree/drivers/power/supply/${driver}.o" ]] || fail "runtime power object missing: ${driver}"
    install -m 0644 "$build_tree/drivers/power/supply/${driver}.o" "$output_root/${driver}.o"
done
cp "$build_tree/.config" "$output_root/resolved.config"
aarch64-linux-gnu-nm "$build_tree/vmlinux" > "$output_root/vmlinux.symbols"
for symbol in qcom_fg_probe smb5_probe umi_pps_probe qcom_pmic_typec_probe; do
    grep -Eq " [tT] ${symbol}$" "$output_root/vmlinux.symbols" || fail "runtime power driver not linked: ${symbol}"
done

readonly dtb="$output_root/umi-csot.dtb"
# Work on a copy: every build starts with dtc's original symbol identities.
install -m 0644 "$build_tree/arch/arm64/boot/dts/qcom/umi-csot.dtb" "$dtb"
python3 "$repo_root/tools/isolate_abl_targets.py" "$dtb"
# A compile-only PPS port must never silently become an active power profile.
for label in umi_pps pm8150b_typec i2c15 pm8150b_vbus; do
    node="$(fdtget "$dtb" /__symbols__ "$label")"
    [[ "$(fdtget "$dtb" "$node" status)" == disabled ]] || fail "unqualified power node enabled: ${label}"
done
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
aarch64-linux-gnu-gcc -Os -static -s -Wall -Wextra -Werror \
    -o "$rescue_bin" "$repo_root/kernel/rescue_init.c"
python3 "$repo_root/tools/make_initramfs.py" --init "$rescue_bin" --output "$initramfs"
python3 "$repo_root/tools/pack_android_boot_v2.py" \
    --template "$template" --template-sha256 "$boot_sha256" \
    --kernel "$kernel_image" --ramdisk "$initramfs" --dtb "$dtb" \
    --output "$output_root/boot.img"

printf '\nMiSeed boot L2 input and artifact hashes:\n'
sha256sum "$input_dts" "$anchor_dtsi" "$repo_root/tools/extract_abl_anchors.py" \
    "$repo_root/tools/extract_boot_v2_dtb.py" "$repo_root/tools/isolate_abl_targets.py" "$repo_root/tests/test_abl_isolation.py" "$config_fragment" \
    "$repo_root/kernel/rescue_init.c" "$repo_root/kernel/charge_log.h" \
    "$observer_source" "$observer_applier" "$repo_root/tools/make_initramfs.py" \
    "$repo_root/kernel/power/qcom_fg.c" "$repo_root/kernel/power/qcom_pm8150b_charger.c" \
    "$repo_root/kernel/power/Kconfig" "$repo_root/kernel/power/umi_charge_policy.h" \
    "$repo_root/kernel/power/umi-pm8150b.dtsi" "$repo_root/tools/apply_power_port.py" \
    "$repo_root/kernel/power/umi_pps.c" "$repo_root/kernel/power/umi_pps_policy.h" \
    "$repo_root/kernel/power/umi_smb5.h" "$repo_root/kernel/power/umi-pps.dtsi" \
    "$output_root/umi_pps.o" "$output_root/qcom_fg.o" "$output_root/qcom_pm8150b_charger.o" \
    "$repo_root/tools/pack_android_boot_v2.py" "$template" \
    "$build_tree/.config" "$kernel_image" "$initramfs" "$dtb" "$output_root/boot.img"
printf '\nLinux revision: %s\nReference report revision: %s\n' "$kernel_revision" "$reference_revision"
printf 'Android header template SHA-256: %s (metadata only; payloads replaced)\n' "$boot_sha256"
printf 'Compiler: %s\nBuild output: %s\n' "$(aarch64-linux-gnu-gcc --version | sed -n '1p')" "$output_root"
