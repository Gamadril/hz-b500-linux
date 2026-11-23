#!/usr/bin/env bash
set -euo pipefail
logo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$logo_root"
logo_out="$logo_root/out/boot-splash"
mkdir -p "$logo_out"
logo_asset=device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg
bash scripts/convert-boot-logo.sh "$logo_asset" "$logo_out/artwork.bin"
out/host/bin/boot-logo verify "$logo_out/artwork.bin"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -Idevice/config/chips/d1s \
    tests/boot-splash/updater.c -ljpeg -lpng -lz -o "$logo_out/updater"
"$logo_out/updater" "$logo_asset" "$logo_out/update-sd.img"
cc -std=gnu11 -O2 -Wall -Wextra -Werror tests/boot-splash/converter.c -lpng -o "$logo_out/converter-test"
"$logo_out/converter-test" generate "$logo_out/alpha.png"
bash scripts/convert-boot-logo.sh "$logo_out/alpha.png" "$logo_out/alpha.bin"
"$logo_out/converter-test" verify "$logo_out/alpha.bin"
# Exercise the public CLI against a disk image; root/boot bytes must stay intact.
head -c 574619648 "$logo_out/update-sd.img" | sha256sum > "$logo_out/prefix-before.sha256"
out/host/bin/boot-logo install-image "$logo_out/alpha.png" "$logo_out/update-sd.img"
head -c 574619648 "$logo_out/update-sd.img" | sha256sum > "$logo_out/prefix-after.sha256"
cmp "$logo_out/prefix-before.sha256" "$logo_out/prefix-after.sha256"
dd if="$logo_out/update-sd.img" of="$logo_out/installed.bin" bs=512 skip=1122304 count=4801 status=none
cmp "$logo_out/installed.bin" "$logo_out/alpha.bin"
# Rejections must leave the entire destination unchanged.
printf '\203' | dd of="$logo_out/update-sd.img" bs=1 seek=466 conv=notrunc status=none
sha256sum "$logo_out/update-sd.img" > "$logo_out/reject-before.sha256"
if out/host/bin/boot-logo install-image "$logo_asset" "$logo_out/update-sd.img" > "$logo_out/layout-reject.log" 2>&1; then
    echo 'Wrong partition type accepted' >&2; exit 1
fi
sha256sum "$logo_out/update-sd.img" > "$logo_out/reject-after.sha256"
cmp "$logo_out/reject-before.sha256" "$logo_out/reject-after.sha256"
head -c 1024 "$logo_asset" > "$logo_out/truncated.jpg"
for logo_bad in "$logo_out/truncated.jpg" "$logo_out/installed.bin"; do
    if out/host/bin/boot-logo convert "$logo_bad" "$logo_out/unwritten.bin" > "$logo_out/decode-reject.log" 2>&1; then
        echo 'Invalid image accepted' >&2; exit 1
    fi
    [[ ! -e "$logo_out/unwritten.bin" ]]
done
# The same native decoder must retain BMP channel order and either row orientation.
cc -std=c11 -O2 -Wall -Wextra -Werror tests/boot-splash/artwork.c -o "$logo_out/artwork"
"$logo_out/artwork" "$logo_out/pattern.raw" "$logo_out/bottom.bmp" "$logo_out/top.bmp"
for logo_bmp in bottom top; do
    out/host/bin/boot-logo convert "$logo_out/$logo_bmp.bmp" "$logo_out/$logo_bmp-native.bin"
    cmp -n 2457600 -i 512:0 "$logo_out/$logo_bmp-native.bin" "$logo_out/pattern.raw"
done
echo 'PASS: JPEG/PNG/BMP CLI, SD partition update, root preservation and rejection before writing'
