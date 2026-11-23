#!/usr/bin/env bash
# Read-only data checks; internal loopback is enabled only while UART3 is open.
set -Eeuo pipefail
line_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$line_root"
(($#==3)) || { printf 'Usage: bash scripts/board-uart-line-test.sh SESSION TARGET_SERIAL_HELPER loopback|external\n' >&2;exit 2; }
line_session=$(cd -- "$1" && pwd);line_tool=$2;line_mode=$3
[[ $line_tool =~ ^/[a-zA-Z0-9_./-]+$ && ( $line_mode == loopback || $line_mode == external ) ]] || exit 2
line_report="$line_session/line-$line_mode-$(date -u +%Y%m%dT%H%M%S)-$$"
line_target="/tmp/hz-uart-line-$(date +%s)-$$"
mkdir "$line_report"
call() { bash scripts/board-uart.sh exec "$line_session" "$1" "${2:-30}"; }
stop_reader="if [ -s '$line_target/reader.pid' ]; then p=\$(cat '$line_target/reader.pid'); kill \"\$p\" 2>/dev/null || :; : >'$line_target/reader.pid'; fi"
cleanup() {
    trap - EXIT INT TERM
    if [[ ! -e $line_session/pending ]];then
        call "if grep -q 'opened' '$line_target/helper.log'; then echo disable > /sys/class/tty/ttyS3/device/loopback; fi; $stop_reader; if [ -s '$line_target/helper.pid' ]; then kill \$(cat '$line_target/helper.pid') 2>/dev/null || :; fi; sleep 1; /etc/init.d/vehicle start" > "$line_report/restore.txt" || :
    fi
    printf 'UART line evidence: %s\n' "$line_report" >&2
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
call "/etc/init.d/vehicle stop; sleep 1; mkdir '$line_target'; mkfifo '$line_target/input'; exec 5<>'$line_target/input'; '$line_tool' /dev/ttyS3 750000 <&5 >'$line_target/helper.log' 2>&1 & echo \$! >'$line_target/helper.pid'; sleep 1; grep -q opened '$line_target/helper.log' || exit 1; cat /dev/ttyS3 >'$line_target/bytes' 2>'$line_target/reader.log' & echo \$! >'$line_target/reader.pid'; cat /sys/class/tty/ttyS3/device/ctrl_info" > "$line_report/before.txt"
if [[ $line_mode == loopback ]];then
    printf 'UART3-LOOPBACK-750000-0123456789\r\n' > "$line_report/expected.bin"
    call "echo enable >/sys/class/tty/ttyS3/device/loopback; printf 'UART3-LOOPBACK-750000-0123456789\\r\\n' >'$line_target/input'; sleep 1; echo disable >/sys/class/tty/ttyS3/device/loopback" > "$line_report/transmit.txt"
else
    line_device=$(cat "$line_session/can-device")
    bash scripts/vehicle-uart-sim.sh --device "$line_device" --baud 750000 --speed 80 --rpm 2500 --count 3 > "$line_report/tx.stdout" 2> "$line_report/tx.log"
    sleep 0.2
fi
call "$stop_reader; cat '$line_target/bytes'" > "$line_report/received.bin"
call "wc -c '$line_target/bytes'; hexdump -C '$line_target/bytes'; cat /sys/class/tty/ttyS3/device/ctrl_info; cat '$line_target/helper.log'" > "$line_report/after.txt"
cat "$line_report/after.txt"
if [[ $line_mode == loopback ]];then cmp "$line_report/expected.bin" "$line_report/received.bin";printf 'PASS: UART3 internal loopback at 750000 baud\n';fi
