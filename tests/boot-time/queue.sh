#!/usr/bin/env bash
set -Eeuo pipefail
queue_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
queue_out="$queue_root/out/hmi-page-art/queue-check"
mkdir -p "$queue_out/fixtures"
queue_procd="$queue_root/out/common/target-3767144151/compile_dir/target/procd-2016-02-08/rcS.c"
queue_ubox=$(printf '%s\n' "$queue_root"/out/common/target-*/compile_dir/target/libubox-*/runqueue.c)
queue_ubox=${queue_ubox%/runqueue.c}
awk '/^static unsigned int find_rc_boot_depend_task/ {copy=1} /^int rcS\(/ {copy=0} copy {print}' "$queue_procd" > "$queue_out/queue.inc"
awk '/^static struct runqueue_task \*get_next_task_from_inactive_list/ {copy=1} /^static void __runqueue_start_next/ {copy=0} copy {print}' "$queue_ubox/runqueue.c" >> "$queue_out/queue.inc"
for queue_file in S00clock S09hz-ui S10boot S55vehicle; do printf '# fixture\n' > "$queue_out/fixtures/$queue_file"; done
cc -std=gnu11 -Wall -Wextra -Werror -Wno-sign-compare -Wno-unused-parameter \
    -I"$queue_out" -I"$queue_ubox" "$queue_root/tests/boot-time/queue.c" "$queue_ubox/safe_list.c" -o "$queue_out/queue"
"$queue_out/queue" "$queue_out/fixtures"
