#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/d1s-hz-b500_sd/lvgl-alloc-fallback/host-check"
mkdir -p "$OUT"
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -DLV_DRV_NO_CONF -DUSE_SUNXIFB=1 -DUSE_SUNXIFB_DOUBLE_BUFFER \
    -DUSE_SUNXIFB_CACHE -DUSE_SUNXIFB_G2D=1 -DUSE_SUNXIFB_G2D_ROTATE \
    -DCONF_G2D_VERSION_NEW -I "$ROOT/tests/lvgl-fb/include" -I "$ROOT" \
    "$ROOT/tests/lvgl-fb/fallback.c" -o "$OUT/fallback"
for mode in allocation address ion-open g2d-open rotation fb-info fb-map success; do
    "$OUT/fallback" "$mode"
done
