#!/usr/bin/env bash
set -Eeuo pipefail
plane_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$plane_root"
plane_out="$plane_root/out/target-ui/video-tests"
mkdir -p "$plane_out"
cc -std=gnu11 -Wall -Wextra -Werror \
    -Ipackage/gui/car-dashboards/target \
    "-DHZ_DISPLAY_HEADER=\"$plane_root/lichee/linux-5.4/include/video/sunxi_display2.h\"" \
    package/gui/car-dashboards/target/video_plane.c package/gui/car-dashboards/host/preview_guard.c tests/target-ui/video-plane.c \
    -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl -Wl,--wrap=write -Wl,--wrap=fopen -o "$plane_out/video-plane"
"$plane_out/video-plane"
