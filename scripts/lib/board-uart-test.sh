#!/usr/bin/env bash
# Sourced by board-uart.sh after the console command lock has been acquired.
board_hardware_test() {
    local report="$session/test-$(date -u +%Y%m%dT%H%M%S)-$$"
    local can_device sim_pid= ui_started=0 passed=0 test_baud=${1:-750000} rate_override=0 rate_dir
    [[ $test_baud =~ ^[0-9]+$ ]] && ((test_baud>=9600 && test_baud<=1500000)) || fail 'Invalid test baud'
    rate_dir="/tmp/hz-uart-hw-rate-$(date +%s)-$$"
    can_device=$(cat "$session/can-device")
    mkdir "$report"
    stop_sim() {
        if [[ -n $sim_pid ]];then kill "$sim_pid" 2>/dev/null || :;wait "$sim_pid" 2>/dev/null || :;sim_pid=;fi
    }
    restore() {
        trap - EXIT INT TERM
        stop_sim
        if ((ui_started)) && live && [[ ! -e $session/pending ]];then
            remote 'if [ -s /var/run/hz-ui-hwtest.pid ]; then p=$(cat /var/run/hz-ui-hwtest.pid); case "$p" in *[!0-9]*|"") exit 1;; esac; kill "$p" 2>/dev/null || :; fi; sleep 1; /etc/init.d/hz-ui start' 15 > "$report/restore.txt" || :
        fi
        if ((rate_override)) && live && [[ ! -e $session/pending ]];then
            remote "for f in '$rate_dir/slcan.pid' '$rate_dir/data.pid'; do if [ -s \"\$f\" ]; then p=\$(cat \"\$f\"); kill \"\$p\" 2>/dev/null || :; : >\"\$f\"; fi; done; sleep 1; /etc/init.d/vehicle start" 20 > "$report/rate-restore.txt" || :
        fi
        printf '%s\n' "$passed" > "$report/completed"
        printf 'Hardware test evidence: %s\n' "$report" >&2
    }
    trap restore EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    wake 90 > "$report/wake.txt"
    remote 'uname -a; opkg status vehicle; opkg status hz-ui; cat /proc/cmdline; cat /proc/tty/driver/uart; cat /var/run/hz-ui.boot; cat /var/run/hz-ui.ready; dmesg' 30 > "$report/baseline.txt"
    printf '%s\n' "$test_baud" > "$report/baud"
    if ((test_baud!=750000));then
        rate_override=1
        remote "/etc/init.d/vehicle stop; sleep 1; mkdir '$rate_dir'; /usr/bin/vehicle-slcan --baud '$test_baud' >'$rate_dir/slcan.log' 2>&1 </dev/null & echo \$! >'$rate_dir/slcan.pid'; /usr/bin/vehicled >'$rate_dir/data.log' 2>&1 </dev/null & echo \$! >'$rate_dir/data.pid'; sleep 1" > "$report/rate-setup.txt"
    fi
    sleep 6
    remote 'ubus call vehicle snapshot' > "$report/before.json"
    jq -e '.schema==1 and (.simulated|not) and (.connected|not) and (.signals.speed_kph.valid|not) and (.signals.engine_rpm.valid|not)' "$report/before.json" > /dev/null
    printf 'PASS: no CAN traffic is reported as unavailable before simulation\n' | tee -a "$report/results.txt"
    remote '/etc/init.d/hz-ui stop; sleep 1; HZ_UI_DIAGNOSTICS=/var/run/hz-ui-hwtest.json /usr/bin/hz-ui --page dashboard >/tmp/hz-ui-hwtest.log 2>&1 </dev/null & echo $! >/var/run/hz-ui-hwtest.pid' > "$report/ui-start.txt"
    ui_started=1
    sleep 2
    for values in '0 0 3587' '80 2500 852' '160 4800 1716';do
        local speed rpm speed_angle attempt received=0
        read -r speed rpm speed_angle <<< "$values"
        bash "$board_root/scripts/vehicle-uart-sim.sh" --device "$can_device" --baud "$test_baud" --speed "$speed" --rpm "$rpm" \
            > "$report/can-$speed.stdout" 2> "$report/can-$speed.log" 7>&- &
        sim_pid=$!
        for attempt in {1..15};do
            kill -0 "$sim_pid" 2>/dev/null || fail "CAN simulator exited; inspect $report/can-$speed.log"
            remote 'ubus call vehicle snapshot' > "$report/vehicle-$speed.json"
            remote 'cat /var/run/hz-ui-hwtest.json' > "$report/ui-$speed.json" || :
            if jq -e --argjson speed "$speed" --argjson rpm "$rpm" \
                '.connected and (.simulated|not) and .received_frames>0 and .signals.speed_kph.valid and .signals.speed_kph.value==$speed and .signals.engine_rpm.valid and .signals.engine_rpm.value==$rpm and .signals.date_year.valid' \
                "$report/vehicle-$speed.json" > /dev/null &&
               jq -e --argjson speed "$speed" --argjson rpm "$rpm" --argjson speed_angle "$speed_angle" \
                '.dashboard_active and .connected and .calendar_valid and .speed.valid and .speed.value==$speed and .speed.exists and .speed.visible and (.speed.hidden|not) and .speed.angle==$speed_angle and .rpm.valid and .rpm.value==$rpm and .rpm.exists and .rpm.visible and (.rpm.hidden|not) and .rpm.angle==(($rpm*2430/8000)|floor)' \
                "$report/ui-$speed.json" > /dev/null;then received=1;break;fi
            sleep 0.2
        done
        ((received)) || fail "Telemetry/hand check failed at speed=$speed RPM=$rpm; evidence: $report"
        printf 'PASS: physical UART -> SLCAN -> ubus -> visible dashboard hands: speed %s, RPM %s\n' "$speed" "$rpm" | tee -a "$report/results.txt"
        if [[ $speed == 80 && -n ${BOARD_UART_FB_CAPTURE:-} ]];then
            [[ $BOARD_UART_FB_CAPTURE =~ ^/[a-zA-Z0-9_./-]+$ ]] || fail 'Invalid framebuffer capture utility path'
            remote "$BOARD_UART_FB_CAPTURE /dev/fb0 '$rate_dir-dashboard.png' && cat '$rate_dir-dashboard.png'" 180 > "$report/dashboard-80.png"
        fi
        stop_sim
    done
    sleep 7
    remote 'ubus call vehicle snapshot' > "$report/vehicle-silent.json"
    remote 'cat /var/run/hz-ui-hwtest.json' > "$report/ui-silent.json"
    jq -e '(.connected|not) and (.signals.speed_kph.valid|not) and (.signals.engine_rpm.valid|not)' "$report/vehicle-silent.json" > /dev/null
    jq -e '(.connected|not) and (.speed.valid|not) and .speed.hidden and (.rpm.valid|not) and .rpm.hidden' "$report/ui-silent.json" > /dev/null
    printf 'PASS: stopping UART traffic expires connection and hides stale hands\n' | tee -a "$report/results.txt"
    remote 'cat /proc/tty/driver/uart; cat /sys/class/net/can0/statistics/rx_packets; cat /sys/class/net/can0/statistics/rx_errors; cat /tmp/hz-ui-hwtest.log; dmesg' 30 > "$report/final.txt"
    passed=1
    restore
}
