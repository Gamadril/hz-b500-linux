#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
: "${CEDAR_SDK_ROOT:?set CEDAR_SDK_ROOT to the matching HZ-B500 Tina SDK}"
out=${CEDAR_TEST_OUT:-"$CEDAR_SDK_ROOT/out/android-auto/cedar-native-tests"}
mkdir -p "$out"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I "$CEDAR_SDK_ROOT/package/allwinner/tina_multimedia/libcedarc/include" \
    -idirafter "$CEDAR_SDK_ROOT/lichee/linux-5.4/include" \
    "$root/tests/cedar/native.c" -ldl -lpthread -o "$out/native"
"$out/native"
if command -v riscv64-linux-gnu-gcc >/dev/null; then
    riscv64-linux-gnu-gcc -std=c11 -Wall -Wextra -Werror \
        -I "$CEDAR_SDK_ROOT/package/allwinner/tina_multimedia/libcedarc/include" \
        -idirafter "$CEDAR_SDK_ROOT/lichee/linux-5.4/include" \
        -c "$root/native/cedar_sink.c" -o "$out/cedar-riscv.o"
    echo 'PASS: native bridge compiled for RISC-V against SDK headers'
    for profile in sd; do
        build="$out/kernel-$profile"
        mkdir -p "$build"
        cp "$CEDAR_SDK_ROOT/device/config/chips/d1s/configs/hz-b500_$profile/linux/config-5.4" "$build/.config"
        bash "$CEDAR_SDK_ROOT/lichee/linux-5.4/scripts/config" --file "$build/.config" \
            --enable DISP2_SUNXI_COMPOSER
        make -C "$CEDAR_SDK_ROOT/lichee/linux-5.4" O="$build" ARCH=riscv \
            CROSS_COMPILE=riscv64-linux-gnu- olddefconfig > "$build/configure.log" 2>&1
        grep -qE '^CONFIG_DISP2_SUNXI_COMPOSER=y$' "$build/.config"
        grep -qE '^CONFIG_SYNC_FILE=y$' "$build/.config"
        echo "PASS: $profile composer and sync-file Kconfig dependencies"
    done
fi
