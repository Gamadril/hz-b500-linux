#!/usr/bin/env bash
set -Eeuo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
dashboard_runner="$project_root/package/gui/car-dashboards/run-host.sh"
dashboard_logs="$project_root/out/car-dashboards/logs"
mkdir -p "$dashboard_logs"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software
check_dashboard() {
    local project=$1 page=${2:-} result=0
    local log="$dashboard_logs/$project-smoke${page:+-$page}.log"
    local args=("$project")
    [[ -z "$page" ]] || args+=(--page "$page")
    timeout --kill-after=2s 6s bash "$dashboard_runner" "${args[@]}" \
        </dev/null >"$log" 2>&1 || result=$?
    if [[ $result != 124 ]]; then
        printf 'FAIL: %s page %s (exit %s), see %s\n' "$project" "${page:-default}" "$result" "$log" >&2
        return 1
    fi
    if grep -qE '\[Error\]|Segmentation fault|Assertion.*failed|lv_tick_inc\(\) is not called|glyph dsc. not found|Couldn.t create a new layer' "$log"; then
        printf 'FAIL: LVGL/runtime errors in %s\n' "$log" >&2
        return 1
    fi
    if ! grep -qE 'System fonts ready: Latin, CJK and icons' "$log"; then
        printf 'FAIL: %s did not load system fonts\n' "$project" >&2
        return 1
    fi
    if ! grep -qE 'SDL clock running; rendered [1-9][0-9]* areas' "$log"; then
        printf 'FAIL: %s did not render with an advancing clock\n' "$project" >&2
        return 1
    fi
    printf 'PASS: %s page %s\n' "$project" "${page:-default}"
}
check_dashboard dashboard
check_dashboard hmi
for dashboard_page in dashboard music video settings; do
    check_dashboard hmi "$dashboard_page"
done
