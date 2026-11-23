#!/usr/bin/env bash
# Host check for the on-demand wireless AA script: first-run AP config, derived
# receiver config, receiver arguments and AP teardown, using stub commands.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
script="$root/package/multimedia/android-auto/files/android-auto-wireless"
out="$root/out/android-auto/wireless-script"
rm -rf "$out"; mkdir -p "$out/bin" "$out/tree/etc" "$out/tree/tmp" "$out/tree/usr/share/android-auto" "$out/tree/sys/class/net/wlan0"
fail() { echo "FAIL: $*" >&2; exit 1; }
cp "$root/package/multimedia/android-auto/files/hostapd.conf.example" "$out/tree/usr/share/android-auto/"
echo 02:aa:bb:cc:dd:ee >"$out/tree/sys/class/net/wlan0/address"
log="$out/calls.log"
# Daemon stubs leave a real process behind, like -B / dnsmasq forking.
cat >"$out/bin/ip" <<EOF
#!/bin/sh
echo "ip \$*" >>"$log"
EOF
cat >"$out/bin/hostapd" <<EOF
#!/bin/sh
echo "hostapd \$*" >>"$log"
# HOSTAPD_FAILS: fail this many attempts first (driver still warming up).
n=\$(cat "$out/hostapd.fails" 2>/dev/null || echo 0)
if [ "\$n" -lt "\${HOSTAPD_FAILS:-0}" ]; then echo \$((n + 1)) >"$out/hostapd.fails"; exit 1; fi
sleep 60 & echo \$! >"\$3"
EOF
cat >"$out/bin/dnsmasq" <<EOF
#!/bin/sh
echo "dnsmasq \$*" >>"$log"
for a; do case \$a in --pid-file=*) sleep 60 & echo \$! >"\${a#--pid-file=}";; esac; done
EOF
cat >"$out/bin/cedar-headunit" <<EOF
#!/bin/sh
echo "cedar-headunit \$*" >>"$log"
cp "$out/tree/var/run/android-auto/hostapd.pid" "$out/hostapd.seen"
exit \${RECEIVER_STATUS:-0}
EOF
cat >"$out/bin/ab5301b" <<EOF
#!/bin/sh
echo "ab5301b \$*" >>"$log"
EOF
cat >"$out/bin/sleep" <<EOF
#!/bin/sh
# Retry pauses are instant; the fake daemons still sleep for real.
[ "\$1" = 1 ] || exec /bin/sleep "\$@"
EOF
chmod +x "$out"/bin/*
run() { PATH="$out/bin:$PATH" ANDROID_AUTO_ROOT="$out/tree" sh "$script" --once; }

run || fail "first run failed"
conf="$out/tree/etc/android-auto-hostapd.conf" aa="$out/tree/etc/android-auto-wireless.conf"
psk=$(sed -n 's/^wpa_passphrase=//p' "$conf")
[[ $psk =~ ^[A-Za-z0-9]{16}$ ]] || fail "first run passphrase: $psk"
[[ $(stat -c %a "$conf") == 600 && $(stat -c %a "$aa") == 600 ]] || fail "config permissions"
grep -qx "psk=$psk" "$aa" && grep -qx 'ssid=HZ-B500-AA' "$aa" && grep -qx 'channel=36' "$aa" &&
    grep -qx 'mac=02:aa:bb:cc:dd:ee' "$aa" && grep -qx 'ip=192.168.43.1' "$aa" || fail "receiver config: $(cat "$aa")"
grep -qx "cedar-headunit --transport wireless --wireless-config $aa --once" "$log" || fail "receiver arguments"
grep -A1 "^cedar-headunit " "$log" | grep -qx "ab5301b bt disconnect" || fail "phone link not dropped after the receiver"
grep -q 'dnsmasq --conf-file=/dev/null --port=0 --interface=wlan0 --bind-interfaces' "$log" || fail "dnsmasq scope"
! kill -0 "$(cat "$out/hostapd.seen")" 2>/dev/null || fail "hostapd still running after exit"
[[ ! -e $out/tree/var/run/android-auto/hostapd.pid && ! -e $out/tree/var/run/android-auto/dnsmasq.pid ]] || fail "pid files left"
[[ $(tail -n 1 "$log") == 'ip addr flush dev wlan0' ]] || fail "address not dropped last: $(tail -n 1 "$log")"
! grep -q 'link set wlan0 down' "$log" || fail "wlan0 taken down (loses the boot pre-warm)"

run || fail "second run failed"
[[ $(sed -n 's/^wpa_passphrase=//p' "$conf") == "$psk" ]] || fail "passphrase changed on second run"
status=0; RECEIVER_STATUS=3 run || status=$?
[[ $status == 3 ]] || fail "receiver status not returned ($status)"
# hostapd failing while the driver warms up is retried, then succeeds.
rm -f "$out/hostapd.fails"; HOSTAPD_FAILS=2 run || fail "retry after 2 hostapd failures"
[[ $(grep -c "^hostapd " "$log") -ge 3 ]] || fail "hostapd not retried"
# Still failing after 15 attempts: give up.
rm -f "$out/hostapd.fails"; status=0; HOSTAPD_FAILS=99 run || status=$?
[[ $status == 1 && $(cat "$out/hostapd.fails") == 15 ]] || fail "hostapd give-up ($status, $(cat "$out/hostapd.fails"))"
rm -r "$out/tree/sys/class/net/wlan0"; status=0; run 2>/dev/null || status=$?
[[ $status == 2 ]] || fail "missing wlan0 not reported ($status)"
echo "PASS: on-demand AP config, receiver config/arguments, exit status, AP teardown with wlan0 kept up and hostapd retry"
