#!/usr/bin/env bash
# Verify the actual ext4 image, not just package selection or a staging tree.
set -Eeuo pipefail
ui_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ui_build="$ui_root/out/d1s-hz-b500_sd"
ui_image=${1:-"$ui_build/rootfs.img"}
ui_out="$ui_root/out/target-ui/image-check"
mkdir -p "$ui_out"
test -s "$ui_image"
dump() {
    : > "$ui_out/$2"
    debugfs -R "dump $1 $ui_out/$2" "$ui_image" >> "$ui_out/debugfs.log" 2>&1
    test -s "$ui_out/$2"
}
dump /usr/bin/hz-ui hz-ui
debugfs -R 'stat /usr/bin/hz-ui' "$ui_image" > "$ui_out/binary-stat.txt" 2>&1
grep -qE 'Mode: *0755' "$ui_out/binary-stat.txt"
cmp "$ui_out/hz-ui" "$ui_build/compile_dir/target/rootfs/usr/bin/hz-ui"
readelf -h "$ui_out/hz-ui" | grep -qE 'Machine:.*RISC-V'
readelf -d "$ui_out/hz-ui" | grep -qE 'Shared library:.*libmpg123.so.0'
readelf -d "$ui_out/hz-ui" | grep -qE 'Shared library:.*libout123.so.0'
! readelf -d "$ui_out/hz-ui" | grep -qE 'Shared library:.*libtplayer.so'
readelf -d "$ui_out/hz-ui" | grep -qE 'Shared library:.*libuapi.so'
readelf -d "$ui_out/hz-ui" | grep -qE 'Shared library:.*libuci.so'
strings "$ui_out/hz-ui" > "$ui_out/hz-ui.strings"
grep -qE 'UI G2D: cached 1024x600 DMA surface' "$ui_out/hz-ui.strings"
grep -qE 'home ready \(build-time artwork\)' "$ui_out/hz-ui.strings"
for ui_manifest in "$ui_root"/out/common/target-*/compile_dir/target/hz-ui-1.0/art/art.list; do
    test -s "$ui_manifest"
    while IFS= read -r ui_art_name; do
        ui_art="${ui_manifest%/*}/$ui_art_name"
        test -s "$ui_art"
        dump "/usr/share/hz-ui/art/$ui_art_name" "$ui_art_name"
        cmp "$ui_out/$ui_art_name" "$ui_art"
    done < "$ui_manifest"
done
for ui_removed_page in gallery clock; do
    debugfs -R "stat /usr/share/hz-ui/art/$ui_removed_page.hzp" "$ui_image" > "$ui_out/removed-$ui_removed_page.log" 2>&1
    ! grep -qE 'Type: regular' "$ui_out/removed-$ui_removed_page.log"
done
dump /etc/init.d/hz-ui hz-ui.init
dump /usr/libexec/hz-ui-start hz-ui-start
cmp "$ui_out/hz-ui-start" "$ui_root/package/gui/car-dashboards/files/hz-ui-start"
cmp "$ui_out/hz-ui.init" "$ui_root/package/gui/car-dashboards/files/hz-ui.init"
dump /etc/config/hz-ui hz-ui.config
cmp "$ui_out/hz-ui.config" "$ui_root/package/gui/car-dashboards/files/hz-ui.config"
debugfs -R 'stat /etc/rc.d/S09hz-ui' "$ui_image" > "$ui_out/autostart.txt" 2>&1
grep -qE 'Fast link dest:.*../init.d/hz-ui' "$ui_out/autostart.txt"
for ui_font in NotoSans-Regular.ttf fontawesome-webfont.ttf; do
    dump "/usr/share/fonts/hz-ui/$ui_font" "$ui_font"
    cmp "$ui_out/$ui_font" "$ui_root/package/gui/car-dashboards/fonts/files/$ui_font"
done
for ui_notice in noto-core.txt font-awesome.txt; do
    dump "/usr/share/doc/hz-ui-fonts/$ui_notice" "$ui_notice"
    cmp "$ui_out/$ui_notice" "$ui_root/package/gui/car-dashboards/fonts/notices/$ui_notice"
