#!/usr/bin/env bash
# Capture a short standard-baud UART3 sample before SLCAN parsing.
set -Eeuo pipefail
raw_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$raw_root"
(($#==1)) || { printf 'Usage: bash scripts/board-uart-raw-test.sh SESSION\n' >&2;exit 2; }
raw_session=$(cd -- "$1" && pwd)
raw_device=$(cat "$raw_session/can-device")
raw_report="$raw_session/raw-$(date -u +%Y%m%dT%H%M%S)-$$"
raw_target="/tmp/hz-uart-raw-$(date +%s)-$$"
mkdir "$raw_report"
call() { bash scripts/board-uart.sh exec "$raw_session" "$1" "${2:-30}"; }
stop_reader="if [ -s '$raw_target/pid' ]; then p=\$(cat '$raw_target/pid'); kill \"\$p\" 2>/dev/null || :; : >'$raw_target/pid'; fi"
cleanup() {
    trap - EXIT INT TERM
    if [[ ! -e $raw_session/pending ]];then
        call "$stop_reader; /etc/init.d/vehicle start" > "$raw_report/restore.txt" || :
    fi
    printf 'Raw UART evidence: %s\n' "$raw_report" >&2
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
call "/etc/init.d/vehicle stop; sleep 1; mkdir '$raw_target'; stty -F /dev/ttyS3 raw 115200 cs8 -echo -parenb -cstopb cread clocal -crtscts -ixon -ixoff -hupcl || exit 1; cat /dev/ttyS3 >'$raw_target/bytes' 2>'$raw_target/error' & echo \$! >'$raw_target/pid'; cat /sys/class/tty/ttyS3/device/ctrl_info" > "$raw_report/before.txt"
bash scripts/vehicle-uart-sim.sh --device "$raw_device" --baud 115200 --speed 80 --rpm 2500 --count 3 > "$raw_report/tx.stdout" 2> "$raw_report/tx.log"
sleep 0.2
call "$stop_reader; wc -c '$raw_target/bytes'; hexdump -C '$raw_target/bytes'; cat '$raw_target/error'; cat /sys/class/tty/ttyS3/device/ctrl_info" > "$raw_report/capture.txt"
cat "$raw_report/capture.txt"
