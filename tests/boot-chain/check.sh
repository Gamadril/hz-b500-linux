#!/usr/bin/env bash
# Exercise the retained boot chain without target builds or physical media.
set -Eeuo pipefail
trap 'printf "FAIL at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$ROOT/out/boot-chain"
TEST_DIR=$(mktemp -d "$ROOT/out/boot-chain/check.XXXXXXXX")
SDK="$TEST_DIR/sdk"
mkdir -p "$SDK/scripts" "$SDK/lichee/brandy-2.0" "$SDK/out/host/bin"
extract_functions() {
    awk '/^function / {copy=1} copy {print} copy && /^}/ {copy=0}' "$1"
}
source <(extract_functions "$ROOT/build/envsetup.sh")
for removed in get_uboot muboot muboot_nor cboot; do
    ! declare -F "$removed" >/dev/null
done
cp "$ROOT/lichee/brandy-2.0/build.sh" "$SDK/lichee/brandy-2.0/"
cp -a "$ROOT/lichee/brandy-2.0/spl" "$SDK/lichee/brandy-2.0/"
# A clean SPL checkout must neither request nor unpack an ARM compiler.
command make -C "$SDK/lichee/brandy-2.0/spl" distclean > "$TEST_DIR/clean.log" 2>&1
[[ ! -e "$SDK/lichee/brandy-2.0/tools/toolchain" ]]
export BUILD_CALLS="$TEST_DIR/build-calls"
: > "$BUILD_CALLS"
gettop() { printf '%s\n' "$SDK"; }
# Replace compilation only; exercise the real shell entry points and dispatch.
make() { printf '%s | %s\n' "$PWD" "$*" >> "$BUILD_CALLS"; }
export -f make
export TARGET_PRODUCT=d1s_hz_b500_sd TARGET_PLATFORM=d1s TARGET_CHIP=sun20iw1p1
export LICHEE_BRANDY_SPL=spl
for profile in sd; do
    export TARGET_BOARD="d1s-hz-b500_$profile" TARGET_PLAN="hz-b500_$profile"
    config="$SDK/device/config/chips/d1s/configs/$TARGET_PLAN"
    mkdir -p "$config"
    cp "$ROOT/device/config/chips/d1s/configs/$TARGET_PLAN/sys_config.fex" "$config/"
    (mboot) > "$TEST_DIR/mboot-$profile.log" 2>&1
done
export TARGET_BOARD=d1s-hz-b500_sd TARGET_PLAN=hz-b500_sd
[[ $(wc -l < "$BUILD_CALLS") == 3 ]]
! grep -qE 'u-boot|uboot|arm-linux' "$BUILD_CALLS"
grep -qE 'p=sun20iw1p1 b=d1s' "$BUILD_CALLS"
if (cd "$SDK/lichee/brandy-2.0"; bash build.sh -o uboot) > "$TEST_DIR/removed-target.log" 2>&1; then
    echo 'Removed U-Boot build target unexpectedly succeeded' >&2; exit 1
fi
unset -f make
# Validate the actual Makefile configuration path with only a RISC-V compiler.
mkdir -p "$TEST_DIR/compiler"
export CROSS_COMPILE="$TEST_DIR/compiler/riscv64-unknown-linux-gnu-"
cat > "${CROSS_COMPILE}gcc" <<'GCC'
#!/usr/bin/env bash
[[ " $* " == *' -print-libgcc-file-name '* ]] || exit 1
printf '/fixture/libgcc.a\n'
GCC
chmod +x "${CROSS_COMPILE}gcc"
export LICHEE_CHIP_CONFIG_DIR="$SDK/device/config/chips/d1s" LICHEE_BOARD=hz-b500_sd
command make -C "$SDK/lichee/brandy-2.0/spl" p=sun20iw1p1 b=d1s > "$TEST_DIR/configure.log" 2>&1
[[ ! -e "$SDK/lichee/brandy-2.0/tools/toolchain" ]]

