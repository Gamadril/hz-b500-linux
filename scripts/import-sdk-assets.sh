#!/usr/bin/env bash
# Restore external compilers/cache without adding them to the Git repository.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TARGET_REL=prebuilt/gcc/linux-x86/riscv/toolchain-thead-glibc/riscv64-glibc-gcc-thead_20200702
RV=riscv64-linux-x86_64-20200528
SDK=
DOWNLOADS=0
usage() {
    echo "Usage: $0 --check | --from-sdk /path/to/TinaSDK [--downloads]"
}
while (( $# )); do
    case "$1" in
        --check) shift ;;
        --from-sdk) [[ $# -ge 2 ]] || { usage >&2; exit 2; }; SDK=$2; shift 2 ;;
        --downloads) DOWNLOADS=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
if (( DOWNLOADS )) && [[ -z "$SDK" ]]; then
    echo '--downloads requires --from-sdk' >&2
    exit 2
fi
if [[ -n "$SDK" ]]; then
    SDK=$(cd -- "$SDK" && pwd -P)
    [[ "$SDK" != "$ROOT" ]] || { echo 'Source and destination SDK must differ' >&2; exit 2; }
    command -v rsync >/dev/null
    [[ -x "$SDK/$TARGET_REL/bin/riscv64-unknown-linux-gnu-gcc" ]] || {
        echo "Missing target compiler in $SDK/$TARGET_REL" >&2; exit 1;
    }
    mkdir -p "$ROOT/$TARGET_REL" "$ROOT/prebuilt/bootloader"
    rsync -a --exclude=.git "$SDK/$TARGET_REL/" "$ROOT/$TARGET_REL/"
    BOOT_SOURCE=$SDK/prebuilt/bootloader
    [[ -d "$BOOT_SOURCE" ]] || BOOT_SOURCE=$SDK/lichee/brandy-2.0/tools/toolchain
    for name in "$RV"; do
        if [[ -f "$BOOT_SOURCE/$name.tar.xz" ]]; then
            cp -p -- "$BOOT_SOURCE/$name.tar.xz" "$ROOT/prebuilt/bootloader/"
        elif [[ -d "$BOOT_SOURCE/$name" ]]; then
            mkdir -p "$ROOT/prebuilt/bootloader/$name"
            rsync -a --exclude=.git "$BOOT_SOURCE/$name/" "$ROOT/prebuilt/bootloader/$name/"
        else
            echo "Missing bootloader compiler $name in $BOOT_SOURCE" >&2
            exit 1
        fi
    done
    if (( DOWNLOADS )); then
        [[ -d "$SDK/dl" ]] || { echo "Missing $SDK/dl" >&2; exit 1; }
        mkdir -p "$ROOT/dl"
        rsync -a --exclude=.git --exclude=README.md "$SDK/dl/" "$ROOT/dl/"
    fi
fi
MISSING=0
if [[ -x "$ROOT/$TARGET_REL/bin/riscv64-unknown-linux-gnu-gcc" ]]; then
    echo 'OK: Linux/userspace T-Head compiler'
else
    echo "Missing: $TARGET_REL/bin/riscv64-unknown-linux-gnu-gcc" >&2
    MISSING=1
fi
for name in "$RV"; do
    prefix=riscv64-unknown-linux-gnu
    if [[ -x "$ROOT/prebuilt/bootloader/$name/bin/$prefix-gcc" || -s "$ROOT/prebuilt/bootloader/$name.tar.xz" ]]; then
        echo "OK: bootloader toolchain $name (directory or archive)"
    else
        echo "Missing: prebuilt/bootloader/$name[.tar.xz]" >&2
        MISSING=1
    fi
done
if command -v riscv64-linux-gnu-gcc >/dev/null; then
    echo 'OK: OpenSBI distribution cross compiler'
else
    echo 'Missing host command: riscv64-linux-gnu-gcc (OpenSBI)' >&2
    MISSING=1
fi
exit "$MISSING"
