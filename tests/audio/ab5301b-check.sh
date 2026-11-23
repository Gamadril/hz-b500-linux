#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/ab5301b/check"
mkdir -p "$OUT"
cc -std=gnu99 -Wall -Wextra -Werror -O2 \
    "$ROOT/package/utils/ab5301b/src/ab5301b.c" -o "$OUT/ab5301b"
cc -std=gnu99 -Wall -Wextra -Werror -O2 \
    "$ROOT/tests/audio/ab5301b-test.c" -o "$OUT/ab5301b-test"
"$OUT/ab5301b-test" "$ROOT/tests/audio/fixtures/ab5301b-stock-boot-tx.hex" \
    "$ROOT/tests/audio/fixtures/ab5301b-stock-boot-rx.hex" 2> "$OUT/serial-test.log"
cc -std=gnu99 -Wall -Wextra -Werror -O2 \
    "$ROOT/tests/audio/ab5301b-bluetooth-test.c" -o "$OUT/ab5301b-bluetooth-test"
"$OUT/ab5301b-bluetooth-test" 2> "$OUT/bluetooth-test.log"
