#!/usr/bin/env bash
set -Eeuo pipefail
ulimit -c 0
logo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$logo_root"
logo_out="$logo_root/out/boot-splash"
logo_kernel="$logo_root/lichee/linux-5.4"
logo_fdt="$logo_kernel/scripts/dtc/libfdt"
mkdir -p "$logo_out"
bash scripts/boot-logo.sh --default "$logo_out/diagnostic.bin"
bash scripts/boot-logo.sh --verify "$logo_out/diagnostic.bin"
if [[ -f device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg ]]; then
    bash scripts/convert-boot-logo.sh \
        device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg "$logo_out/logo.bin"
else
    cp "$logo_out/diagnostic.bin" "$logo_out/logo.bin"
fi
bash scripts/boot-logo.sh --verify "$logo_out/logo.bin"
cc -std=c11 -O2 -Wall -Wextra -Werror tests/boot-splash/artwork.c -o "$logo_out/artwork"
"$logo_out/artwork" "$logo_out/pattern.raw" "$logo_out/bottom.bmp" "$logo_out/top.bmp"
for logo_bmp in bottom top; do
    bash scripts/boot-logo.sh --bmp "$logo_out/$logo_bmp.bmp" "$logo_out/$logo_bmp.bin"
    cmp -n 2457600 -i 512:0 "$logo_out/$logo_bmp.bin" "$logo_out/pattern.raw"
done
bash scripts/boot-logo.sh --raw "$logo_out/pattern.raw" "$logo_out/pattern.bin"
cmp -n 2457600 -i 512:0 "$logo_out/pattern.bin" "$logo_out/pattern.raw"
echo 'PASS: 24/32-bit BMP row orientation, opaque pixels, channel order and raw artwork conversion'
awk '/^static void load_hz_splash\(/ { copy=1 } copy { print } copy && /^}/ {exit}' \
    lichee/brandy-2.0/spl/nboot/load_image_mmc/load_image_sdmmc.c > "$logo_out/loader.inc"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$logo_out" tests/boot-splash/loader.c -o "$logo_out/loader"
"$logo_out/loader" "$logo_out/logo.bin"
cp "$logo_out/logo.bin" "$logo_out/corrupt.bin"
printf '\001' | dd of="$logo_out/corrupt.bin" bs=1 seek=512 conv=notrunc status=none
if bash scripts/boot-logo.sh --verify "$logo_out/corrupt.bin" > "$logo_out/corrupt.log" 2>&1; then
    echo 'Corrupt pixels accepted' >&2; exit 1
fi
head -c 1024 "$logo_out/logo.bin" > "$logo_out/truncated.bin"
if bash scripts/boot-logo.sh --verify "$logo_out/truncated.bin" > "$logo_out/truncated.log" 2>&1; then
    echo 'Truncated image accepted' >&2; exit 1
fi
cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp \
    -I "$logo_kernel/include" -I "$logo_kernel/arch/riscv/boot/dts/sunxi" \
    device/config/chips/d1s/configs/hz-b500_sd/linux-5.4/board.dts > "$logo_out/board.dts"
dtc -q -@ -I dts -O dtb -o "$logo_out/board.dtb" "$logo_out/board.dts"
logo_fdt_sources=("$logo_fdt/"{fdt,fdt_ro,fdt_rw,fdt_wip,fdt_sw,fdt_empty_tree,fdt_strerror,fdt_addresses}.c)
cc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -Itests/boot-splash/include -I"$logo_fdt" tests/boot-splash/firmware.c \
    "${logo_fdt_sources[@]}" -o "$logo_out/firmware"
"$logo_out/firmware" "$logo_out/board.dtb" "$logo_out/logo.bin" "$logo_out/handoff.dtb" "$logo_out/registers.bin"
cc -std=gnu11 -O2 -Wall -Wextra -Wno-sign-compare -Wno-misleading-indentation -I"$logo_fdt" \
    tests/boot-splash/handoff.c "${logo_fdt_sources[@]}" -o "$logo_out/handoff"
"$logo_out/handoff" "$logo_out/handoff.dtb" "$logo_out/registers.bin"
echo 'PASS: boot logo format, corruption/truncation handling, OpenSBI and kernel handoff contract'
