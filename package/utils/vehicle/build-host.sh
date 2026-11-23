#!/usr/bin/env bash
set -Eeuo pipefail
vehicle_source=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vehicle_root=$(cd -- "$vehicle_source/../../.." && pwd)
vehicle_out="$vehicle_root/out/vehicle"
vehicle_prefix="$vehicle_out/host/prefix"
vehicle_fetch=0
case "${1:-}" in --fetch-deps) vehicle_fetch=1;; '') ;; *) printf 'Usage: %s [--fetch-deps]\n' "$0" >&2; exit 2;; esac
mkdir -p "$vehicle_out/deps" "$vehicle_prefix" "$vehicle_out/host/json-build" "$vehicle_out/host/ubox-build" "$vehicle_out/host/ubus-build"
fetch() {
    local name=$1 url=$2 digest=$3
    local archive="$vehicle_out/deps/$name.tar.gz"
    if [[ ! -f "$archive" ]]; then
        if (( ! vehicle_fetch )); then
            printf 'Missing %s; run bash %s --fetch-deps to download pinned host-only dependencies\n' "$archive" "$0" >&2
            exit 1
        fi
        curl --fail --location --max-time 60 "$url" -o "$archive"
    fi
    printf '%s  %s\n' "$digest" "$archive" | sha256sum --check --status
}
fetch ubus https://codeload.github.com/openwrt/ubus/tar.gz/619f3a160de4f417226b69039538882787b3811c 30612835510938c0478e1b668abdb1c2194636bc6d939d35b2bf0f5ce2c1c0c3
fetch libubox https://codeload.github.com/openwrt/libubox/tar.gz/5326ce1046425154ab715387949728cfb09f4083 1fa06d1b184218f2d05f89739406e739870d7a29af4ba53a01468f17db47182a
fetch json-c https://s3.amazonaws.com/json-c_releases/releases/json-c-0.13.1-nodoc.tar.gz 94a26340c0785fcff4f46ff38609cf84ebcd670df0c8efd75d039cc951d80132
vehicle_ubus="$vehicle_out/deps/ubus-619f3a160de4f417226b69039538882787b3811c"
vehicle_ubox="$vehicle_out/deps/libubox-5326ce1046425154ab715387949728cfb09f4083"
vehicle_json="$vehicle_out/deps/json-c-0.13.1"
[[ -d "$vehicle_ubus" ]] || tar -xzf "$vehicle_out/deps/ubus.tar.gz" -C "$vehicle_out/deps"
[[ -d "$vehicle_ubox" ]] || tar -xzf "$vehicle_out/deps/libubox.tar.gz" -C "$vehicle_out/deps"
[[ -d "$vehicle_json" ]] || tar -xzf "$vehicle_out/deps/json-c.tar.gz" -C "$vehicle_out/deps"
# Only these generated host dependency copies need modern compiler/CMake accommodations.
sed -i 's/ -Werror / /' "$vehicle_ubus/CMakeLists.txt" "$vehicle_ubox/CMakeLists.txt"
# The private, unprivileged test broker must accept ACL files owned by its user.
# Production ubus sources/recipes keep their root ownership requirement.
sed -i 's/if (st.st_uid || st.st_gid)/if (st.st_uid != getuid() || st.st_gid != getgid())/' "$vehicle_ubus/ubusd_acl.c"
vehicle_patch="$vehicle_root/package/system/ubus/patches/001-fix_sendmsg_data_loss.patch"
if patch -d "$vehicle_ubus" -p1 --forward --dry-run <"$vehicle_patch" >/dev/null 2>&1; then
    patch -d "$vehicle_ubus" -p1 --forward <"$vehicle_patch"
else
    patch -d "$vehicle_ubus" -p1 --reverse --dry-run <"$vehicle_patch" >/dev/null
fi
(
    cd "$vehicle_out/host/json-build"
    if [[ ! -f Makefile ]]; then "$vehicle_json/configure" --prefix="$vehicle_prefix" --disable-static >configure.log 2>&1; fi
    make -j"${VEHICLE_JOBS:-4}" CFLAGS='-O2 -Wno-error' >build.log 2>&1
    make install CFLAGS='-O2 -Wno-error' >install.log 2>&1
)
PKG_CONFIG_PATH="$vehicle_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" cmake -S "$vehicle_ubox" -B "$vehicle_out/host/ubox-build" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX="$vehicle_prefix" \
    -DCMAKE_PREFIX_PATH="$vehicle_prefix" -DBUILD_LUA=OFF -DBUILD_EXAMPLES=OFF >"$vehicle_out/host/ubox-build/configure.log" 2>&1
cmake --build "$vehicle_out/host/ubox-build" -j"${VEHICLE_JOBS:-4}" >"$vehicle_out/host/ubox-build/build.log" 2>&1
cmake --install "$vehicle_out/host/ubox-build" >"$vehicle_out/host/ubox-build/install.log" 2>&1
cmake -S "$vehicle_ubus" -B "$vehicle_out/host/ubus-build" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_INSTALL_PREFIX="$vehicle_prefix" -DCMAKE_PREFIX_PATH="$vehicle_prefix" \
    -DCMAKE_C_FLAGS="-I$vehicle_prefix/include" -DCMAKE_EXE_LINKER_FLAGS="-L$vehicle_prefix/lib" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L$vehicle_prefix/lib" -DBUILD_LUA=OFF -DBUILD_EXAMPLES=OFF -DENABLE_SYSTEMD=OFF \
    >"$vehicle_out/host/ubus-build/configure.log" 2>&1
cmake --build "$vehicle_out/host/ubus-build" -j"${VEHICLE_JOBS:-4}" >"$vehicle_out/host/ubus-build/build.log" 2>&1
cmake --install "$vehicle_out/host/ubus-build" >"$vehicle_out/host/ubus-build/install.log" 2>&1
cmake -S "$vehicle_source" -B "$vehicle_out/build" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$vehicle_prefix" -DCMAKE_BUILD_RPATH="$vehicle_prefix/lib" >"$vehicle_out/configure.log" 2>&1
cmake --build "$vehicle_out/build" -j"${VEHICLE_JOBS:-4}" >"$vehicle_out/build.log" 2>&1
printf 'Built vehicle tools: %s\nUse: bash %s/run-host.sh {vehicled|vehicle-watch|ubus|ubusd} ...\n' "$vehicle_out/build" "$vehicle_source"
