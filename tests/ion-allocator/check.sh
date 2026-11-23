#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/d1s-hz-b500_sd/ion-g2d-fix/host-check"
mkdir -p "$OUT"
for mode in CMA IOMMU; do
    gcc -std=gnu11 -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter \
        -DCONF_KERNEL_VERSION_5_4 -DCONF_KERNEL_$mode \
        -I "$ROOT/package/allwinner/libuapi/src" \
        -I "$ROOT/package/allwinner/libuapi/src/include" \
        "$ROOT/tests/ion-allocator/allocator.c" -pthread -o "$OUT/allocator-$mode"
    "$OUT/allocator-$mode"
done
# Compile the retained legacy path as well.
gcc -std=gnu11 -fsyntax-only -DCONF_KERNEL_VERSION_4_9 -DCONF_KERNEL_CMA \
    -I "$ROOT/package/allwinner/libuapi/src/include" \
    "$ROOT/package/allwinner/libuapi/src/ion_alloc.c"
