#!/usr/bin/env bash
set -Eeuo pipefail
dashboard_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$dashboard_source/../../.." && pwd)
case "${1:---list}" in
    --list)
        printf '%s\n' \
            'dashboard   800x480 dashboard and simulated gauges' \
            'hmi         1024x600 launcher, dashboard and media UI'
        printf '%s\n' 'pdc         Parking sensor view using the vehicle service'
        exit 0 ;;
    dashboard|hmi|pdc) dashboard_project=$1; shift ;;
    *) printf 'Usage: %s {dashboard|hmi|pdc|--list} [--page NAME] [--vehicle|--demo] [--ubus-socket PATH]\n' "$0" >&2; exit 2 ;;
esac
dashboard_binary="$project_root/out/car-dashboards/$dashboard_project/preview"
if [[ "$dashboard_project" == pdc ]]; then
    dashboard_project=hmi
    dashboard_binary="$project_root/out/car-dashboards/hmi/pdc-preview"
fi
if [[ ! -x "$dashboard_binary" ]]; then
    printf 'Build first: bash %s/build-host.sh %s\n' "$dashboard_source" "$dashboard_project" >&2
    exit 1
fi
export SDL_RENDER_DRIVER=${SDL_RENDER_DRIVER:-software}
export HZ_UI_PAGE_ART=${HZ_UI_PAGE_ART:-"$project_root/out/hmi-page-art/assets"}
if [[ -d "$project_root/out/vehicle/host/prefix/lib" ]]; then
    export LD_LIBRARY_PATH="$project_root/out/vehicle/host/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
cd -- "$dashboard_source/src/$dashboard_project"
exec "$dashboard_binary" "$@"
