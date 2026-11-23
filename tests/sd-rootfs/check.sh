#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/d1s-hz-b500_sd/ui-validation/rootfs-check"
mkdir -p "$OUT"
TARGET="$ROOT/target/allwinner/d1s-hz-b500_sd/base-files/lib/preinit/80_mount_root"
INITRAMFS=0
boot_hook_add() { [[ "$1" == preinit_main && "$2" == do_mount_root ]]; }
boot_run_hook() { [[ "$1" == preinit_mount_root ]]; printf 'hook\n' >> "$OUT/calls"; }
mount() { printf 'mount %s\n' "$*" >> "$OUT/calls"; }
mount_root() { printf 'fallback\n' >> "$OUT/calls"; }
awk() { command awk "$1" "$OUT/mounts"; }
source "$TARGET"
printf '/dev/root / ext4 ro,relatime 0 0\n' > "$OUT/mounts"
: > "$OUT/calls"
do_mount_root || true # The optional, absent sysupgrade archive returns false.
printf 'mount -o remount,rw,noatime /\nhook\n' > "$OUT/expected"
cmp "$OUT/calls" "$OUT/expected"
printf '/dev/root / squashfs ro 0 0\n/dev/mmcblk0p8 /mnt/data ext4 rw 0 0\n' > "$OUT/mounts"
: > "$OUT/calls"
do_mount_root || true
printf 'fallback\nhook\n' > "$OUT/expected"
cmp "$OUT/calls" "$OUT/expected"
printf 'SD ext4 root remounts writable without overlay; other roots keep fallback\n'
