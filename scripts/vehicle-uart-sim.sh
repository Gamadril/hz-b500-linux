#!/usr/bin/env bash
# Send bridge-format SLCAN frames to F133 UART3 from a host serial adapter.
set -Eeuo pipefail
export LC_ALL=C
export TZ=${TZ:-Europe/Berlin}
sim_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sim_device=/dev/ttyUSB0
sim_baud=750000
sim_speed=sweep sim_rpm=sweep sim_time=now sim_count=0 sim_hz=50 sim_stdout=0 sim_lights=
usage() {
    cat <<'HELP'
Usage: bash scripts/vehicle-uart-sim.sh [options]
  --device PATH      Serial adapter (default /dev/ttyUSB0, 750000 baud 8N1)
  --baud BAUD        Diagnostic UART rate, 9600..1500000 (default 750000)
  --speed KMH        Fixed speed, 0..220 (default: sweep 0..160 and back)
  --rpm RPM          Fixed RPM, 0..8000 (default: 800 + speed * 25)
  --lights STATE     Also send 0x21A headlights: off, low or high (default: not sent)
  --time DATE        Starting local calendar time (default: current host time)
  --hz HZ            Samples per second, 1..50 (default 10)
  --count N          Stop after N samples; 0 runs until Ctrl-C (default 0)
  --stdout           Emit CR-terminated SLCAN bytes without opening a device
  --help             Show this help

Each sample contains speed counters 0x1A6, RPM 0x0AA and calendar time 0x2F8,
plus headlights 0x21A with --lights (low or high drive night mode and LCD dimming).
Connect adapter TX to F133 PB7 / UART3 RX, and common GND.
Keep the target vehicle service running in its normal live mode.
Calendar time uses TZ (Europe/Berlin by default) and advances during the run.
The live vehicle service may set system time from the first calendar sample.
No wheel actions or PDC activation are generated. Ctrl-C stops transmission.
HELP
}
fail() { printf 'vehicle-uart-sim: %s\n' "$*" >&2;exit 1; }
number() {
    [[ $2 =~ ^[0-9]{1,8}$ ]] || fail "$1 must be an integer"
    local n=$((10#$2))
    ((n>=$3 && n<=$4)) || fail "$1 must be in $3..$4"
    printf '%d' "$n"
}
while (($#)); do
    case $1 in
        --help|-h) usage;exit 0;;
        --stdout) sim_stdout=1;shift;continue;;
        --device|--baud|--speed|--rpm|--lights|--time|--hz|--count)
            (($#>=2)) || fail "$1 needs a value";;
        *) fail "Unknown option: $1 (use --help)";;
    esac
    case $1 in
        --device) sim_device=$2;;
        --baud) sim_baud=$(number baud "$2" 9600 1500000);;
        --speed) sim_speed=$(number speed "$2" 0 220);;
        --rpm) sim_rpm=$(number rpm "$2" 0 8000);;
        --lights) case $2 in off) sim_lights=00;; low) sim_lights=04;; high) sim_lights=07;;
            *) fail "lights must be off, low or high";; esac;;
        --time) sim_time=$2;;
        --hz) sim_hz=$(number hz "$2" 1 50);;
        --count) sim_count=$(number count "$2" 0 10000000);;
    esac
    shift 2
done
sim_epoch=$(date --date="$sim_time" +%s) || fail 'Invalid starting date/time'
sim_year=$(date --date="@$sim_epoch" +%Y)
((10#$sim_year>=1 && 10#$sim_year<=2500)) || fail 'Calendar year must be in 1..2500'
printf -v sim_interval '%d.%03d' "$((1000/sim_hz/1000))" "$((1000/sim_hz%1000))"
emit() {
    local step=0 speed rpm raw year month day hour minute second elapsed started
    local distance=0 clock=0 fraction=0 ticks clock_fraction=0
    started=$(date +%s)
    while ((sim_count==0 || step<sim_count)); do
        speed=$sim_speed
        if [[ $speed == sweep ]]; then
            speed=$((step/sim_hz*4%320))
            ((speed<=160)) || speed=$((320-speed))
        fi
        rpm=$sim_rpm
        [[ $rpm != sweep ]] || rpm=$((800+speed*25))
        printf 't1A68%02X%02X%02X%02X%02X%02X%02X%02X\r' \
            "$((distance&255))" "$((distance>>8))" "$((distance&255))" "$((distance>>8))" \
            "$((distance&255))" "$((distance>>8))" "$((clock&255))" "$((0xf0|(clock>>8)))"
        fraction=$((fraction+speed*20000000))
        distance=$(((distance+fraction/(sim_hz*1609344))&65535))
        fraction=$((fraction%(sim_hz*1609344)))
        clock_fraction=$((clock_fraction+4000));ticks=$((clock_fraction/sim_hz))
        clock_fraction=$((clock_fraction%sim_hz));clock=$(((clock+ticks)&4095))
        raw=$((rpm*4))
        printf 't0AA800000000%02X%02X0000\r' "$((raw&255))" "$((raw>>8))"
        elapsed=$(($(date +%s)-started))
        read -r year month day hour minute second < <(date --date="@$((sim_epoch+elapsed))" '+%Y %-m %-d %-H %-M %-S')
        year=$((10#$year))
        printf 't2F88%02X%02X%02X%02X%02X%02X%02X00\r' \
            "$hour" "$minute" "$second" "$day" "$((month<<4))" "$((year&255))" "$((year>>8))"
        [[ -z $sim_lights ]] || printf 't21A1%s\r' "$sim_lights"
        if ((step%sim_hz==0)); then
            printf 'SIMULATED: speed %d km/h, RPM %d, %04d-%02d-%02d %02d:%02d:%02d (%s)\n' \
                "$speed" "$rpm" "$year" "$month" "$day" "$hour" "$minute" "$second" "$TZ" >&2
        fi
        step=$((step+1))
        ((sim_count==0 || step<sim_count)) || break
        sleep "$sim_interval"
    done
}
if ((sim_stdout)); then emit;exit;fi
[[ -c $sim_device ]] || fail "Serial device is unavailable: $sim_device"
sim_out="$sim_root/out/vehicle/uart-simulator"
sim_source="$sim_root/package/utils/vehicle/tools/serial-send.c"
mkdir -p "$sim_out"
# Linux's nonstandard 750000 baud needs termios2/BOTHER, unavailable in stty.
cc -std=gnu11 -O2 -Wall -Wextra -Werror "$sim_source" -o "$sim_out/serial-send.$$"
mv -- "$sim_out/serial-send.$$" "$sim_out/serial-send"
printf 'Sending SLCAN to %s at %s baud, 8N1; Ctrl-C stops.\n' "$sim_device" "$sim_baud" >&2
sim_run=$(mktemp -d "$sim_out/run.XXXXXXXX")
mkfifo "$sim_run/data"
sim_children=()
cleanup() {
    trap - EXIT INT TERM
    local pid
    for pid in "${sim_children[@]}";do kill "$pid" 2>/dev/null || :;done
    for pid in "${sim_children[@]}";do wait "$pid" 2>/dev/null || :;done
    rm -f -- "$sim_run/data"
    rmdir -- "$sim_run"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
emit > "$sim_run/data" &
sim_children+=("$!")
"$sim_out/serial-send" "$sim_device" "$sim_baud" < "$sim_run/data" &
sim_children+=("$!")
wait "${sim_children[1]}"
wait "${sim_children[0]}"
