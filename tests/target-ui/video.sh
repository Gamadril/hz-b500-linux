#!/usr/bin/env bash
set -Eeuo pipefail
video_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$video_root"
video_out="$video_root/out/target-ui/video-tests"
mkdir -p "$video_out"
video_headers="$video_root/out/d1s-hz-b500_sd/staging_dir/target/usr/include"
cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DHZ_VIDEO_PLAYBACK \
    -isystem "$video_headers/allwinner" -isystem "$video_headers/allwinner/include" -idirafter "$video_headers" \
    -Ipackage/gui/car-dashboards/host package/gui/car-dashboards/host/video_player.c \
    tests/target-ui/video.c -Wl,--gc-sections -lpthread -o "$video_out/video"
"$video_out/video"
cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -DHZ_VIDEO_PLAYBACK -DHZ_VIDEO_DYNAMIC \
    -isystem "$video_headers/allwinner" -isystem "$video_headers/allwinner/include" -idirafter "$video_headers" \
    -Ipackage/gui/car-dashboards/host package/gui/car-dashboards/host/video_player.c \
    tests/target-ui/video.c -Wl,--gc-sections -Wl,--wrap=dlopen -Wl,--wrap=dlsym -ldl -lpthread -o "$video_out/video-dynamic"
"$video_out/video-dynamic"
