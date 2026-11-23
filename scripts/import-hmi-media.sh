#!/usr/bin/env bash
# Restore optional reference media from explicit local paths, with hash checks.
set -Eeuo pipefail
media_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
media_dir="$media_root/package/gui/car-dashboards/media"
media_audio=u_98o9hlkn7r-synthwave.mp3
media_video=big_buck_bunny_720p_h264.mov
media_audio_hash=afb92075295597e20ca407b35577c359442edd63753479768b8b642391578167
media_video_hash=45c8bafeb9a53df7f491198d2e71529701bcf1cd51805782089fac1d32869f9b
media_check=0
usage() { printf 'Usage: bash scripts/import-hmi-media.sh [--audio FILE] [--video FILE] [--check]\n'; }
verify() {
    local actual
    [[ -f $1 ]] || { printf 'Missing media: %s\n' "$1" >&2;return 1; }
    actual=$(sha256sum -- "$1");actual=${actual%% *}
    [[ $actual == "$2" ]] || { printf 'Reference media hash mismatch: %s\n' "$1" >&2;return 1; }
}
import_media() {
    local source=$1 name=$2 expected=$3 destination="$media_dir/$2"
    verify "$source" "$expected"
    if [[ -e $destination ]];then
        verify "$destination" "$expected"
    else
        cp --no-clobber -- "$source" "$destination"
        verify "$destination" "$expected"
    fi
    printf 'OK: %s\n' "$name"
}
(($#)) || { usage >&2;exit 2; }
while (($#));do
    case $1 in
        --audio) (($#>=2)) || { usage >&2;exit 2; };import_media "$2" "$media_audio" "$media_audio_hash";shift 2;;
        --video) (($#>=2)) || { usage >&2;exit 2; };import_media "$2" "$media_video" "$media_video_hash";shift 2;;
        --check) media_check=1;shift;;
        -h|--help) usage;exit 0;;
        *) usage >&2;exit 2;;
    esac
done
if ((media_check));then
    for media_kind in audio video;do
        if [[ $media_kind == audio ]];then media_name=$media_audio;media_hash=$media_audio_hash;else media_name=$media_video;media_hash=$media_video_hash;fi
        if [[ -f $media_dir/$media_name ]];then verify "$media_dir/$media_name" "$media_hash";printf 'OK: %s\n' "$media_name";
        else printf 'OPTIONAL: %s is not installed locally\n' "$media_name";fi
    done
fi
