#!/usr/bin/env bash
set -Eeuo pipefail
settings_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$settings_root"
settings_out="$settings_root/out/target-ui/settings-tests"
settings_prefix="$settings_root/out/vehicle/host/prefix"
mkdir -p "$settings_out"
settings_config=$(mktemp -d "$settings_out/config.XXXXXX")
mkdir -p "$settings_config/delta"
cc -std=gnu11 -Wall -Wextra -Werror -pthread -Ipackage/gui/car-dashboards/host \
    package/gui/car-dashboards/host/settings_store.c tests/target-ui/settings-store.c -Wl,--wrap=clock_gettime -o "$settings_out/store"
"$settings_out/store"
# Reuse the SDK's already unpacked UCI source; all host objects stay under out/.
settings_uci=("$settings_root"/out/common/target-*/compile_dir/target/uci-2016-02-02.1)
test "${#settings_uci[@]}" -eq 1
settings_sources=()
for settings_source in libuci file util delta parse blob; do settings_sources+=("${settings_uci[0]}/$settings_source.c"); done
cc -std=gnu99 -shared -fPIC -O2 -I"${settings_uci[0]}" -I"$settings_prefix/include" \
    "${settings_sources[@]}" -L"$settings_prefix/lib" -lubox -o "$settings_out/libuci.so"
cp package/gui/car-dashboards/files/hz-ui.config "$settings_config/hz-ui"
cp package/utils/ab5301b/files/ab5301b.config "$settings_config/ab5301b"
cp package/utils/vehicle/files/vehicle.config "$settings_config/vehicle"
cat > "$settings_config/system" <<'EOF'
config system
    option zonename 'Europe/Berlin'
EOF
cc -std=gnu11 -Wall -Wextra -Werror -pthread -I"${settings_uci[0]}" \
    -Ipackage/gui/car-dashboards/host -Ipackage/gui/car-dashboards/target \
    package/gui/car-dashboards/target/settings_device.c tests/target-ui/settings-device.c \
    -L"$settings_out" -luci -Wl,-rpath,"$settings_out" -Wl,-rpath-link,"$settings_prefix/lib" \
    -Wl,--wrap=uci_alloc_context -Wl,--wrap=uci_commit -Wl,--wrap=socket -Wl,--wrap=connect \
    -Wl,--wrap=getsockopt -Wl,--wrap=poll -Wl,--wrap=send -Wl,--wrap=read -Wl,--wrap=close -o "$settings_out/device"
LD_LIBRARY_PATH="$settings_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$settings_out/device" "$settings_config" "$settings_config/delta"
