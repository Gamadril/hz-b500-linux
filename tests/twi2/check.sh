#!/usr/bin/env bash
# Check the actual compiled DT and kernel configuration for MFi bus access.
set -Eeuo pipefail
trap 'printf "FAIL at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/out/twi2-check"
KERNEL="$ROOT/lichee/linux-5.4"
CROSS_COMPILE=${CROSS_COMPILE:-riscv64-linux-gnu-}
mkdir -p "$OUT/dtb"
for profile in sd; do
    board="$ROOT/device/config/chips/d1s/configs/hz-b500_$profile"
    dtb="$OUT/dtb/$profile.dtb"
    cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
        -I "$KERNEL/include" -I "$KERNEL/arch/riscv/boot/dts/sunxi" \
        "$board/linux-5.4/board.dts" > "$OUT/dtb/$profile.dts"
    dtc -q -@ -I dts -O dtb -o "$dtb" "$OUT/dtb/$profile.dts"
    bus=$(fdtget -t s "$dtb" /__symbols__ twi2)
    pins=$(fdtget -t s "$dtb" /__symbols__ twi2_pins_a)
    sleep_pins=$(fdtget -t s "$dtb" /__symbols__ twi2_pins_b)
    [[ $(fdtget -t s "$dtb" /aliases twi2) == "$bus" ]]
    [[ $(fdtget -t x "$dtb" "$bus" reg) == '0 2502800 0 400' ]]
    [[ $(fdtget -t s "$dtb" "$bus" compatible) == allwinner,sun20i-twi ]]
    [[ $(fdtget -t s "$dtb" "$bus" status) == okay ]]
    [[ $(fdtget -t u "$dtb" "$bus" clock-frequency) == 400000 ]]
    [[ $(fdtget -t s "$dtb" "$bus" pinctrl-names) == 'default sleep' ]]
    [[ $(fdtget -t x "$dtb" "$bus" pinctrl-0) == "$(fdtget -t x "$dtb" "$pins" phandle)" ]]
    [[ $(fdtget -t x "$dtb" "$bus" pinctrl-1) == "$(fdtget -t x "$dtb" "$sleep_pins" phandle)" ]]
    [[ $(fdtget -t s "$dtb" "$pins" pins) == 'PE12 PE13' ]]
    [[ $(fdtget -t s "$dtb" "$pins" function) == twi2 ]]
    fdtget "$dtb" "$pins" bias-pull-up >/dev/null
    [[ $(fdtget -t s "$dtb" "$sleep_pins" pins) == 'PE12 PE13' ]]
    [[ $(fdtget -t s "$dtb" "$sleep_pins" function) == gpio_in ]]
    # Shared template pin groups must not take PE12/PE13 from TWI2.
    for label in daudio0 gmac0; do
        if node=$(fdtget -t s "$dtb" /__symbols__ "$label" 2>/dev/null); then
            [[ $(fdtget -t s "$dtb" "$node" status) == disabled ]]
        fi
    done
    for label in twi1 uart4 sdc1; do
        node=$(fdtget -t s "$dtb" /__symbols__ "$label")
        [[ $(fdtget -t s "$dtb" "$node" status) == okay ]]
    done

    build="$OUT/kernel-$profile"
    mkdir -p "$build"
    cp "$board/linux/config-5.4" "$build/.config"
    make -C "$KERNEL" O="$build" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" \
        olddefconfig > "$build/configure.log" 2>&1
    for symbol in I2C I2C_SUNXI I2C_CHARDEV PINCTRL_SUN8IW20; do
        grep -qE "^CONFIG_$symbol=y$" "$build/.config"
    done
    printf 'PASS: %s TWI2 alias, PE12/PE13, pin conflicts and built-in I2C dependencies\n' "$profile"
done
printf 'Artifacts: %s\n' "$OUT"
