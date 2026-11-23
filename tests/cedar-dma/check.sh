#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
SOURCE=${1:-"$ROOT/lichee/linux-5.4/drivers/media/cedar-ve/cedar_ve.c"}
OUT="$ROOT/out/d1s-hz-b500_sd/cedar-dma-list-fix/host-check"
mkdir -p "$OUT"
extract_function() {
    awk -v signature="$1" '$0 == signature { active=1 } active { print } active && /^}$/ { exit }' "$SOURCE"
}
{
    printf 'static void probe_mapping_state(void)\n{\n'
    extract_function 'static int cedardev_init(struct platform_device *pdev)' |
        sed -n '/memset(cedar_devp, 0, sizeof(struct cedar_dev));/,/\/\/1.register cdev/p' |
        sed '$d'
    printf '}\n'
    extract_function 'int enable_cedar_hw_clk(void)'
    extract_function 'int disable_cedar_hw_clk(void)'
    extract_function 'static int map_dma_buf_addr(int fd, unsigned int *addr, struct file *filp)'
    extract_function 'static void unmap_dma_buf_addr(int unmap_all, int fd, struct file *filp)'
} > "$OUT/driver-functions.inc"
gcc -std=gnu11 -Wall -Werror -Wno-unused-parameter \
    -I "$ROOT/lichee/linux-5.4/drivers/media/cedar-ve" -I "$OUT" \
    "$ROOT/tests/cedar-dma/lifetime.c" -o "$OUT/lifetime"
ulimit -c 0
"$OUT/lifetime"
