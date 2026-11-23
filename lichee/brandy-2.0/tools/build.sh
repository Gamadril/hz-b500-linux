#!/usr/bin/env bash
# Compatibility entry point for the Boot0/FES builder.
set -euo pipefail
BRANDY_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$BRANDY_DIR"
exec bash "$BRANDY_DIR/build.sh" "$@"
