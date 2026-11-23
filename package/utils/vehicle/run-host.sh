#!/usr/bin/env bash
set -Eeuo pipefail
vehicle_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vehicle_root=$(cd -- "$vehicle_source/../../.." && pwd)
vehicle_out="$vehicle_root/out/vehicle"
export LD_LIBRARY_PATH="$vehicle_out/host/prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
case "${1:-}" in
    vehicled|vehicle-watch|vehicle-slcan) vehicle_binary="$vehicle_out/build/$1" ;;
    ubus) vehicle_binary="$vehicle_out/host/prefix/bin/ubus" ;;
    ubusd) vehicle_binary="$vehicle_out/host/prefix/sbin/ubusd" ;;
    *) printf 'Usage: %s {vehicled|vehicle-watch|vehicle-slcan|ubus|ubusd} [ARGS...]\n' "$0" >&2; exit 2 ;;
esac
shift
exec "$vehicle_binary" "$@"
