#!/usr/bin/env bash
set -Eeuo pipefail
boot_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
boot_build="$boot_root/out/d1s-hz-b500_sd"
boot_out="$boot_root/out/hmi-boot-time/image-check"
mkdir -p "$boot_out"
for boot_file in lib/modules/5.4.61/ssv6x5x.ko etc/init.d/ssv6x5x usr/libexec/ssv6x5x-start etc/init.d/hz-ui; do
    boot_name=${boot_file##*/}
    : > "$boot_out/$boot_name"
    debugfs -R "dump /$boot_file $boot_out/$boot_name" "$boot_build/rootfs.img" >> "$boot_out/debugfs.log" 2>&1
    test -s "$boot_out/$boot_name"
    cmp "$boot_out/$boot_name" "$boot_build/compile_dir/target/rootfs/$boot_file"
done
for boot_link in S09hz-ui S96ssv6x5x; do
    debugfs -R "stat /etc/rc.d/$boot_link" "$boot_build/rootfs.img" > "$boot_out/$boot_link.txt" 2>&1
    grep -qE 'Fast link dest:.*../init.d/' "$boot_out/$boot_link.txt"
done
debugfs -R 'stat /etc/procd/hmi-first' "$boot_build/rootfs.img" > "$boot_out/hmi-first.txt" 2>&1
grep -qE 'Type: regular' "$boot_out/hmi-first.txt"
debugfs -R "dump /sbin/procd $boot_out/procd" "$boot_build/rootfs.img" >> "$boot_out/debugfs.log" 2>&1
strings "$boot_out/procd" > "$boot_out/procd.strings"
grep -qE '/etc/procd/hmi-first' "$boot_out/procd.strings"
grep -qE 'S09hz-ui' "$boot_out/procd.strings"
readelf -h "$boot_out/ssv6x5x.ko" | grep -qE 'Machine:.*RISC-V'
readelf -p .modinfo "$boot_out/ssv6x5x.ko" | grep -qE 'vermagic=5.4.61 '
readelf -p .modinfo "$boot_out/ssv6x5x.ko" | grep -qE 'import_ns=VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver'
! grep -qE ssv6x5x "$boot_build/compile_dir/target/rootfs/etc/modules.d"
grep -qE '^CONFIG_SSV6X5X=m$' "$boot_root/lichee/linux-5.4/.config"
fdtget "$boot_build/image/sunxi.fex" /chosen bootargs > "$boot_out/bootargs.txt"
grep -qE 'loglevel=6' "$boot_out/bootargs.txt"
boot_sd=/soc/sdmmc@4020000
fdtget "$boot_build/image/sunxi.fex" "$boot_sd" compatible >/dev/null
! fdtget "$boot_build/image/sunxi.fex" "$boot_sd" allwinner,sd-diag-capture >/dev/null 2>&1
printf 'PASS: SD image has gated first-frame startup in procd, deferred Wi-Fi and matching RISC-V module without early autoload, NOTICE bootargs and no SD trace capture\n'
