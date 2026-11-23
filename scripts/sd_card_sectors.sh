#!/usr/bin/env bash
# Select and validate the SD capacity limit, in 512-byte sectors.
set -euo pipefail
if [[ $# != 1 ]]; then
    printf 'Usage: bash %s DEFAULT_SECTORS_FILE\n' "$0" >&2
    exit 1
fi
sectors=${SD_CARD_SECTORS-$(< "$1")}
# The current 512 MiB root layout fits within 1 GiB. The SDK uses a u32 count.
if [[ ! $sectors =~ ^[1-9][0-9]{0,9}$ ]] ||
   (( sectors < 2097152 || sectors > 4294967295 )); then
    printf 'SD_CARD_SECTORS must be a decimal sector count from 2097152 to 4294967295\n' >&2
    exit 1
fi
printf '%s\n' "$sectors"
