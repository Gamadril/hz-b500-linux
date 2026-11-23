#!/usr/bin/env bash
# Build/run the host image converter; generated files stay under out/.
set -euo pipefail
logo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
logo_bin="$logo_root/out/host/bin/hz-boot-logo"
logo_src="$logo_root/tools/hz-boot-logo/main.c"
logo_header="$logo_root/device/config/chips/d1s/hz-b500-splash.h"
logo_font="$logo_root/lichee/brandy-2.0/opensbi/platform/generic/allwinner/hz-b500-font-8x16.h"
mkdir -p "${logo_bin%/*}"
if [[ ! -x "$logo_bin" || "$logo_src" -nt "$logo_bin" || "$logo_header" -nt "$logo_bin" || "$logo_font" -nt "$logo_bin" ]]; then
    "${HOSTCC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "$logo_src" -o "$logo_bin"
fi
exec "$logo_bin" "$@"
