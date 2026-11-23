#!/usr/bin/env bash
set -Eeuo pipefail
boot_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
boot_out="$boot_root/out/hmi-boot-time/tests"
mkdir -p "$boot_out/bin"
cd "$boot_root"
# Run the actual deferred loader with only its device paths replaced.
sed -e "s|/var/run/hz-ui.ready|$boot_out/ready|g" \
    -e "s|/sys/module/ssv6x5x|$boot_out/loaded|g" \
    -e "s|/usr/sbin/modprobe|$boot_out/bin/modprobe|g" \
    package/kernel/linux/files/ssv6x5x-start > "$boot_out/start"
cat > "$boot_out/bin/modprobe" <<'MOCK'
#!/usr/bin/env bash
set -eu
[[ "$1" == ssv6x5x ]]
printf 'load\n' >> "$BOOT_TEST_OUT/events"
MOCK
cat > "$boot_out/bin/sleep" <<'MOCK'
#!/usr/bin/env bash
set -eu
printf 'wait\n' >> "$BOOT_TEST_OUT/events"
if [[ ${BOOT_TEST_READY_AFTER:-0} != 0 ]]; then
    count=$(wc -l < "$BOOT_TEST_OUT/events")
    if ((count >= BOOT_TEST_READY_AFTER)); then printf '3456\n' > "$BOOT_TEST_OUT/ready"; fi
fi
MOCK
chmod +x "$boot_out/bin/modprobe" "$boot_out/bin/sleep"
export BOOT_TEST_OUT="$boot_out" PATH="$boot_out/bin:$PATH"
# Empty marker is not readiness. Two waits, then load exactly once.
: > "$boot_out/ready";: > "$boot_out/events"
BOOT_TEST_READY_AFTER=2 bash "$boot_out/start" > "$boot_out/loader.log"
[[ $(cat "$boot_out/events") == $'wait\nwait\nload' ]]
# A ready HMI loads immediately.
: > "$boot_out/events";bash "$boot_out/start" >> "$boot_out/loader.log"
[[ $(cat "$boot_out/events") == load ]]
# A failed/disabled HMI does not disable Wi-Fi indefinitely.
: > "$boot_out/ready";: > "$boot_out/events"
bash "$boot_out/start" >> "$boot_out/loader.log"
[[ $(wc -l < "$boot_out/events") == 21 ]]
[[ $(tail -n1 "$boot_out/events") == load ]]
# Existing module is not loaded a second time.
printf '3456\n' > "$boot_out/ready";: > "$boot_out/events"
mkdir -p "$boot_out/loaded";bash "$boot_out/start" >> "$boot_out/loader.log"
[[ ! -s "$boot_out/events" ]];rmdir "$boot_out/loaded"
grep -qE '^START=09$' package/gui/car-dashboards/files/hz-ui.init
grep -qE 'mkdir -p /var/run' package/gui/car-dashboards/files/hz-ui.init
grep -qE '^START=96$' package/kernel/linux/files/ssv6x5x.init
grep -qE '^CONFIG_SSV6X5X=m$' device/config/chips/d1s/configs/hz-b500_sd/linux/config-5.4
grep -qE 'loglevel=6' device/config/chips/d1s/configs/hz-b500_sd/linux/config-5.4
! grep -qE 'sd-diag-capture' device/config/chips/d1s/configs/hz-b500_sd/linux-5.4/board.dts
printf 'PASS: early HMI init and deferred Wi-Fi loading after first frame, bounded failure fallback, existing-module guard and quiet SD configuration\n'
# Exercise the actual UI boot gate without starting host procd or sleeping.
sed 's|/var/run/hz-ui.ready|'"$boot_out"'/ui-ready|g' package/gui/car-dashboards/files/hz-ui.init | \
    awk '/^boot\(\)/ {copy=1} copy {print} copy && /^}/ {exit}' > "$boot_out/gate.sh"
source "$boot_out/gate.sh"
start() { :; }
config_get_bool() { printf -v "$1" '%s' "$boot_ui_enabled"; }
sleep() {
    [[ "$1" == 0.05 ]];boot_ui_waits=$((boot_ui_waits+1))
    if ((boot_ui_ready_after && boot_ui_waits>=boot_ui_ready_after)); then printf '3100\n' > "$boot_out/ui-ready"; fi
}
boot_ui_enabled=1;boot_ui_waits=0;boot_ui_ready_after=2;: > "$boot_out/ui-ready"
boot;[[ "$boot_ui_waits" == 2 ]]
boot_ui_enabled=0;boot_ui_waits=0;: > "$boot_out/ui-ready"
boot;[[ "$boot_ui_waits" == 0 ]]
boot_ui_enabled=1;boot_ui_waits=0;boot_ui_ready_after=0
boot;[[ "$boot_ui_waits" == 120 ]]
echo 'PASS: UI boot gate releases on first frame, skips disabled UI and releases normal startup after bounded failure timeout'