# Full SDK make dispatch must reach the OS and cache Boot0 rebuilds without
# inspecting a deleted bootloader directory; Boot0 failure must still stop it.
(
    source <(extract_functions "$ROOT/build/envsetup.sh")
    gettop() { printf '%s\n' "$SDK"; }
    mboot0() { printf 'boot0\n' >> "$TEST_DIR/sdk-build-calls"; }
    mkarisc() { :; }
    check_parameters() { :; }
    prepare_buildserver() { :; }
    export TEST_DIR
    printf 'all:\n\t@echo os >> "$(TEST_DIR)/sdk-build-calls"\n' > "$SDK/Makefile"
    m -j1
    m -j1
    [[ $(grep -c '^boot0$' "$TEST_DIR/sdk-build-calls") == 1 ]]
    [[ $(grep -c '^os$' "$TEST_DIR/sdk-build-calls") == 2 ]]
    mboot0() { return 1; }
    # Drop the Boot0 rebuild cache so the next build must rebuild Boot0.
    rm -f "$SDK"/lichee/brandy-2.0/spl/.newest-*.patch
    if m -j1; then
        echo 'SDK build ignored Boot0 failure' >&2; exit 1
    fi
    [[ $(grep -c '^os$' "$TEST_DIR/sdk-build-calls") == 2 ]]
) > "$TEST_DIR/sdk-build.log" 2>&1

# A restored checkout needs the target and Boot0 compilers, with no ARM archive.
cp "$ROOT/scripts/import-sdk-assets.sh" "$SDK/scripts/"
target_bin="$SDK/prebuilt/gcc/linux-x86/riscv/toolchain-thead-glibc/riscv64-glibc-gcc-thead_20200702/bin"
mkdir -p "$target_bin" "$SDK/prebuilt/bootloader"
ln -s /bin/true "$target_bin/riscv64-unknown-linux-gnu-gcc"
printf 'fixture archive\n' > "$SDK/prebuilt/bootloader/riscv64-linux-x86_64-20200528.tar.xz"
bash "$SDK/scripts/import-sdk-assets.sh" --check > "$TEST_DIR/assets.log" 2>&1

# Use the real FEX/Boot0 tools, the open DTB/TOC1 scripts and synthetic
# OpenSBI/kernel bytes. xz supplies LZMA-alone encoding, matching the SDK format.
cp "$ROOT/scripts/mk_direct_payload.sh" "$ROOT/scripts/mk_minimal_sd_image.sh" "$ROOT/scripts/boot-logo.sh" "$SDK/scripts/"
cp "$ROOT/scripts/pad_dtb.sh" "$ROOT/scripts/hz-pack" "$SDK/scripts/"
mkdir -p "$SDK/tools"
ln -s "$ROOT/tools/hz-pack" "$SDK/tools/hz-pack"
export HZ_PACK_TARGET_DIR="$ROOT/out/host/hz-pack"
cp "$ROOT/scripts/convert-boot-logo.sh" "$SDK/scripts/"
mkdir -p "$SDK/package/utils/boot-logo/src" "$SDK/device/config/chips/d1s/configs/hz-b500_sd/assets"
cp "$ROOT/package/utils/boot-logo/src/boot-logo.c" "$SDK/package/utils/boot-logo/src/"
if [[ -f $ROOT/device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg ]]; then
    cp "$ROOT/device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg" \
        "$SDK/device/config/chips/d1s/configs/hz-b500_sd/assets/"
fi
mkdir -p "$SDK/tools/hz-boot-logo" "$SDK/device/config/chips/d1s" \
    "$SDK/lichee/brandy-2.0/opensbi/platform/generic/allwinner"
cp "$ROOT/tools/hz-boot-logo/main.c" "$SDK/tools/hz-boot-logo/"
cp "$ROOT/device/config/chips/d1s/hz-b500-splash.h" "$SDK/device/config/chips/d1s/"
cp "$ROOT/lichee/brandy-2.0/opensbi/platform/generic/allwinner/hz-b500-font-8x16.h" \
    "$SDK/lichee/brandy-2.0/opensbi/platform/generic/allwinner/"
