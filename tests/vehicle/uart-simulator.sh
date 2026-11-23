#!/usr/bin/env bash
set -Eeuo pipefail
sim_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$sim_root"
sim_out="$sim_root/out/vehicle/uart-simulator/tests"
mkdir -p "$sim_out"
bash -n scripts/vehicle-uart-sim.sh
cc -std=gnu11 -Wall -Wextra -Werror -Ipackage/utils/vehicle/src \
    tests/vehicle/uart-simulator.c package/utils/vehicle/src/vehicle.c -o "$sim_out/check"
timeout 20s "$sim_out/check" "$sim_root/scripts/vehicle-uart-sim.sh"
bash scripts/vehicle-uart-sim.sh --stdout --speed 0 --rpm 0 --time '2026-12-31 23:59:59' --count 1 > "$sim_out/zero.frames"
printf 't1A6800000000000000F0\rt0AA80000000000000000\rt2F88173B3B1FC0EA0700\r' > "$sim_out/zero.expected"
cmp "$sim_out/zero.expected" "$sim_out/zero.frames"
bash scripts/vehicle-uart-sim.sh --stdout --speed 220 --rpm 8000 --count 2 --hz 10 > "$sim_out/max.frames"
LC_ALL=C tr '\r' '\n' < "$sim_out/max.frames" > "$sim_out/max.txt"
grep -qxE 't1A6811011101110190F1' "$sim_out/max.txt"
grep -qxE 't0AA800000000007D0000' "$sim_out/max.txt"
# --lights appends 0x21A to every sample; without it the output above is unchanged.
bash scripts/vehicle-uart-sim.sh --stdout --speed 0 --rpm 0 --time '2026-12-31 23:59:59' --count 1 --lights high > "$sim_out/lights.frames"
printf 't1A6800000000000000F0\rt0AA80000000000000000\rt2F88173B3B1FC0EA0700\rt21A107\r' > "$sim_out/lights.expected"
cmp "$sim_out/lights.expected" "$sim_out/lights.frames"
for args in '--speed -1' '--speed 221' '--rpm 8001' '--hz 0' '--count -1' '--time invalid' '--speed' '--lights dim'; do
    read -r -a sim_args <<< "$args"
    if bash scripts/vehicle-uart-sim.sh --stdout --count 1 "${sim_args[@]}" > "$sim_out/invalid.frames" 2> "$sim_out/invalid.log"; then
        printf 'Unexpected success: %s\n' "$args" >&2;exit 1
    fi
    [[ ! -s "$sim_out/invalid.frames" ]]
done
printf 'PASS: zero/max values, calendar byte layout, headlight frames and invalid arguments rejected before transmission\n'