done
# The 19 MB CJK font is not part of the image.
! debugfs -R 'stat /usr/share/fonts/hz-ui/NotoSansCJK-Regular.ttc' "$ui_image" 2>/dev/null | grep -q 'Inode:'
dump /usr/bin/vehicled vehicled
dump /etc/config/vehicle vehicle.config
cmp "$ui_out/vehicle.config" "$ui_root/package/utils/vehicle/files/vehicle.config"
dump /etc/config/ab5301b ab5301b.config
cmp "$ui_out/ab5301b.config" "$ui_root/package/utils/ab5301b/files/ab5301b.config"
dump /usr/bin/mpg123 mpg123
readelf -h "$ui_out/mpg123" | grep -qE 'Machine:.*RISC-V'
if [[ -f $ui_root/package/gui/car-dashboards/media/u_98o9hlkn7r-synthwave.mp3 ]]; then
    dump /usr/share/sounds/hz-ui/u_98o9hlkn7r-synthwave.mp3 synthwave.mp3
    cmp "$ui_out/synthwave.mp3" "$ui_root/package/gui/car-dashboards/media/u_98o9hlkn7r-synthwave.mp3"
fi
if [[ -f $ui_root/package/gui/car-dashboards/media/big_buck_bunny_720p_h264.mov ]]; then
    dump /usr/share/videos/hz-ui/big_buck_bunny_720p_h264.mov bunny.mov
    cmp "$ui_out/bunny.mov" "$ui_root/package/gui/car-dashboards/media/big_buck_bunny_720p_h264.mov"
fi
dump /usr/bin/tplayerdemo tplayerdemo
readelf -h "$ui_out/tplayerdemo" | grep -qE 'Machine:.*RISC-V'
for ui_video_lib in libtplayer.so libxplayer.so libvdecoder.so libawh264.so libcdx_parser.so libadecoder.so libaw_aacdec.so libuapi.so; do
    dump "/usr/lib/$ui_video_lib" "$ui_video_lib"
    readelf -h "$ui_out/$ui_video_lib" | grep -qE 'Machine:.*RISC-V'
    cmp "$ui_out/$ui_video_lib" "$ui_build/compile_dir/target/rootfs/usr/lib/$ui_video_lib"
done
readelf --dyn-syms --wide "$ui_out/libtplayer.so" > "$ui_out/tplayer.symbols"
while read -r ui_api; do
    grep -qE "[[:space:]]$ui_api$" "$ui_out/tplayer.symbols"
done < <(grep -oE 'TPlayer[A-Za-z]+' "$ui_root/package/gui/car-dashboards/host/cedar_symbols.h")
dump /etc/cedarx.conf cedarx.conf
grep -qE '^audio_device *= *plughw:audiocodec,0' "$ui_out/cedarx.conf"
grep -qE '^vbv_buffer_size *= *2 ' "$ui_out/cedarx.conf"
for ui_audio_lib in libmpg123.so.0 libout123.so.0 libsyn123.so.0; do
    ui_audio_target=$(readlink "$ui_build/compile_dir/target/rootfs/usr/lib/$ui_audio_lib")
    dump "/usr/lib/$ui_audio_target" "$ui_audio_lib"
    readelf -h "$ui_out/$ui_audio_lib" | grep -qE 'Machine:.*RISC-V'
    cmp "$ui_out/$ui_audio_lib" "$ui_build/compile_dir/target/rootfs/usr/lib/$ui_audio_target"
done
dump /etc/init.d/vehicle vehicle.init
cmp "$ui_out/vehicle.init" "$ui_root/package/utils/vehicle/files/vehicle.init"
debugfs -R 'stat /etc/rc.d/S55vehicle' "$ui_image" > "$ui_out/vehicle-autostart.txt" 2>&1
grep -qE 'Fast link dest:.*../init.d/vehicle' "$ui_out/vehicle-autostart.txt"
e2fsck -fn "$ui_image" > "$ui_out/ext4-check.log" 2>&1
printf 'PASS: ext4 image contains matching RISC-V HMI, Cedar hardware H.264/MOV/AAC runtime and terminal player, original MOV/MP3, mpg123 libraries, startup configuration and fonts\n'
