#!/usr/bin/env bash
# Rebuild selected userspace, regenerate rootfs and replace the single SD image.
set -Eo pipefail
prep_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$prep_root"
prep_out="$prep_root/out/hardware-test/preparation"
mkdir -p "$prep_out"
source build/envsetup.sh > "$prep_out/environment.log" 2>&1
lunch d1s_hz_b500_sd-tina >> "$prep_out/environment.log" 2>&1
set -Eeuo pipefail
grep -qE '^CONFIG_TARGET_BOARD="d1s-hz-b500_sd"$' .config
printf 'Building vehicle, HMI, BusyBox transfer tools and opkg...\n'
make package/busybox/compile package/opkg/compile package/vehicle/compile package/gui/car-dashboards/compile \
    -j4 V=s > "$prep_out/packages.log" 2>&1
printf 'Installing packages and generating the SD root filesystem...\n'
make package/install -j4 V=s > "$prep_out/install.log" 2>&1
make target/allwinner/install -j4 V=s > "$prep_out/rootfs.log" 2>&1
printf 'Packing the SD image...\n'
p > "$prep_out/pack.log" 2>&1
for prep_check in tests/sd-rootfs/layout.sh tests/target-ui/image.sh tests/boot-time/image.sh tests/board-uart/image.sh;do
    bash "$prep_check" > "$prep_out/$(basename "$(dirname "$prep_check")")-check.log" 2>&1
    cat "$prep_out/$(basename "$(dirname "$prep_check")")-check.log"
done
sha256sum out/d1s-hz-b500_sd/tina_d1s-hz-b500_sd_uart0.img > "$prep_out/image.sha256"
date -u +%Y-%m-%dT%H:%M:%SZ > "$prep_out/prepared-at"
cat "$prep_out/image.sha256"
