#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/d1s-hz-b500_sd/audio-ths/check"
mkdir -p "$OUT"
awk '/^static int sunxi_card_init\(/ { copy=1 }
     copy { print } copy && /^}/ { copy=0 }' \
    "$ROOT/lichee/linux-5.4/sound/soc/sunxi/sun20iw1-sndcodec.c" > "$OUT/card-init.inc"
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -I "$OUT" \
    "$ROOT/tests/audio/card-init.c" -o "$OUT/card-init"
"$OUT/card-init"
