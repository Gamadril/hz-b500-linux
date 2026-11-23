#!/usr/bin/env bash
set -Eeuo pipefail
case ${1:-} in
    ''|--local-only) ;;
    *) printf 'Usage: %s [--local-only]\n' "$0" >&2;exit 2 ;;
esac
ui_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ui_root"
ui_out="$ui_root/out/target-ui/tests"
ui_prefix="$ui_root/out/vehicle/host/prefix"
mkdir -p "$ui_out/acl"
cmake -S package/gui/car-dashboards/tools/page-art -B out/hmi-page-art/tool -G Ninja > "$ui_out/art-tool-config.log" 2>&1
cmake --build out/hmi-page-art/tool -j4 > "$ui_out/art-tool-build.log" 2>&1
out/hmi-page-art/tool/hz-page-art out/hmi-page-art/assets package/gui/car-dashboards/fonts/files > "$ui_out/art-export.log" 2>&1
export HZ_UI_PAGE_ART="$ui_root/out/hmi-page-art/assets"
bash tests/target-ui/clock.sh
bash tests/target-ui/settings.sh
"$ui_root/out/car-dashboards/hmi/hz-gradient-check"
cc -std=gnu11 -Wall -Wextra -Werror -DLV_CONF_INCLUDE_SIMPLE \
    -Ipackage/gui/car-dashboards/src/hmi -Ipackage/gui/car-dashboards/host \
    -Ipackage/gui/car-dashboards/target package/gui/car-dashboards/target/linux_display.c \
    tests/target-ui/backend.c -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=ioctl \
    -Wl,--wrap=mmap -Wl,--wrap=munmap -Wl,--wrap=read -o "$ui_out/backend"
"$ui_out/backend"
"$ui_root/out/car-dashboards/hmi/hz-g2d-check" > "$ui_out/g2d.log" 2>&1
grep -E '^PASS:' "$ui_out/g2d.log"
jq -n --arg user "$(id -un)" '{user:$user,publish:["vehicle"],subscribe:["vehicle"],access:{vehicle:{methods:["snapshot","inject","set"]}}}' > "$ui_out/acl/vehicle.json"
chmod 600 "$ui_out/acl/vehicle.json"
export LD_LIBRARY_PATH="$ui_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DASHBOARD_FONT="$ui_root/package/gui/car-dashboards/fonts/files/NotoSans-Regular.ttf"
export DASHBOARD_ICON_FONT="$ui_root/package/gui/car-dashboards/fonts/files/fontawesome-webfont.ttf"
export HZ_UI_DIAGNOSTICS="$ui_out/ui-diagnostic.json"
timeout 30s "$ui_root/out/car-dashboards/hmi/hz-gear-calibration-check" "$ui_out/gear-$$.sock" > "$ui_out/gear-calibration.log" 2>&1
grep -E '^PASS:' "$ui_out/gear-calibration.log"
timeout 30s "$ui_root/out/car-dashboards/hmi/hz-ui-check" --demo < /dev/null > "$ui_out/demo.log" 2>&1
grep -E '^PASS:' "$ui_out/demo.log"
HZ_UI_STATIC_CACHE=0 HZ_UI_TRANSITION_CACHE=0 timeout 30s "$ui_root/out/car-dashboards/hmi/hz-ui-check" --demo < /dev/null > "$ui_out/uncached.log" 2>&1
cmp "$ui_out/home-software.ppm" "$ui_out/home-cached.ppm"
echo 'PASS: cached launcher background preserves complete original home-screen pixels'
cc -std=gnu11 -Wall -Wextra -Werror tests/target-ui/slide-compare.c -o "$ui_out/slide-compare"
for slide_time in 80 160 240; do "$ui_out/slide-compare" "$ui_out/slide-cached-$slide_time.ppm" "$ui_out/slide-live-$slide_time.ppm"; done
for button in {0..3}; do cmp "$ui_out/press-cached-$button.ppm" "$ui_out/press-live-$button.ppm"; done
cmp "$ui_out/page-cached-settings.ppm" "$ui_out/page-live-settings.ppm"
echo 'PASS: live Settings and pressed Home icons match exactly; slide interiors match with bounded outer-column glyph clipping'
grep -E '^Home full redraw benchmark:' "$ui_out/demo.log" "$ui_out/uncached.log"
if [[ ${1:-} == --local-only ]]; then
    echo 'PASS: local rendering, in-process CAN body view, settings persistence and cached/live comparisons'
    exit 0
fi
ui_socket="$ui_out/ubus.sock"
ui_children=()
cleanup() {
    for pid in "${ui_children[@]}"; do kill "$pid" 2>/dev/null || :; done
    for pid in "${ui_children[@]}"; do wait "$pid" 2>/dev/null || :; done
}
trap cleanup EXIT
"$ui_prefix/sbin/ubusd" -A "$ui_out/acl" -s "$ui_socket" > "$ui_out/ubusd.log" 2>&1 &
ui_children+=("$!")
for attempt in {1..50}; do
    if "$ui_prefix/bin/ubus" -s "$ui_socket" list >/dev/null 2>&1; then break; fi
    sleep 0.05
done
"$ui_root/out/vehicle/build/vehicled" --simulate --no-demo --ubus-socket "$ui_socket" > "$ui_out/service.log" 2>&1 &
ui_children+=("$!")
for attempt in {1..50}; do
    if "$ui_prefix/bin/ubus" -s "$ui_socket" call vehicle snapshot >/dev/null 2>&1; then break; fi
    sleep 0.05
done
timeout 30s "$ui_root/out/car-dashboards/hmi/hz-ui-check" "$ui_socket" "$ui_prefix/bin/ubus" > "$ui_out/ui.log" 2>&1
if grep -qE '\[Error\]|glyph dsc. not found|Couldn.t create a new layer' "$ui_out/ui.log"; then
    cat "$ui_out/ui.log" >&2;exit 1
fi
grep -E '^PASS:' "$ui_out/ui.log"
jq -e '.dashboard_active and .speed.valid and .speed.value==103 and .speed.visible and (.speed.hidden|not) and .speed.angle==1100 and .rpm.valid and .rpm.value==2500 and .rpm.visible and (.rpm.hidden|not) and .rpm.angle==759' "$ui_out/dashboard-diagnostic.json" >/dev/null
echo 'PASS: UART-test diagnostics report actual visible speed/RPM widgets and angles'
