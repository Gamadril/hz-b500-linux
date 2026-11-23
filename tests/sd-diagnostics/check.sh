#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${SD_DIAG_TEST_OUT:-$ROOT/out/d1s-hz-b500_sd/sd-init-capture-4gb/host-check}"
SOURCE="$ROOT/lichee/linux-5.4/drivers/mmc/host/sunxi-mmc-diag.c"
mkdir -p "$OUT"
awk '/^#define SDXC_REG_/ { print }' \
    "$ROOT/lichee/linux-5.4/drivers/mmc/host/sunxi-mmc.h" > "$OUT/registers.inc"
awk '/^struct sunxi_mmc_diag_snapshot/ { copy=1 }
     /^static void sunxi_mmc_diag_print/ { copy=0 }
     copy { print }' "$SOURCE" > "$OUT/capture.inc"
awk '/^void sunxi_mmc_diag_start/ || /^void sunxi_mmc_diag_error/ { copy=1 }
     copy { print } copy && /^}/ { copy=0 }' "$SOURCE" >> "$OUT/capture.inc"
gcc -std=gnu11 -Wall -Wextra -Werror -I "$OUT" \
    "$ROOT/tests/sd-diagnostics/capture.c" -o "$OUT/capture"
"$OUT/capture"
