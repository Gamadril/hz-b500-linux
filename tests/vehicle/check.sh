#!/usr/bin/env bash
set -Eeuo pipefail
trap 'printf "FAIL at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
vehicle_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$vehicle_root"
bash tests/vehicle/calibration.sh
vehicle_out="$vehicle_root/out/vehicle/tests"
vehicle_source="$vehicle_root/package/utils/vehicle/src"
vehicle_prefix="$vehicle_root/out/vehicle/host/prefix"
mkdir -p "$vehicle_out"
mkdir -p "$vehicle_out/acl"
jq -n --arg user "$(id -un)" '{user:$user,publish:["vehicle"],subscribe:["vehicle"],access:{vehicle:{methods:["snapshot","inject","set"]}}}' >"$vehicle_out/acl/vehicle.json"
chmod 600 "$vehicle_out/acl/vehicle.json"
export LD_LIBRARY_PATH="$vehicle_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cc -std=gnu11 -Wall -Wextra -Werror -I"$vehicle_source" "$vehicle_source/vehicle.c" tests/vehicle/core.c -o "$vehicle_out/core"
"$vehicle_out/core"
cc -std=gnu11 -Wall -Wextra -Werror -I"$vehicle_source" -isystem "$vehicle_prefix/include" \
    tests/vehicle/receive.c "$vehicle_source/vehicle.c" "$vehicle_source/vehicle_blob.c" \
    "$vehicle_source/vehicle_clock.c" -L"$vehicle_prefix/lib" -lubus -lubox \
    -Wl,--wrap=clock_gettime -Wl,--wrap=socket -Wl,--wrap=close -Wl,--wrap=ioctl \
    -Wl,--wrap=setsockopt -Wl,--wrap=bind -Wl,--wrap=recvmsg \
    -Wl,--wrap=uloop_fd_add -Wl,--wrap=uloop_fd_delete -Wl,--wrap=uloop_timeout_set \
    -Wl,--wrap=ubus_notify -o "$vehicle_out/receive"
"$vehicle_out/receive"
cc -std=gnu11 -Wall -Wextra -Werror -I"$vehicle_source" \
    "$vehicle_source/vehicle.c" "$vehicle_source/vehicle_clock.c" tests/vehicle/clock.c \
    -Wl,--wrap=clock_settime -o "$vehicle_out/clock"
vehicle_clock_out=$(mktemp -d "$vehicle_out/clock.XXXXXXXX")
"$vehicle_out/clock" "$vehicle_clock_out"
cc -std=gnu11 -isystem "$vehicle_prefix/include" tests/vehicle/stress.c -L"$vehicle_prefix/lib" -lubus -lubox -o "$vehicle_out/stress"
vehicle_socket="$vehicle_out/ubus.sock"
vehicle_pids=()
cleanup() {
    local pid
    for pid in "${vehicle_pids[@]}"; do kill -CONT "$pid" 2>/dev/null || :; kill "$pid" 2>/dev/null || :; done
    for pid in "${vehicle_pids[@]}"; do wait "$pid" 2>/dev/null || :; done
}
trap cleanup EXIT
"$vehicle_prefix/sbin/ubusd" -A "$vehicle_out/acl" -s "$vehicle_socket" >"$vehicle_out/ubusd.log" 2>&1 &
vehicle_pids+=("$!")
rpc() { "$vehicle_prefix/bin/ubus" -s "$vehicle_socket" "$@"; }
wait_for() {
    local attempt
    for attempt in {1..80}; do if "$@"; then return 0; fi; sleep 0.05; done
    printf 'Timed out waiting for: %s\n' "$*" >&2; return 1
}
bus_ready() { rpc list >/dev/null 2>&1; }
wait_for bus_ready
start_service() {
    "$vehicle_root/out/vehicle/build/vehicled" --simulate --no-demo --ubus-socket "$vehicle_socket" >"$vehicle_out/service.log" 2>&1 &
    vehicle_service_pid=$!;vehicle_pids+=("$!")
}
service_ready() { rpc call vehicle snapshot >"$vehicle_out/snapshot.json" 2>/dev/null; }
start_service
wait_for service_ready
vehicle_generation=$(jq -r .generation "$vehicle_out/snapshot.json")
jq -e '.schema==1 and .simulated and (.signals.speed_kph.valid|not) and (.signals.engine_rpm.valid|not)' "$vehicle_out/snapshot.json" >/dev/null
for client in first second; do
    "$vehicle_root/out/vehicle/build/vehicle-watch" --ubus-socket "$vehicle_socket" >"$vehicle_out/$client.log" 2>"$vehicle_out/$client.err" &
    vehicle_pids+=("$!")
