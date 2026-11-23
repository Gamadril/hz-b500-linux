#!/usr/bin/env bash
set -Eeuo pipefail
dashboard_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$dashboard_source/../../.." && pwd)
dashboard_jobs=${PREVIEW_JOBS:-4}
pkg-config --exists sdl2 freetype2 fontconfig
if (($#)); then dashboard_projects=("$@"); else
    dashboard_projects=(dashboard hmi)
fi
for dashboard_project in "${dashboard_projects[@]}"; do
    case "$dashboard_project" in
        dashboard|hmi) ;;
        *) printf 'Unknown dashboard: %s\n' "$dashboard_project" >&2; exit 2 ;;
    esac
    cmake -S "$dashboard_source" -B "$project_root/out/car-dashboards/$dashboard_project" \
        -G Ninja -DCMAKE_BUILD_TYPE=Release -DPREVIEW_PROJECT="$dashboard_project"
    cmake --build "$project_root/out/car-dashboards/$dashboard_project" -j"$dashboard_jobs"
    if [[ $dashboard_project == hmi ]]; then
        cmake -S "$dashboard_source/tools/page-art" -B "$project_root/out/hmi-page-art/tool" -G Ninja
        cmake --build "$project_root/out/hmi-page-art/tool" -j"$dashboard_jobs"
        "$project_root/out/hmi-page-art/tool/hz-page-art" "$project_root/out/hmi-page-art/assets" "$dashboard_source/fonts/files"
    fi
done
