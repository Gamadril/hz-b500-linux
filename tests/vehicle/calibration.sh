#!/usr/bin/env bash
set -Eeuo pipefail
cal_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$cal_root"
cal_out="$cal_root/out/vehicle/calibration/tests"
mkdir -p "$cal_out"
cc -std=gnu11 -Wall -Wextra -Werror -Ipackage/utils/vehicle/src \
    tests/vehicle/calibration.c package/utils/vehicle/src/vehicle_calibration.c \
    -pthread -Wl,--wrap=ioctl -o "$cal_out/check"
timeout 20s "$cal_out/check" "$cal_out/control-$$.sock"