done
client_ready() { grep -qE "$vehicle_generation" "$vehicle_out/first.log" && grep -qE "$vehicle_generation" "$vehicle_out/second.log"; }
wait_for client_ready
speed_sample() {
    rpc call vehicle inject '{"id":422,"data":"00000000000000F0"}'
    rpc call vehicle inject '{"id":422,"data":"80008000800090F1"}'
}
speed_sample
rpc call vehicle inject '{"id":450,"data":"0001020304050607"}'
rpc call vehicle inject '{"id":586,"data":"05"}'
rpc call vehicle inject '{"id":760,"data":"0E253816AFEA07FF"}'
rpc call vehicle inject '{"id":466,"data":"52"}'
rpc call vehicle snapshot >"$vehicle_out/decoded.json"
jq -e '.signals.speed_kph.value==103 and .signals.speed_kph.valid and .signals.pdc_front_left_cm.value==4 and .signals.pdc_rear_left_cm.value==0 and .signals.pdc_active.value==1' "$vehicle_out/decoded.json" >/dev/null
jq -e '.signals.time_hour.value==14 and .signals.time_minute.value==37 and .signals.time_second.value==56 and .signals.date_day.value==22 and .signals.date_month.value==10 and .signals.date_year.value==2026 and .signals.date_year.valid' "$vehicle_out/decoded.json" >/dev/null
jq -e '.signals.manual_gear.value==7 and .signals.manual_gear.valid and .signals.manual_gear.ttl_ms==1000 and .signals.manual_gear.unit=="enum"' "$vehicle_out/decoded.json" >/dev/null
rpc call vehicle inject '{"id":470,"data":"E00C"}'
rpc call vehicle inject '{"id":470,"data":"C00C"}'
both_events() { grep -qE '^media 1 next$' "$vehicle_out/first.log" && grep -qE '^media 1 next$' "$vehicle_out/second.log"; }
wait_for both_events
"$vehicle_root/out/vehicle/build/vehicle-watch" --ubus-socket "$vehicle_socket" >"$vehicle_out/late.log" 2>"$vehicle_out/late.err" &
vehicle_late_pid=$!;vehicle_pids+=("$!")
late_ready() { grep -qE "$vehicle_generation" "$vehicle_out/late.log"; }
wait_for late_ready
sleep 0.2
! grep -qE '^media ' "$vehicle_out/late.log"
expired() { rpc call vehicle snapshot >"$vehicle_out/expired.json"; jq -e '.signals.speed_kph.seen and (.signals.speed_kph.valid|not)' "$vehicle_out/expired.json" >/dev/null; }
wait_for expired
jq -e '(.signals.manual_gear.valid|not)' "$vehicle_out/expired.json" >/dev/null
printf 'PASS: snapshots, simultaneous subscribers, state expiry and no event replay to late clients\n'
kill -STOP "$vehicle_late_pid"
"$vehicle_out/stress" "$vehicle_socket" 1200
sleep 1.2
kill -CONT "$vehicle_late_pid"
sleep 0.3
! grep -qE '^media ' "$vehicle_out/late.log"
# The stopped subscriber cannot block the decoder or other clients.
speed_sample
rpc call vehicle snapshot | jq -e '.signals.speed_kph.valid and .signals.speed_kph.value==103' >/dev/null
printf 'PASS: bounded broker queues, slow-client isolation and expired-action suppression\n'
kill "$vehicle_service_pid"
wait "$vehicle_service_pid" || :
start_service
wait_for service_ready
vehicle_new_generation=$(jq -r .generation "$vehicle_out/snapshot.json")
[[ "$vehicle_new_generation" != "$vehicle_generation" ]]
reconnected() { grep -qE "$vehicle_new_generation" "$vehicle_out/first.log" && grep -qE "$vehicle_new_generation" "$vehicle_out/second.log"; }
wait_for reconnected
rpc call vehicle inject '{"id":470,"data":"E00C"}'
rpc call vehicle inject '{"id":470,"data":"C00C"}'
after_restart() { [[ $(grep -cE '^media 1 next$' "$vehicle_out/first.log") == 2 ]]; }
wait_for after_restart
printf 'PASS: service generation changes, client reconnection and event sequence reset\n'
if rpc call vehicle inject '{"id":422,"data":"not hex"}' >/dev/null 2>&1; then exit 1; fi
if rpc call vehicle set '{"signal":"engine_rpm","value":9000}' >/dev/null 2>&1; then exit 1; fi
if rpc call vehicle set '{"signal":"manual_gear","value":1}' >/dev/null 2>&1; then exit 1; fi
printf 'PASS: simulator input validation\n'
if [[ -x out/car-dashboards/dashboard/preview && -x out/car-dashboards/hmi/pdc-preview ]]; then
    for project in dashboard hmi pdc; do
        speed_sample
        rpc call vehicle inject '{"id":450,"data":"0001020304050607"}'
        rpc call vehicle inject '{"id":586,"data":"05"}'
        args=("$project" --vehicle --ubus-socket "$vehicle_socket")
        [[ "$project" != hmi ]] || args+=(--page dashboard)
        result=0
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software VEHICLE_DEBUG=1 \
            timeout --kill-after=2s 4s bash package/gui/car-dashboards/run-host.sh "${args[@]}" \
            >"$vehicle_out/ui-$project.log" 2>&1 || result=$?
        [[ "$result" == 124 ]]
        grep -qE 'speed 103 rpm --.*front_left_cm 4' "$vehicle_out/ui-$project.log"
        grep -qE 'speed -- rpm --' "$vehicle_out/ui-$project.log"
        grep -qE 'SDL clock running; rendered [1-9]' "$vehicle_out/ui-$project.log"
        ! grep -qE '\[Error\]|glyph dsc. not found|Segmentation fault|Assertion.*failed|Couldn.t create a new layer' "$vehicle_out/ui-$project.log"
        printf 'PASS: %s service telemetry, PDC, stale/unknown values and LVGL rendering\n' "$project"
    done
