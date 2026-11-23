#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${CEDAR_SDK_ROOT:?set CEDAR_SDK_ROOT to the HZ-B500 source tree}"
out="$CEDAR_SDK_ROOT/out/android-auto/audio-tests"
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    "$root/tests/audio/native.c" -ldl -lpthread -o "$out/native"
"$out/native"
cc -std=c11 -Wall -Wextra -Werror -g "$root/tests/audio/smoke.c" \
    "$root/native/alsa_sink.c" -lasound -ldl -lpthread -o "$out/smoke"
"$out/smoke" "$CEDAR_SDK_ROOT/package/multimedia/android-auto/files/alsa.conf"
if command -v riscv64-linux-gnu-gcc >/dev/null; then
    riscv64-linux-gnu-gcc -std=c11 -Wall -Wextra -Werror \
        -c "$root/native/alsa_sink.c" -o "$out/alsa-riscv.o"
    echo 'PASS: ALSA bridge compiled for RISC-V'
fi
