#!/usr/bin/env bash
# boot0's LZMA payload: OpenSBI at +0, DTB at +1 MiB, Linux at +2 MiB.
set -euo pipefail

SDK_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BOARD="${1:-d1s-hz-b500_sd}"
case "$BOARD" in
    d1s-hz-b500_sd) ;;
    *) printf 'Unsupported direct-boot board: %s\n' "$BOARD" >&2; exit 1 ;;
esac

BUILD_OUT="$SDK_ROOT/out/$BOARD"
SBI="$BUILD_OUT/opensbi/platform/generic/firmware/fw_jump.bin"
DTB="$BUILD_OUT/image/sunxi.fex"
KERNEL="$BUILD_OUT/compile_dir/target/linux-$BOARD/linux-5.4.61/arch/riscv/boot/Image"
PAYLOAD="$BUILD_OUT/image/epos.img"
LZMA="$SDK_ROOT/out/host/bin/lzma"

for component in "$SBI" "$DTB" "$KERNEL"; do
    [[ -s "$component" ]] || { printf 'Missing payload component: %s\n' "$component" >&2; exit 1; }
done
[[ -x "$LZMA" ]] || { printf 'Missing SDK LZMA encoder: %s\n' "$LZMA" >&2; exit 1; }
(( $(stat -c %s "$SBI") <= 0x100000 )) || { echo 'OpenSBI overlaps DTB' >&2; exit 1; }
(( $(stat -c %s "$DTB") <= 0x100000 )) || { echo 'DTB overlaps kernel' >&2; exit 1; }
PAYLOAD_SIZE=$((0x200000 + $(stat -c %s "$KERNEL")))
# Keep the payload below the HZ-B500 early framebuffer address.
(( PAYLOAD_SIZE <= 0x3c00000 )) || { echo 'Kernel overlaps early framebuffer' >&2; exit 1; }

mkdir -p "$BUILD_OUT/image"
# Truncate first so padding stays zero even when replacing a longer old payload.
: > "$PAYLOAD"
truncate -s "$PAYLOAD_SIZE" "$PAYLOAD"
dd if="$SBI" of="$PAYLOAD" conv=notrunc status=none
dd if="$DTB" of="$PAYLOAD" bs=1M seek=1 conv=notrunc status=none
dd if="$KERNEL" of="$PAYLOAD" bs=1M seek=2 conv=notrunc status=none
"$LZMA" e "$PAYLOAD" "$PAYLOAD.lzma"
printf 'Created %s (%s bytes, raw %s bytes)\n' "$PAYLOAD.lzma" \
    "$(stat -c %s "$PAYLOAD.lzma")" "$PAYLOAD_SIZE"
