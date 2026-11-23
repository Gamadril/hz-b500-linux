#!/usr/bin/env bash
set -Eeuo pipefail
art_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$art_root/out/hmi-page-art/tests"
cc -std=gnu11 -Wall -Wextra -Werror -I"$art_root/package/gui/car-dashboards/host" \
    "$art_root/tests/target-ui/art.c" "$art_root/package/gui/car-dashboards/host/art_codec.c" -o "$art_root/out/hmi-page-art/tests/art"
"$art_root/out/hmi-page-art/tests/art"
