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
readonly source_tree="${work_root}/linux-source-${kernel_revision}"
readonly build_tree="${output_root}/build"
readonly template="${work_root}/reference-boot-v2.img"
readonly input_dts="${repo_root}/kernel/umi-csot.dts"
readonly config_fragment="${repo_root}/kernel/umi-l2.config"
export KBUILD_BUILD_TIMESTAMP="2026-11-05 00:00:00 UTC"
export KBUILD_BUILD_VERSION=1
export KBUILD_BUILD_USER=MiSeed
export KBUILD_BUILD_HOST=Ubuntu2404

fail() { printf 'error: %s\n' "$*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"; }

for tool in git make tar sha256sum python3 aarch64-linux-gnu-gcc dtc fdtget; do require "$tool"; done
[[ -f "$input_dts" && -f "$config_fragment" ]] || fail "board DTS or config fragment is missing"
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

if [[ ! -d "$source_tree" ]]; then
    readonly source_stage="$(mktemp -d "${work_root}/linux-source.XXXXXX")"
    if [[ -n "${MISEED_KERNEL_SOURCE:-}" ]]; then
        [[ "$(git -C "$MISEED_KERNEL_SOURCE" rev-parse HEAD)" == "$kernel_revision" ]] || fail "Linux source revision mismatch"
        [[ -z "$(git -C "$MISEED_KERNEL_SOURCE" status --porcelain)" ]] || fail "Linux source tree must be clean"
        git -C "$MISEED_KERNEL_SOURCE" archive "$kernel_revision" | tar -x -C "$source_stage"
    else
    if [[ ! -d "$source_cache" ]]; then
        git init --bare "$source_cache"
        git -C "$source_cache" remote add origin "$kernel_url"
    fi
    if ! git -C "$source_cache" cat-file -e "${kernel_revision}^{commit}" 2>/dev/null; then
        git -C "$source_cache" fetch --depth=1 --filter=blob:none origin "$kernel_revision"
    fi
    [[ "$(git -C "$source_cache" rev-parse "$kernel_revision^{commit}")" == "$kernel_revision" ]] || fail "Linux revision mismatch"
        git -C "$source_cache" archive "$kernel_revision" | tar -x -C "$source_stage"
    fi
    printf '%s\n' "$kernel_revision" > "$source_stage/.miseed-source-revision"
    mv "$source_stage" "$source_tree"
fi
[[ "$(cat "$source_tree/.miseed-source-revision")" == "$kernel_revision" ]] || fail "cached Linux source revision marker mismatch"

readonly qcom_dts_dir="$source_tree/arch/arm64/boot/dts/qcom"
readonly anchor_dtsi="${output_root}/umi-csot-abl-anchors.dtsi"
readonly reference_dtb="${work_root}/reference-boot-v2.dtb"
python3 "$repo_root/tools/extract_boot_v2_dtb.py" \
    --image "$template" --sha256 "$boot_sha256" --output "$reference_dtb"
python3 "$repo_root/tools/extract_abl_anchors.py" \
    "$reference_dtb" "$anchor_dtsi" "$input_dts" "$qcom_dts_dir"
install -m 0644 "$input_dts" "$qcom_dts_dir/umi-csot.dts"
install -m 0644 "$anchor_dtsi" "$qcom_dts_dir/umi-csot-abl-anchors.dtsi"
grep -Fqx 'dtb-y += umi-csot.dtb' "$qcom_dts_dir/Makefile" || printf '\ndtb-y += umi-csot.dtb\n' >> "$qcom_dts_dir/Makefile"
grep -Fqx 'DTC_FLAGS_umi-csot += -@' "$qcom_dts_dir/Makefile" || printf 'DTC_FLAGS_umi-csot += -@\n' >> "$qcom_dts_dir/Makefile"

mkdir -p "$build_tree"
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
"$source_tree/scripts/kconfig/merge_config.sh" -m -O "$build_tree" \
    "$build_tree/.config" "$config_fragment"
make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
for option in ARCH_QCOM BLK_DEV_INITRD RD_GZIP DEVTMPFS PROC_FS SYSFS OF_OVERLAY; do
    grep -q "^CONFIG_${option}=y$" "$build_tree/.config" || fail "resolved config lost CONFIG_${option}"
done

make -C "$source_tree" O="$build_tree" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    -j"$(nproc)" Image.gz qcom/umi-csot.dtb

readonly dtb="$build_tree/arch/arm64/boot/dts/qcom/umi-csot.dtb"
readonly symbols="$(fdtget "$dtb" /__symbols__ arch_timer 2>/dev/null || true)"
[[ "$symbols" == "/timer" ]] || fail "DTB lacks overlay symbols (__symbols__.arch_timer=$symbols)"
readonly anchor_count="$(fdtget -l "$dtb" /abl_ovf | wc -l)"
readonly symbol_count="$(fdtget -p "$dtb" /__symbols__ | wc -l)"
(( anchor_count > 0 )) || fail "DTB has no placeholder anchors under /abl_ovf"
(( symbol_count >= 152 )) || fail "DTB has only $symbol_count overlay symbols"
python3 "$repo_root/tools/extract_abl_anchors.py" \
    "$reference_dtb" "$anchor_dtsi" "$input_dts" "$qcom_dts_dir" "$dtb"
[[ "$(fdtget "$dtb" / model)" == "Xiaomi Mi 10 (CSOT, MiSeed L2)" ]] || fail "unexpected board DTB model"

readonly rescue_bin="$output_root/rescue-init"
readonly initramfs="$output_root/rescue-initramfs.cpio.gz"
readonly kernel_gz="$build_tree/arch/arm64/boot/Image.gz"
aarch64-linux-gnu-gcc -Os -static -s -Wall -Wextra -Werror \
    -o "$rescue_bin" "$repo_root/kernel/rescue_init.c"
python3 "$repo_root/tools/make_initramfs.py" --init "$rescue_bin" --output "$initramfs"
python3 "$repo_root/tools/pack_android_boot_v2.py" \
    --template "$template" --template-sha256 "$boot_sha256" \
    --kernel "$kernel_gz" --ramdisk "$initramfs" --dtb "$dtb" \
    --output "$output_root/boot.img"

printf '\nMiSeed boot L2 input and artifact hashes:\n'
sha256sum "$input_dts" "$anchor_dtsi" "$repo_root/tools/extract_abl_anchors.py" \
    "$repo_root/tools/extract_boot_v2_dtb.py" "$config_fragment" \
    "$repo_root/kernel/rescue_init.c" "$repo_root/tools/make_initramfs.py" \
    "$repo_root/tools/pack_android_boot_v2.py" "$template" \
    "$build_tree/.config" "$kernel_gz" "$initramfs" "$dtb" "$output_root/boot.img"
printf '\nLinux revision: %s\nReference report revision: %s\n' "$kernel_revision" "$reference_revision"
printf 'Android header template SHA-256: %s (metadata only; payloads replaced)\n' "$boot_sha256"
printf 'Compiler: %s\nBuild output: %s\n' "$(aarch64-linux-gnu-gcc --version | sed -n '1p')" "$output_root"