else
    printf 'SKIP: UI integration (build package/gui/car-dashboards first)\n'
fi
kill "$vehicle_service_pid"
wait "$vehicle_service_pid" || :
"$vehicle_root/out/vehicle/build/vehicled" --interface vehicle-missing --ubus-socket "$vehicle_socket" >"$vehicle_out/live-mode.log" 2>&1 &
vehicle_live_pid=$!;vehicle_pids+=("$!")
wait_for service_ready
rpc call vehicle snapshot | jq -e '(.simulated|not) and (.connected|not) and (.signals.speed_kph.valid|not)' >/dev/null
if rpc call vehicle inject '{"id":422,"data":"00000000000000F0"}' >/dev/null 2>&1; then exit 1; fi
if rpc call vehicle set '{"signal":"speed_kph","value":100}' >/dev/null 2>&1; then exit 1; fi
printf 'PASS: disconnected live source stays unknown and rejects simulator mutation methods\n'
kill "${vehicle_pids[0]}"
wait "${vehicle_pids[0]}" || :
wait "$vehicle_live_pid" || :
"$vehicle_prefix/sbin/ubusd" -A "$vehicle_out/acl" -s "$vehicle_socket" >"$vehicle_out/ubusd-restart.log" 2>&1 &
vehicle_pids+=("$!")
wait_for bus_ready
start_service
wait_for service_ready
vehicle_new_generation=$(jq -r .generation "$vehicle_out/snapshot.json")
wait_for reconnected
printf 'PASS: broker loss invalidates clients and they reconnect to a restarted broker/service\n'
