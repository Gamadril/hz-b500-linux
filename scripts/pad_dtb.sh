#!/usr/bin/env bash
# Copy a DTB and zero-pad it like the closed "update_dtb FILE 4096": at least
# 4 KiB of spare room, rounded up to 512 bytes. The FDT header is unchanged.
# Usage: bash scripts/pad_dtb.sh INPUT.dtb OUTPUT
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: bash $0 INPUT.dtb OUTPUT" >&2
    exit 1
fi
[[ $(od -An -tx1 -N 4 "$1" | tr -d ' \n') == d00dfeed ]] || { echo "Not a DTB: $1" >&2; exit 1; }
cp -- "$1" "$2"
truncate -s $(( ($(stat -Lc %s "$1") + 4096 + 511) / 512 * 512 )) "$2"
