#!/usr/bin/env bash
# Verify the compiled bus wiring and resolved driver dependencies, without hardware.
set -Eeuo pipefail
trap 'printf "FAIL at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/out/wifi-check"
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
    sdio=$(fdtget -t s "$dtb" /__symbols__ sdc1)
    pins=$(fdtget -t s "$dtb" /__symbols__ sdc1_pins_a)
    [[ $(fdtget -t s "$dtb" "$sdio" status) == okay ]]
    [[ $(fdtget -t u "$dtb" "$sdio" bus-width) == 4 ]]
    [[ $(fdtget -t u "$dtb" "$sdio" max-frequency) == 50000000 ]]
    [[ $(fdtget -t x "$dtb" "$sdio" pinctrl-0) == "$(fdtget -t x "$dtb" "$pins" phandle)" ]]
    [[ $(fdtget -t s "$dtb" "$pins" pins) == 'PG0 PG1 PG2 PG3 PG4 PG5' ]]
    [[ $(fdtget -t s "$dtb" "$pins" function) == sdc1 ]]
    for property in non-removable no-mmc no-sd cap-sdio-irq keep-power-in-suspend; do
        fdtget "$dtb" "$sdio" "$property" >/dev/null
    done
    # Preserve the separate boot-card controller and companion UART wiring.
    sdcard=$(fdtget -t s "$dtb" /__symbols__ sdc0)
    [[ $(fdtget -t s "$dtb" "$sdcard" status) == okay ]]
    fdtget "$dtb" "$sdcard" no-sdio >/dev/null
    uart=$(fdtget -t s "$dtb" /__symbols__ uart4)
    uartpins=$(fdtget -t s "$dtb" /__symbols__ hz_b500_uart4_pins)
    [[ $(fdtget -t s "$dtb" "$uart" status) == okay ]]
    [[ $(fdtget -t s "$dtb" "$uartpins" pins) == 'PE4 PE5' ]]
    if rfkill=$(fdtget -t s "$dtb" /__symbols__ rfkill 2>/dev/null); then
        [[ $(fdtget -t s "$dtb" "$rfkill" status) == disabled ]]
    fi
    if [[ "$profile" == sd ]]; then
        [[ $(fdtget -t u "$dtb" "$sdcard" max-frequency) == 15000000 ]]
        [[ $(fdtget -t s "$dtb" /chosen bootargs) == *'root=/dev/mmcblk0p1 rootfstype=ext4 rootwait rw'* ]]
    fi

    build="$OUT/kernel-$profile"
    mkdir -p "$build"
    cp "$board/linux/config-5.4" "$build/.config"
    if [[ -x "$KERNEL/scripts/kconfig/conf" ]]; then
        # The SDK builds in-tree. Calling make O= rejects that configured source
        # tree; use its built Kconfig tool with an isolated output config instead.
        (cd "$KERNEL"
            ARCH=riscv SRCARCH=riscv srctree=. CC="${CROSS_COMPILE}gcc" LD="${CROSS_COMPILE}ld" \
                KCONFIG_CONFIG="$build/.config" \
                scripts/kconfig/conf --olddefconfig Kconfig
        ) > "$build/configure.log" 2>&1
    else
        make -C "$KERNEL" O="$build" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" \
            olddefconfig > "$build/configure.log" 2>&1
    fi
    for symbol in NET PACKET UNIX INET NETDEVICES WIRELESS CFG80211 MAC80211 WLAN \
        MMC MMC_SUNXI MMC_SUNXI_V5P3X; do
        grep -Eq "^CONFIG_$symbol=y$" "$build/.config"
    done
    expected_driver=y
    [[ "$profile" != sd ]] || expected_driver=m
    grep -Eq "^CONFIG_SSV6X5X=$expected_driver$" "$build/.config"
    ! grep -Eq '^CONFIG_XR829_WLAN=[ym]$' "$build/.config"
    printf 'PASS: %s SDIO wiring, boot/companion pins and Wi-Fi driver=%s\n' "$profile" "$expected_driver"
done
printf 'Artifacts: %s\n' "$OUT"
