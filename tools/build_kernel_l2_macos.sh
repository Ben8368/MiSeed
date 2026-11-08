#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly profile="${MISEED_COLIMA_PROFILE:-miseed-boot-l2}"
readonly docker_context="colima-${profile}"
readonly image="miseed-boot-l2:local"
readonly jobs="${MISEED_JOBS:-4}"

fail() { printf 'error: %s\n' "$*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"; }

require colima
require docker
[[ "$(uname -s)" == Darwin ]] || fail "this entry point is for a macOS host"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "MISEED_JOBS must be a positive integer"

case "$(uname -m)" in
    arm64)
        readonly build_arch=arm64
        readonly image_platform=linux/arm64
        if ! colima status "$profile" 2>/dev/null | grep -qi 'running'; then
            colima start "$profile" --vm-type vz --arch aarch64 \
                --cpus 4 --memory 8 --disk 60 --activate=false
        fi
        ;;
    x86_64)
        readonly build_arch=amd64
        readonly image_platform=linux/amd64
        if ! colima status "$profile" 2>/dev/null | grep -qi 'running'; then
            colima start "$profile" --vm-type vz --arch x86_64 \
                --cpus 4 --memory 8 --disk 60 --activate=false
        fi
        ;;
    *) fail "unsupported macOS architecture: $(uname -m)" ;;
esac
readonly work_volume="miseed-boot-l2-${profile}-${build_arch}"

docker --context "$docker_context" build \
    --platform "$image_platform" \
    --file "$repo_root/tools/macos-l2.Dockerfile" \
    --tag "$image" \
    "$repo_root"

docker --context "$docker_context" volume create "$work_volume" >/dev/null
docker --context "$docker_context" run --rm \
    --platform "$image_platform" \
    --mount "type=bind,source=${repo_root},target=/workspace" \
    --mount "type=volume,source=${work_volume},target=/build" \
    --workdir /workspace \
    --env MISEED_WORK_ROOT=/build \
    --env "MISEED_OUTPUT_ROOT=/workspace/.work/boot-l2-${build_arch}" \
    --env "MISEED_JOBS=${jobs}" \
    "$image" bash tools/build_kernel_l2.sh
