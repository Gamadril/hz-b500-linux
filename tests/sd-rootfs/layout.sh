#!/usr/bin/env bash
# Validate the actual built image, including raw boot locations and root args.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/out/d1s-hz-b500_sd"
IMAGE="${1:-$BUILD/tina_d1s-hz-b500_sd_uart0.img}"
OUT="$ROOT/out/sd-minimal/check"
KERNEL="$BUILD/compile_dir/target/linux-d1s-hz-b500_sd/linux-5.4.61"
mkdir -p "$OUT"
if [[ $# == 0 ]]; then
    mapfile -t flash_images < <(find "$BUILD" -maxdepth 1 -type f \
        -name 'tina_d1s-hz-b500_sd_*.img' -print)
    [[ ${#flash_images[@]} == 1 && ${flash_images[0]} == "$IMAGE" ]] || {
        echo 'Expected exactly one raw SD flash image with the standard filename' >&2
        exit 1
    }
fi
sfdisk --json "$IMAGE" > "$OUT/partitions.json"
awk '
    /"label": "dos"/ { dos=1 }
    /"node":/ { count++ }
    /"start":/ { value=$2; gsub(/,/, "", value); start[count]=value }
    /"size":/ { value=$2; gsub(/,/, "", value); size[count]=value }
    /"type": "83"/ { linux=count }
    /"type": "da"/ { splash=count }
    END { exit !(dos && linux==1 && splash==2 && count==2 &&
        start[1]==73728 && size[1]==1048576 && start[2]==1122304 && size[2]==6144) }
' "$OUT/partitions.json"
test "$(stat -c %s "$IMAGE")" = 577765376
dd if="$IMAGE" of="$OUT/splash.bin" bs=512 skip=1122304 count=6144 status=none
if [[ ${SD_BOOT_LOGO:-default} != none ]]; then
    bash "$ROOT/scripts/boot-logo.sh" --verify "$OUT/splash.bin"
fi
cmp -n 536870912 -i 37748736:0 "$IMAGE" "$BUILD/rootfs.img"
cmp -n "$(stat -Lc %s "$BUILD/image/boot0_sdcard.fex")" -i 8192:0 \
    "$IMAGE" "$BUILD/image/boot0_sdcard.fex"
for offset in 12582912 16793600; do
    cmp -n "$(stat -Lc %s "$BUILD/image/boot_package.fex")" -i "$offset:0" \
        "$IMAGE" "$BUILD/image/boot_package.fex"
done
cmp -n "$(stat -c %s "$KERNEL/arch/riscv/boot/Image")" -i 2097152:0 \
    "$BUILD/image/epos.img" "$KERNEL/arch/riscv/boot/Image"
cmp -n "$(stat -c %s "$BUILD/image/sunxi.fex")" -i 1048576:0 \
    "$BUILD/image/epos.img" "$BUILD/image/sunxi.fex"
grep -qE '^CONFIG_CMDLINE=".*root=/dev/mmcblk0p1 rootfstype=ext4 rootwait rw"$' "$KERNEL/.config"
grep -qE '^CONFIG_CMDLINE_FORCE=y$' "$KERNEL/.config"
! grep -qE '^CONFIG_FRAMEBUFFER_CONSOLE=y$|^CONFIG_CMDLINE=.*console=tty0' "$KERNEL/.config"
# Do not use grep -q here: strings must finish when pipefail is set.
strings "$KERNEL/arch/riscv/boot/Image" | grep 'root=/dev/mmcblk0p1 rootfstype=ext4 rootwait rw' > "$OUT/kernel-root.txt"
fdtget "$BUILD/image/sunxi.fex" /chosen bootargs > "$OUT/dtb-root.txt"
grep -qE 'root=/dev/mmcblk0p1 rootfstype=ext4 rootwait rw' "$OUT/dtb-root.txt"
debugfs -R "dump /usr/share/sounds/sample-1mb.wav $OUT/sample.wav" "$BUILD/rootfs.img" > "$OUT/rootfs-files.log" 2>&1
cmp "$OUT/sample.wav" "$ROOT/target/allwinner/d1s-hz-b500_sd/base-files/usr/share/sounds/sample-1mb.wav"
debugfs -R "dump /usr/bin/ab5301b $OUT/ab5301b" "$BUILD/rootfs.img" >> "$OUT/rootfs-files.log" 2>&1
cmp "$OUT/ab5301b" "$BUILD/compile_dir/target/rootfs/usr/bin/ab5301b"
e2fsck -fn "$BUILD/rootfs.img" > "$OUT/ext4-check.log" 2>&1
printf 'PASS: ext4 p1, splash p2, UART-only root args, boot0/TOC1 copies, payload, ext4, audio sample/controller\n'