cat > "$SDK/out/host/bin/lzma" <<'LZMA'
#!/usr/bin/env bash
set -euo pipefail
[[ $1 == e ]]
xz --format=lzma --stdout "$2" > "$3"
LZMA
chmod +x "$SDK/out/host/bin/lzma"
export PATH="$SDK/out/host/bin:$ROOT/tools/pack-bintools/src:$PATH"
# Expand the real image recipes for both a normal and initramfs build. Only
# kernel compilation is replaced by fixture files; mkbootimg is the real tool.
image_test="$TEST_DIR/image-recipes"
mkdir -p "$image_test/build" "$image_test/kernel" "$image_test/images"
: > "$image_test/rules.mk"
: > "$image_test/build/host.mk"
cat > "$image_test/build/image.mk" <<'MAKE'
define BuildImage
.PHONY: verify
verify:
	$(if $(CONFIG_TARGET_ROOTFS_INITRAMFS),$(call Image/Build/Initramfs),$(call Image/BuildKernel))
	$(call Image/InstallKernel)
endef
MAKE
printf 'fixture kernel\n' > "$image_test/kernel/Image"
for name in Image.gz Image-initramfs Image.gz-initramfs; do
    cp "$image_test/kernel/Image" "$image_test/kernel/$name"
done
printf 'fixture ramdisk\n' > "$image_test/ramdisk.img"
for initramfs in '' y; do
    (cd "$image_test"; command make -f "$ROOT/target/allwinner/generic/image/Makefile" verify \
        TOPDIR="$image_test" BUILD_DIR="$image_test/build" ARCH=riscv \
        KDIR="$image_test/kernel" TARGET_OUT_DIR="$image_test/images" IMG_PREFIX=fixture \
        CP=cp CONFIG_TARGET_ROOTFS_INITRAMFS="$initramfs") > "$TEST_DIR/image-${initramfs:-normal}.log" 2>&1
    image=boot.img
    [[ -z "$initramfs" ]] || image=boot_initramfs.img
    [[ $(dd if="$image_test/images/$image" bs=1 count=8 status=none) == 'ANDROID!' ]]
done

