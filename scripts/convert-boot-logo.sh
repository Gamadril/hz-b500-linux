#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Host wrapper for the same decoder/converter installed on the target.
set -euo pipefail
logo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
logo_bin="$logo_root/out/host/bin/boot-logo"
logo_src="$logo_root/package/utils/boot-logo/src/boot-logo.c"
logo_include="$logo_root/device/config/chips/d1s"
if [[ $# != 2 ]]; then
    echo "Usage: bash $0 INPUT.{jpg,png,bmp} OUTPUT.bin" >&2
    exit 2
fi
mkdir -p "${logo_bin%/*}"
if [[ ! -x "$logo_bin" || "$logo_src" -nt "$logo_bin" || "$logo_include/hz-b500-splash.h" -nt "$logo_bin" ]]; then
    "${HOSTCC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror \
        -I"$logo_include" "$logo_src" -ljpeg -lpng -lz -o "$logo_bin"
fi
exec "$logo_bin" convert "$1" "$2"
