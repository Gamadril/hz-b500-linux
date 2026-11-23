#!/usr/bin/env bash
# Verify cleanup using a black color plane, without starting a video decoder.
set -Eeuo pipefail
overlay_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$overlay_root"
(($#==3)) || { printf 'Usage: bash scripts/board-uart-video-overlay-test.sh SESSION TARGET_TAP_TOOL TARGET_PLANE_FIXTURE\n' >&2;exit 2; }
overlay_session=$(cd -- "$1" && pwd)
overlay_tap=$2 overlay_plane=$3
[[ $overlay_tap =~ ^/[a-zA-Z0-9_./-]+$ && $overlay_plane =~ ^/[a-zA-Z0-9_./-]+$ ]] || exit 2
overlay_report="$overlay_session/video-overlay-$(date -u +%Y%m%dT%H%M%S)-$$"
mkdir "$overlay_report"
call() { bash scripts/board-uart.sh exec "$overlay_session" "set -e; $1" 60; }
cleanup() {
    trap - EXIT INT TERM
    if [[ ! -e $overlay_session/pending ]];then
        call "'$overlay_plane' clear; /etc/init.d/hz-ui restart; sleep 2; test -s /var/run/hz-ui.ready" > "$overlay_report/restore.txt" || :
    fi
    printf 'Video-overlay test evidence: %s\n' "$overlay_report" >&2
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
call '/etc/init.d/hz-ui restart; sleep 2; test -s /var/run/hz-ui.ready' > "$overlay_report/start.txt"
call "'$overlay_tap' /dev/input/event0 920 170; sleep 2; '$overlay_plane' create; '$overlay_tap' /dev/input/event0 850 565; sleep 2; '$overlay_plane' inspect" > "$overlay_report/back.txt"
[[ $(tail -n 1 "$overlay_report/back.txt") == VIDEO_PLANE_ENABLED=0 ]]
printf 'PASS: Back to Home clears a stale video plane without video playback\n' | tee -a "$overlay_report/results.txt"
call "'$overlay_plane' create; /etc/init.d/hz-ui restart; sleep 2; test -s /var/run/hz-ui.ready; '$overlay_plane' inspect" > "$overlay_report/restart.txt"
[[ $(tail -n 1 "$overlay_report/restart.txt") == VIDEO_PLANE_ENABLED=0 ]]
printf 'PASS: HMI restart clears an orphaned video plane\n' | tee -a "$overlay_report/results.txt"
call 'cat /sys/class/disp/disp/attr/sys; ps | grep /usr/bin/hz-ui' > "$overlay_report/final.txt"
printf '1\n' > "$overlay_report/completed"
