#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
HEADER=${1:-"$ROOT/package/gui/littlevgl-8/lv_drivers/display/g2d_driver_enh.h"}
OUT="$ROOT/out/d1s-hz-b500_sd/g2d-abi-check"
mkdir -p "$OUT"
for variant in kernel lvgl; do
    case "$variant" in
        kernel) input="$ROOT/lichee/linux-5.4/include/linux/g2d_driver.h" ;;
        lvgl) input="$HEADER" ;;
    esac
    gcc -std=gnu11 -Wall -Werror -include stdbool.h \
        "-DG2D_HEADER=\"$input\"" "$ROOT/tests/g2d-abi/abi.c" -o "$OUT/$variant"
    "$OUT/$variant" > "$OUT/$variant.txt"
done
diff -u "$OUT/kernel.txt" "$OUT/lvgl.txt"
printf 'PASS: LVGL fill/blit/blend/mixer layouts and fill payload match kernel ABI\n'
