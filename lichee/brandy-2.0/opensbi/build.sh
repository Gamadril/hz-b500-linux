#!/usr/bin/env bash
# build.sh — OpenSBI build script for Tina Linux
# Compiles OpenSBI for the D1s/F133 HZ-B500 SD profile

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
AW_OL_ROOT="$(cd -- "${SCRIPT_DIR}/../../.." && pwd)"

CROSS_COMPILE="${CROSS_COMPILE:-riscv64-linux-gnu-}"
OUTPUT_DIR="${AW_OL_ROOT}/device/config/chips/d1s/bin"

MODE="${1:-all}"

build_flavor() {
    local name="$1"
    local text_start="$2"
    local jump_addr="$3"
    local fdt_addr="$4"
    local build_dir="${AW_OL_ROOT}/out/d1s-hz-b500_${name}/opensbi"

    echo "==> Building OpenSBI flavor: ${name} (TEXT: ${text_start}, JUMP: ${jump_addr})"
    # Separate output trees preserve each profile and allow incremental builds.
    make -C "${SCRIPT_DIR}" \
        O="${build_dir}" \
        CROSS_COMPILE="${CROSS_COMPILE}" \
        PLATFORM=generic \
        FW_TEXT_START="${text_start}" \
        FW_JUMP_ADDR="${jump_addr}" \
        FW_JUMP_FDT_ADDR="${fdt_addr}" \
        FW_DYNAMIC=n FW_PAYLOAD=n \
        -j"$(nproc)"

    # Existing boot0 pack tooling consumes this staging filename.
    mkdir -p "${OUTPUT_DIR}"
    cp -v "${build_dir}/platform/generic/firmware/fw_jump.bin" \
        "${OUTPUT_DIR}/opensbi_sun20iw1p1.bin"
}

case "$MODE" in
    sd|all)
        # Direct jump to Linux Image at 0x40200000, DTB at 0x40100000 (No U-Boot)
        build_flavor sd "0x40000000" "0x40200000" "0x40100000"
        ;;
    *)
        echo "Usage: $0 [sd|all]" >&2
        exit 1
        ;;
esac

echo "OpenSBI build complete."
