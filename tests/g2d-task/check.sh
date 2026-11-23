#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE=${1:-"$ROOT/lichee/linux-5.4/drivers/char/sunxi_g2d/g2d_rcq/g2d_mixer.c"}
OUT="$ROOT/out/d1s-hz-b500_sd/g2d-task-check"
mkdir -p "$OUT"
extract_function() {
    awk -v signature="$1" '$0 == signature { active=1 } active { print } active && /^}$/ { exit }' "$SOURCE"
}
{
    extract_function 'static __s32 g2d_mixer_task_destory(struct g2d_mixer_task *p_task)'
    extract_function '__u32 create_mixer_task(__g2d_info_t *p_g2d_info, struct mixer_para *p_para,'
} > "$OUT/driver-functions.inc"
gcc -std=gnu11 -Wall -Werror -Wno-type-limits -I "$OUT" \
    "$ROOT/tests/g2d-task/lifetime.c" -o "$OUT/lifetime"
ulimit -c 0
"$OUT/lifetime"
