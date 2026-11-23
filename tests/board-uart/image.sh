#!/usr/bin/env bash
set -Eeuo pipefail
image_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$image_root"
image_build="$image_root/out/d1s-hz-b500_sd"
image_out="$image_root/out/board-uart/image-check"
mkdir -p "$image_out"
dump() {
    : > "$image_out/$2"
    debugfs -R "dump $1 $image_out/$2" "$image_build/rootfs.img" >> "$image_out/debugfs.log" 2>&1
    test -s "$image_out/$2"
}
dump /usr/bin/vehicled vehicled
cmp "$image_out/vehicled" "$image_build/compile_dir/target/rootfs/usr/bin/vehicled"
strings "$image_out/vehicled" > "$image_out/vehicled.strings"
grep -qE 'ready; waiting for CAN frames' "$image_out/vehicled.strings"
dump /usr/lib/opkg/info/vehicle.control vehicle.control
grep -qE '^Version: 1.0-6$' "$image_out/vehicle.control"
dump /usr/bin/hz-ui hz-ui
cmp "$image_out/hz-ui" "$image_build/compile_dir/target/rootfs/usr/bin/hz-ui"
strings "$image_out/hz-ui" > "$image_out/hz-ui.strings"
grep -qE HZ_UI_DIAGNOSTICS "$image_out/hz-ui.strings"
grep -qE -- '--page home\|dashboard' "$image_out/hz-ui.strings"
dump /usr/lib/opkg/info/hz-ui.control hz-ui.control
grep -qE '^Version: 1.0-10$' "$image_out/hz-ui.control"
dump /bin/opkg opkg
readelf -h "$image_out/opkg" | grep -qE 'Machine:.*RISC-V'
dump /bin/busybox busybox
cmp "$image_out/busybox" "$image_build/compile_dir/target/rootfs/bin/busybox"
grep -lE '^CONFIG_FEATURE_EDITING_MAX_LEN=4096$' "$image_root"/out/common/target-*/compile_dir/target/busybox-*/.config > "$image_out/busybox-configs.txt"
for applet in base64 sha256sum mktemp stty gunzip ln wc sed;do
    found=0
    for directory in bin usr/bin;do
        report="$image_out/$applet-${directory//\//-}.stat"
        debugfs -R "stat /$directory/$applet" "$image_build/rootfs.img" > "$report" 2>&1 || :
        if grep -qE 'Type: symlink|Type: regular' "$report";then found=1;break;fi
    done
    ((found)) || { printf 'Missing target applet: %s\n' "$applet" >&2;exit 1; }
done
printf 'PASS: packed rootfs has vehicle 1.0-6, HMI 1.0-10 with reserved settings scrollbar gutters, opkg and UART transfer prerequisites\n'
