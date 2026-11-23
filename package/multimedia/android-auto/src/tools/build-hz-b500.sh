#!/usr/bin/env bash
# Use the SAME Linux cross compiler/sysroot as the board firmware, not host libc.
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${CEDAR_SDK_ROOT:?set CEDAR_SDK_ROOT to the HZ-B500 Tina SDK}"
cross=${CROSS_COMPILE:-riscv64-unknown-linux-gnu-}
command -v "${cross}gcc" >/dev/null
command -v "${cross}ar" >/dev/null
export CC_riscv64gc_unknown_linux_gnu="${cross}gcc"
export AR_riscv64gc_unknown_linux_gnu="${cross}ar"
if [[ -z ${CMAKE:-} && -x /usr/bin/cmake ]]; then
    export CMAKE=/usr/bin/cmake
fi
# Keep the old Tina compiler's CMake caches separate from native/distro builds.
export CARGO_TARGET_DIR=${CARGO_TARGET_DIR:-"$CEDAR_SDK_ROOT/out/android-auto/cargo"}
# Replace only ld: current Rust emits ISA attributes the SDK's 2020 ld cannot read.
# GCC continues supplying the SDK's start files, libc and libgcc.
modern_ld=$(command -v "${CEDAR_LD:-riscv64-linux-gnu-ld}")
mkdir -p "$CARGO_TARGET_DIR/linker"
export CEDAR_LINKER_PREFIX=$(cd -- "$CARGO_TARGET_DIR/linker" && pwd)
if [[ ! -e "$CEDAR_LINKER_PREFIX/ld" ]]; then
    ln -s "$modern_ld" "$CEDAR_LINKER_PREFIX/ld"
else
    [[ $(readlink "$CEDAR_LINKER_PREFIX/ld") == "$modern_ld" ]] || {
        echo 'The build directory uses a different linker; choose another CARGO_TARGET_DIR.' >&2
        exit 1
    }
fi
export CROSS_COMPILE="$cross"
export CARGO_TARGET_RISCV64GC_UNKNOWN_LINUX_GNU_LINKER="$root/tools/tina-linker.sh"
cd "$root"
cargo build --locked --release --target riscv64gc-unknown-linux-gnu \
    --bin cedar-headunit --features cedar,ab5301b "$@"
