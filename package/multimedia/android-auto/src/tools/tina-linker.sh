#!/usr/bin/env bash
set -Eeuo pipefail
: "${CROSS_COMPILE:?run tools/build-hz-b500.sh}"
: "${CEDAR_LINKER_PREFIX:?run tools/build-hz-b500.sh}"
exec "${CROSS_COMPILE}gcc" -B "$CEDAR_LINKER_PREFIX/" "$@"
