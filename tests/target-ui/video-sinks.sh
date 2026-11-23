#!/usr/bin/env bash
set -Eeuo pipefail
video_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$video_root"
video_out="$video_root/out/target-ui/video-tests"
video_source="$video_root/package/allwinner/tina_multimedia/tplayer"
video_headers="$video_root/out/d1s-hz-b500_sd/staging_dir/target/usr/include"
mkdir -p "$video_out"
awk '/^int TSoundDeviceWrite\(/ {active=1} active {print} active && /^}$/ {exit}' \
    "$video_source/awsink/tsound_ctrl.c" > "$video_out/sound-write.inc"
awk '/^int LayerDisplayOnoff\(/ {active=1} active {print} active && /^}$/ {exit}' \
    "$video_source/awsink/tlayer_ctrl.c" > "$video_out/layer-visible.inc"
cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -isystem "$video_headers/allwinner" -isystem "$video_headers/allwinner/include" -idirafter "$video_headers" \
    -isystem "$video_source" -isystem "$video_source/awsink" -I"$video_out" \
    tests/target-ui/video-sinks.c -Wl,--gc-sections -lpthread -o "$video_out/video-sinks"
"$video_out/video-sinks"
video_port="$video_root/package/allwinner/libuapi/src"
awk '/^#ifdef __SUNXI_DISPLAY2__/ {active=1} active && /^#ifdef SUNXI_DISPLAY_GPU/ {exit} active && !/^#include/ {print}' \
    "$video_port/videoOutPort.c" > "$video_out/port-zorder.inc"
for video_operation in Zorder Enable; do
    awk -v signature="int DispSet$video_operation(" 'index($0,signature)==1 {active=1} active {print} active && /^}$/ {exit}' \
        "$video_port/videoOutPort.c" > "$video_out/port-set-${video_operation,,}.inc"
done
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-variable \
    -D__SUNXI_DISPLAY2__ -DCONF_KERNEL_IOMMU -DCONF_KERNEL_VERSION_5_4 \
    -I"$video_port" -I"$video_port/include" -I"$video_out" tests/target-ui/video-port.c -o "$video_out/video-port"
"$video_out/video-port"
