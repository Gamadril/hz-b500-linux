#!/usr/bin/env bash
set -Eeuo pipefail
vehicle_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vehicle_root=$(cd -- "$vehicle_source/../../.." && pwd)
vehicle_run="$vehicle_root/out/vehicle/simulator"
mkdir -p "$vehicle_run"
mkdir -p "$vehicle_run/acl"
jq -n --arg user "$(id -un)" '{user:$user,publish:["vehicle"],subscribe:["vehicle"],access:{vehicle:{methods:["snapshot","inject","set"]}}}' >"$vehicle_run/acl/vehicle.json"
chmod 600 "$vehicle_run/acl/vehicle.json"
exec 9>"$vehicle_run/lock"
flock -n 9 || { printf 'Simulator already running\n' >&2; exit 1; }
vehicle_socket="$vehicle_run/ubus.sock"
vehicle_children=()
cleanup() {
    local pid
    for pid in "${vehicle_children[@]}"; do kill "$pid" 2>/dev/null || :; done
    for pid in "${vehicle_children[@]}"; do wait "$pid" 2>/dev/null || :; done
}
trap cleanup EXIT
trap 'exit 0' INT TERM
bash "$vehicle_source/run-host.sh" ubusd -A "$vehicle_run/acl" -s "$vehicle_socket" >"$vehicle_run/ubusd.log" 2>&1 &
vehicle_children+=("$!")
for attempt in {1..50}; do
    if bash "$vehicle_source/run-host.sh" ubus -s "$vehicle_socket" list >/dev/null 2>&1; then break; fi
    sleep 0.1
done
bash "$vehicle_source/run-host.sh" vehicled --simulate --ubus-socket "$vehicle_socket" "$@" >"$vehicle_run/service.log" 2>&1 &
vehicle_children+=("$!")
printf 'Simulator running. In another terminal:\nexport VEHICLE_UBUS_SOCKET=%q\nbash package/gui/car-dashboards/run-host.sh dashboard --vehicle\nbash package/gui/car-dashboards/run-host.sh hmi --page dashboard --vehicle\nbash package/gui/car-dashboards/run-host.sh pdc\n' "$vehicle_socket"
wait "${vehicle_children[1]}"
