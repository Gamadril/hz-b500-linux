#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${CEDAR_SDK_ROOT:?set CEDAR_SDK_ROOT to the HZ-B500 source tree}"
out="$CEDAR_SDK_ROOT/out/android-auto/touch-tests"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    "$root/tests/touch/native.c" -o "$out/native"
"$out/native"
if command -v riscv64-linux-gnu-gcc >/dev/null; then
    riscv64-linux-gnu-gcc -std=c11 -Wall -Wextra -Werror \
        -c "$root/native/touch_input.c" -o "$out/touch-riscv.o"
    echo 'PASS: evdev bridge compiled for RISC-V'
fi
