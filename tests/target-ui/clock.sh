#!/usr/bin/env bash
set -Eeuo pipefail
clock_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$clock_root"
clock_out="$clock_root/out/target-ui/clock-tests"
mkdir -p "$clock_out"
clock_flags=(-std=gnu11 -ffunction-sections -fdata-sections -DLV_CONF_INCLUDE_SIMPLE
    -DHZ_TARGET_UI -DHZ_NATIVE_CLOCK -Ipackage/gui/car-dashboards/src/hmi
    -Ipackage/gui/car-dashboards/src/hmi/lvgl -Ipackage/gui/car-dashboards/target
    -Ipackage/gui/car-dashboards/host -Ipackage/utils/vehicle/src)
cc "${clock_flags[@]}" -Dmain=hz_target_main -c package/gui/car-dashboards/target/main.c -o "$clock_out/main.o"
cc "${clock_flags[@]}" tests/target-ui/clock.c \
    package/gui/car-dashboards/src/hmi/lvgl/src/hal/lv_hal_tick.c "$clock_out/main.o" \
    -Wl,--gc-sections -o "$clock_out/clock"
"$clock_out/clock"
cc "${clock_flags[@]}" tests/target-ui/startup.c \
    -Wl,--gc-sections -Wl,--wrap=fopen -Wl,--wrap=unlink -Wl,--wrap=close -o "$clock_out/startup"
"$clock_out/startup" "$clock_out/first-frame.ready"
