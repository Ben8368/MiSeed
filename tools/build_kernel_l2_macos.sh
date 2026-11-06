#!/usr/bin/env bash
set -euo pipefail

readonly repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly profile="${MISEED_COLIMA_PROFILE:-miseed-boot-l2}"
readonly docker_context="colima-${profile}"
readonly image="miseed-boot-l2:local"
readonly work_volume="miseed-boot-l2-${profile}"
readonly jobs="${MISEED_JOBS:-1}"

fail() { printf 'error: %s\n' "$*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || fail "missing required tool: $1"; }

require colima
require docker
[[ "$(uname -s)" == Darwin ]] || fail "this entry point is for a macOS host"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail "MISEED_JOBS must be a positive integer"

case "$(uname -m)" in
    arm64)
        if ! colima status "$profile" 2>/dev/null | grep -qi 'running'; then
            colima start "$profile" --vm-type vz --arch aarch64 --vz-rosetta \
                --cpus 4 --memory 8 --disk 60 --activate=false
        fi
        ;;
    x86_64)
        if ! colima status "$profile" 2>/dev/null | grep -qi 'running'; then
            colima start "$profile" --vm-type vz --arch x86_64 \
                --cpus 4 --memory 8 --disk 60 --activate=false
        fi
        ;;
    *) fail "unsupported macOS architecture: $(uname -m)" ;;
esac

docker --context "$docker_context" build \
    --platform linux/amd64 \
    --file tools/macos-l2.Dockerfile \
    --tag "$image" \
    "$repo_root"

docker --context "$docker_context" volume create "$work_volume" >/dev/null
docker --context "$docker_context" run --rm \
    --platform linux/amd64 \
    --mount "type=bind,source=${repo_root},target=/workspace" \
    --mount "type=volume,source=${work_volume},target=/build" \
    --workdir /workspace \
    --env MISEED_WORK_ROOT=/build \
    --env MISEED_OUTPUT_ROOT=/workspace/.work/boot-l2 \
    --env "MISEED_JOBS=${jobs}" \
    "$image" bash tools/build_kernel_l2.sh
