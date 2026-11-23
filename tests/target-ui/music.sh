#!/usr/bin/env bash
set -Eeuo pipefail
music_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$music_root"
if [[ ! -f package/gui/car-dashboards/media/u_98o9hlkn7r-synthwave.mp3 ]]; then
    printf 'SKIP: optional local MP3 fixture is absent; see package/gui/car-dashboards/media/README.md\n'
    exit 0
fi
music_out="$music_root/out/target-ui/music-tests"
mkdir -p "$music_out"
music_prefix="$music_root/out/mpg123/host/prefix"
test -f "$music_prefix/include/mpg123.h"
cc -std=gnu11 -Wall -Wextra -Werror -DHZ_AUDIO_PLAYBACK \
    "-DHZ_MUSIC_INPUT=\"$music_root/package/gui/car-dashboards/media/u_98o9hlkn7r-synthwave.mp3\"" \
    -I"$music_prefix/include" -L"$music_prefix/lib" -Wl,-rpath,"$music_prefix/lib" \
    -Ipackage/gui/car-dashboards/host package/gui/car-dashboards/host/music_player.c \
    tests/target-ui/music.c -Wl,--wrap=out123_open -Wl,--wrap=out123_play \
    -lmpg123 -lout123 -lpthread -o "$music_out/music"
"$music_out/music"
