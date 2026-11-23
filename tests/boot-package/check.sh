#!/usr/bin/env bash
# Check the open TOC1 packer and DTB padding against the vendor tools/outputs.
# 1. Synthetic item lists: "hz-pack toc1" must match "dragonsecboot -pack".
# 2. If an SD build exists, both scripts must reproduce its boot_package.fex
#    and sunxi.fex byte for byte (BUILD overrides the image directory).
set -Eeuo pipefail
trap 'printf "FAIL at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/out/boot-package-check"
DRAGON="$ROOT/tools/pack-bintools/src/dragonsecboot"
rm -rf "$OUT"
mkdir -p "$OUT"

head -c 1 /dev/urandom > "$OUT/a.bin"
head -c 5000 /dev/urandom > "$OUT/b.bin"
head -c 20000 /dev/urandom > "$OUT/c.bin"
head -c 1024 /dev/urandom > "$OUT/d.bin"
head -c 16383 /dev/urandom > "$OUT/e.bin"
cases=(
    $'item=melis-lzma, a.bin\r\n'
    $'item=melis-lzma, b.bin\r\n'
    $'item=melis-lzma, c.bin\r\n'
    $'item=melis-lzma, d.bin\r\n'
    $'item=melis-lzma, e.bin\r\n'
    $';item=Item_TOC_name, Item_filename,\r\nitem=melis-lzma,   b.bin\r\nitem=dtb, c.bin\r\n'
    $'item=opensbi, a.bin\r\nitem=dtb, d.bin\r\nitem=scp, c.bin\r\nitem=melis, e.bin\r\nitem=foo, b.bin\r\n'
    $'item=melis-lzma, b.bin\n'
)
for i in "${!cases[@]}"; do
    printf '[package]\r\n%s' "${cases[i]}" > "$OUT/boot_package.cfg"
    (cd "$OUT"; rm -f boot_package.fex; "$DRAGON" -pack boot_package.cfg > "dragon-$i.log" 2>&1)
    mv "$OUT/boot_package.fex" "$OUT/vendor-$i.fex"
    "$ROOT/scripts/hz-pack" toc1 "$OUT/boot_package.cfg" "$OUT/open-$i.fex"
    cmp "$OUT/vendor-$i.fex" "$OUT/open-$i.fex"
done
printf '[package]\r\nitem=u-boot, a.bin\r\n' > "$OUT/boot_package.cfg"
! "$ROOT/scripts/hz-pack" toc1 "$OUT/boot_package.cfg" "$OUT/u-boot.fex" 2> /dev/null
printf 'PASS: %s synthetic TOC1 packages match dragonsecboot\n' "${#cases[@]}"

BUILD=${BUILD:-$ROOT/out/d1s-hz-b500_sd/image}
if [[ -f $BUILD/boot_package.fex && -f $BUILD/epos.img.lzma && -f $BUILD/sunxi.dtb ]]; then
    cp "$BUILD/boot_package.cfg" "$BUILD/epos.img.lzma" "$OUT/"
    "$ROOT/scripts/hz-pack" toc1 "$OUT/boot_package.cfg" "$OUT/build-boot_package.fex"
    cmp "$OUT/build-boot_package.fex" "$BUILD/boot_package.fex"
    bash "$ROOT/scripts/pad_dtb.sh" "$BUILD/sunxi.dtb" "$OUT/build-sunxi.fex"
    cmp "$OUT/build-sunxi.fex" "$BUILD/sunxi.fex"
    printf 'PASS: build boot_package.fex and sunxi.fex reproduced from %s\n' "$BUILD"
else
    printf 'SKIP: no SD build image directory at %s\n' "$BUILD"
fi
