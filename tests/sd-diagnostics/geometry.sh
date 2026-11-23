#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
helper="$ROOT/scripts/sd_card_sectors.sh"
default="$ROOT/device/config/chips/d1s/configs/hz-b500_sd/sd-card-sectors"
unset SD_CARD_SECTORS
test "$(bash "$helper" "$default")" = 7698432
test "$(SD_CARD_SECTORS=15269888 bash "$helper" "$default")" = 15269888
test "$(SD_CARD_SECTORS=4294967295 bash "$helper" "$default")" = 4294967295
for value in '' 0 -1 077 2097151 4294967296 99999999999999999999 '1+7698432' invalid; do
    if SD_CARD_SECTORS="$value" bash "$helper" "$default" >/dev/null 2>&1; then
        printf 'FAIL: invalid capacity accepted: %s\n' "$value" >&2
        exit 1
    fi
done
printf 'PASS: SD card capacity default, overrides and invalid input rejection\n'
