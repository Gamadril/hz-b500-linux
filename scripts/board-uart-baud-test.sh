#!/usr/bin/env bash
# Controlled UART3 receive comparison, without persistent configuration changes.
set -Eeuo pipefail
baud_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$baud_root"
if (($#<1));then printf 'Usage: bash scripts/board-uart-baud-test.sh SESSION [BAUD ...]\nOptional BOARD_UART_RX_BAUD fixes the target baud while the listed transmitter rates vary.\n' >&2;exit 2;fi
baud_session=$(cd -- "$1" && pwd);shift
baud_rates=("$@");((${#baud_rates[@]})) || baud_rates=(115200 500000 750000)
for baud_rate in "${baud_rates[@]}";do
    [[ $baud_rate =~ ^[0-9]+$ ]] && ((baud_rate>=9600 && baud_rate<=1500000)) || exit 2
done
baud_device=$(cat "$baud_session/can-device")
baud_report="$baud_session/baud-$(date -u +%Y%m%dT%H%M%S)-$$"
baud_target="/tmp/hz-uart-baud-$(date +%s)-$$"
mkdir "$baud_report"
call() { bash "$baud_root/scripts/board-uart.sh" exec "$baud_session" "$1" "${2:-30}"; }
stop_pair="for f in '$baud_target/slcan.pid' '$baud_target/data.pid'; do if [ -s \"\$f\" ]; then p=\$(cat \"\$f\"); case \"\$p\" in ''|*[!0-9]*) exit 1;; esac; kill \"\$p\" 2>/dev/null || :; : >\"\$f\"; fi; done; sleep 1"
restore() {
    trap - EXIT INT TERM
    if [[ ! -e $baud_session/pending ]];then
        call "$stop_pair; /etc/init.d/vehicle start" 20 > "$baud_report/restore.txt" || :
    fi
    printf 'Baud comparison evidence: %s\n' "$baud_report" >&2
}
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
call "/etc/init.d/vehicle stop; sleep 1; mkdir '$baud_target'" > "$baud_report/setup.txt"
for baud_rate in "${baud_rates[@]}";do
    baud_rx=${BOARD_UART_RX_BAUD:-$baud_rate}
    [[ $baud_rx =~ ^[0-9]+$ ]] && ((baud_rx>=9600 && baud_rx<=1500000)) || exit 2
    call "$stop_pair; /usr/bin/vehicle-slcan --baud '$baud_rx' >'$baud_target/slcan.log' 2>&1 </dev/null & echo \$! >'$baud_target/slcan.pid'; /usr/bin/vehicled >'$baud_target/data.log' 2>&1 </dev/null & echo \$! >'$baud_target/data.pid'; sleep 1; cat /sys/class/tty/ttyS3/device/ctrl_info" > "$baud_report/before-$baud_rate.txt"
    bash scripts/vehicle-uart-sim.sh --device "$baud_device" --baud "$baud_rate" --speed 80 --rpm 2500 --count 20 > "$baud_report/tx-$baud_rate.stdout" 2> "$baud_report/tx-$baud_rate.log"
    call 'ubus call vehicle snapshot' > "$baud_report/vehicle-$baud_rate.json"
    call "cat /sys/class/tty/ttyS3/device/ctrl_info; cat '$baud_target/slcan.log'; cat '$baud_target/data.log'" > "$baud_report/after-$baud_rate.txt"
    jq -c --argjson baud "$baud_rate" --argjson rx "$baud_rx" '{tx_baud:$baud,rx_baud:$rx,connected,received_frames,dropped_frames,speed:.signals.speed_kph,rpm:.signals.engine_rpm}' "$baud_report/vehicle-$baud_rate.json"
done