source <(extract_functions "$ROOT/scripts/pack_img.sh")
export PACK_TOPDIR="$SDK" PACK_BOARD=d1s-hz-b500_sd
export ROOT_DIR="$SDK/out/$PACK_BOARD"
image_dir="$ROOT_DIR/image"
firmware="$ROOT_DIR/opensbi/platform/generic/firmware"
kernel_dir="$ROOT_DIR/compile_dir/target/linux-$PACK_BOARD/linux-5.4.61/arch/riscv/boot"
mkdir -p "$image_dir" "$firmware" "$kernel_dir"
printf 'fixture OpenSBI\n' > "$firmware/fw_jump.bin"
printf 'fixture Linux\n' > "$kernel_dir/Image"
printf '/dts-v1/; / { compatible = "test,boot-chain"; };\n' > "$TEST_DIR/board.dts"
dtc -I dts -O dtb -o "$image_dir/sunxi.dtb" "$TEST_DIR/board.dts"
cp "$ROOT/device/config/chips/d1s/configs/hz-b500_sd/"{sys_config.fex,sys_partition.fex,boot_package.cfg} "$image_dir/"
cp "$ROOT/device/config/chips/d1s/bin/boot0_sdcard_sun20iw1p1.bin" "$image_dir/boot0_sdcard.fex"
(do_common) > "$TEST_DIR/pack.log" 2>&1
# The SD Boot0 carries the board header written from sys_config.fex.
cp "$ROOT/device/config/chips/d1s/bin/boot0_sdcard_sun20iw1p1.bin" "$TEST_DIR/expected-boot0.fex"
"$ROOT/scripts/hz-pack" boot0-header "$TEST_DIR/expected-boot0.fex" "$image_dir/sys_config.fex"
cmp "$TEST_DIR/expected-boot0.fex" "$image_dir/boot0_sdcard.fex"
! cmp -s "$ROOT/device/config/chips/d1s/bin/boot0_sdcard_sun20iw1p1.bin" "$image_dir/boot0_sdcard.fex"
# One TOC1 item: Boot0's LZMA payload, with no bootloader or environment.
[[ $(od -An -tu4 -j 32 -N 4 "$image_dir/boot_package.fex" | tr -d ' ') == 1 ]]
[[ $(dd if="$image_dir/boot_package.fex" bs=1 skip=64 count=10 status=none) == melis-lzma ]]
cmp -n "$(stat -c %s "$firmware/fw_jump.bin")" "$firmware/fw_jump.bin" "$image_dir/epos.img"
cmp -n "$(stat -c %s "$image_dir/sunxi.fex")" -i 1048576:0 "$image_dir/epos.img" "$image_dir/sunxi.fex"
cmp -n "$(stat -c %s "$kernel_dir/Image")" -i 2097152:0 "$image_dir/epos.img" "$kernel_dir/Image"
xz --format=lzma --decompress --stdout "$image_dir/epos.img.lzma" > "$TEST_DIR/decoded.img"
cmp "$TEST_DIR/decoded.img" "$image_dir/epos.img"
! find "$image_dir" -maxdepth 1 -type f -printf '%f\n' | grep -E 'u-boot|^env.*\.fex$'
# The actual SD prefix tool and assembler must retain both TOC1 copies and p1.
truncate -s 512M "$image_dir/rootfs.fex"
mkfs.ext4 -q -F "$image_dir/rootfs.fex"
export PACK_SIG=none PACK_PROGRAMMER=programmer PACK_CARD_SECTORS=7698432
(cd "$image_dir"; img_to_programmer sd.img unused) > "$TEST_DIR/sd.log" 2>&1
sd_image="$image_dir/sd.img"
[[ $(stat -c %s "$sd_image") == 577765376 ]]
sfdisk --json "$sd_image" > "$TEST_DIR/partitions.json"
grep -qE '"start": 73728' "$TEST_DIR/partitions.json"
grep -qE '"size": 1048576' "$TEST_DIR/partitions.json"
[[ $(grep -cE '"node":' "$TEST_DIR/partitions.json") == 2 ]]
grep -qE '"start": 1122304' "$TEST_DIR/partitions.json"
grep -qE '"size": 6144' "$TEST_DIR/partitions.json"
grep -qE '"type": "da"' "$TEST_DIR/partitions.json"
dd if="$sd_image" of="$TEST_DIR/splash.bin" bs=512 skip=1122304 count=6144 status=none
bash "$SDK/scripts/boot-logo.sh" --verify "$TEST_DIR/splash.bin"
# Same choice as mk_minimal_sd_image.sh: the supplied artwork if present
# (it is git-ignored), otherwise the generated default logo.
artwork="$SDK/device/config/chips/d1s/configs/hz-b500_sd/assets/linux_boot.jpg"
if [[ -f $artwork ]]; then
    bash "$SDK/scripts/convert-boot-logo.sh" "$artwork" "$TEST_DIR/expected-logo.bin"
else
    bash "$SDK/scripts/boot-logo.sh" --default "$TEST_DIR/expected-logo.bin"
fi
cmp -n 2458112 "$TEST_DIR/splash.bin" "$TEST_DIR/expected-logo.bin"
for offset in 12582912 16793600; do
    cmp -n "$(stat -c %s "$image_dir/boot_package.fex")" -i "$offset:0" "$sd_image" "$image_dir/boot_package.fex"
done
# Explicitly disabling artwork keeps p2 and all boot/root geometry, but zeros its contents.
(
    cd "$image_dir"
    SD_BOOT_LOGO=none img_to_programmer sd-none.img unused
) > "$TEST_DIR/sd-none.log" 2>&1
[[ $(stat -c %s "$image_dir/sd-none.img") == 577765376 ]]
cmp -n 3145728 -i 574619648:0 "$image_dir/sd-none.img" /dev/zero
printf 'PASS: Boot0 dispatch/configuration, SD direct payload, boot copies, ext4 p1 and verified splash p2\nLogs: %s\n' "$TEST_DIR"
