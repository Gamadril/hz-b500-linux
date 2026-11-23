#!/usr/bin/env bash
# Build the raw SD image: boot0/TOC1 prefix, ext4 root (p1), raw splash (p2).
# Only creates or replaces a regular image file, never a block device.
set -euo pipefail
if [[ $# != 3 ]]; then
    echo "Usage: bash $0 IMAGE_DIR OUTPUT_IMAGE CARD_SECTORS" >&2
    exit 1
fi
image_dir=$1
output=$2
card_sectors=$3
root_start=73728 # 36 MiB: retain the established reserved boot area.
splash_sectors=6144 # 3 MiB, raw header and ARGB8888 pixels.
[[ ! -e "$output" && ! -L "$output" ]] || [[ -f "$output" && ! -L "$output" ]] || {
    echo 'Output must be a regular file' >&2; exit 1;
}
[[ "$card_sectors" =~ ^[1-9][0-9]{0,9}$ ]] &&
    (( card_sectors >= 2097152 && card_sectors <= 4294967295 )) || {
    echo 'Invalid SD capacity' >&2; exit 1;
}
command -v sfdisk >/dev/null || { echo 'Host sfdisk (util-linux) is required' >&2; exit 1; }

# Fail rather than silently omit a new partition added to the configuration.
mapfile -t parts < <(awk -F= '
    { sub(/;.*/, ""); gsub(/[[:space:]\r"]/, "") }
    $1 == "name" { print $2 }
' "$image_dir/sys_partition.fex")
[[ ${#parts[@]} == 1 && ${parts[0]} == rootfs ]] || {
    echo 'Minimal SD layout requires exactly one rootfs partition' >&2; exit 1;
}
root_sectors=$(awk -F= '
    { sub(/;.*/, ""); gsub(/[[:space:]\r"]/, "") }
    /^\[partition\]$/ { selected=1 }
    selected && $1 == "size" { print $2; exit }
' "$image_dir/sys_partition.fex")
root_file=$(awk -F= '
    { sub(/;.*/, ""); gsub(/[[:space:]\r"]/, "") }
    $1 == "downloadfile" { print $2 }
' "$image_dir/sys_partition.fex")
[[ "$root_sectors" =~ ^[1-9][0-9]{0,9}$ && "$root_file" == rootfs.fex ]] || {
    echo 'Invalid rootfs size or downloadfile' >&2; exit 1;
}
(( root_sectors <= 4294967295 && root_start + root_sectors + splash_sectors <= card_sectors )) || {
    echo 'Root partition exceeds SD capacity' >&2; exit 1;
}
rootfs="$image_dir/$root_file"
[[ -s "$rootfs" ]] && (( $(stat -Lc %s "$rootfs") == root_sectors * 512 )) || {
    echo 'Root filesystem size must match its partition exactly' >&2; exit 1;
}
[[ $(od -An -tx1 -j 1080 -N 2 "$rootfs" | tr -d ' \n') == 53ef ]] || {
    echo 'Root filesystem has no ext superblock signature' >&2; exit 1;
}
# Reserved boot area, matching the former programmer_img output and boot0's
# load_toc1_from_sdmmc(): boot0 at 8 KiB plus a backup copy at 128 KiB, and
# TOC1 at 12 MiB plus a backup copy at 16 MiB + 16 KiB. The rest stays zero.
boot0_offsets=(8192 131072)
toc_offsets=(12582912 16793600)
boot0=$image_dir/boot0_sdcard.fex
toc=$image_dir/boot_package.fex
boot0_size=$(stat -Lc %s "$boot0")
toc_size=$(stat -Lc %s "$toc")
(( boot0_size > 0 && 8192 + boot0_size <= 131072 &&
   131072 + boot0_size <= 12582912 &&
   toc_size > 0 && 12582912 + toc_size <= 16793600 &&
   16793600 + toc_size <= root_start * 512 )) || {
    echo 'Boot components overlap reserved offsets' >&2; exit 1;
}
: > "$output"
for offset in "${boot0_offsets[@]}"; do
    dd if="$boot0" of="$output" bs=512 seek=$((offset / 512)) conv=notrunc status=none
done
for offset in "${toc_offsets[@]}"; do
    dd if="$toc" of="$output" bs=512 seek=$((offset / 512)) conv=notrunc status=none
done

splash_start=$((root_start + root_sectors))
logo_script="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/boot-logo.sh"
if [[ ${SD_BOOT_LOGO:-default} == none ]]; then
    : > "$image_dir/boot-logo.bin"
elif [[ ${SD_BOOT_LOGO:-default} == default ]]; then
    logo_root="$(cd -- "$(dirname -- "$logo_script")/.." && pwd)"
    logo_artwork="$logo_root/device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg"
    if [[ -f $logo_artwork ]]; then
        bash "$logo_root/scripts/convert-boot-logo.sh" "$logo_artwork" "$image_dir/boot-logo.bin"
    else
        bash "$logo_script" --default "$image_dir/boot-logo.bin"
    fi
else
    [[ -f "$SD_BOOT_LOGO" && ! "$SD_BOOT_LOGO" -ef "$output" ]] || { echo 'Invalid SD_BOOT_LOGO file' >&2; exit 1; }
    bash "$logo_script" --verify "$SD_BOOT_LOGO"
    if [[ ! "$SD_BOOT_LOGO" -ef "$image_dir/boot-logo.bin" ]]; then
        cp -- "$SD_BOOT_LOGO" "$image_dir/boot-logo.bin"
    fi
fi
truncate -s "$((splash_sectors * 512))" "$image_dir/boot-logo.bin"
truncate -s "$(((splash_start + splash_sectors) * 512))" "$output"
# MBR needs only sector zero, leaving boot0 at sector 16 undisturbed. It also
# has no backup table tied to the physical card's final sector.
printf 'label: dos\nlabel-id: 0x0133b500\nunit: sectors\n\nstart=%s, size=%s, type=83\nstart=%s, size=%s, type=da\n' \
    "$root_start" "$root_sectors" "$splash_start" "$splash_sectors" |
    sfdisk --no-reread --no-tell-kernel --wipe never --wipe-partitions never "$output"
dd if="$rootfs" of="$output" bs=1M seek=36 conv=notrunc status=none
dd if="$image_dir/boot-logo.bin" of="$output" bs=512 seek="$splash_start" conv=notrunc status=none
printf 'Created SD image: rootfs p1, raw splash p2; root start=%s sectors, size=%s sectors\n' \
    "$root_start" "$root_sectors"
