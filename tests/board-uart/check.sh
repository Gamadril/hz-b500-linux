#!/usr/bin/env bash
set -Eeuo pipefail
check_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$check_root"
mkdir -p out/board-uart/tests
check_out=$(mktemp -d "$check_root/out/board-uart/tests/run.XXXXXXXX")
check_session="$check_out/session"
check_script="$check_root/scripts/board-uart.sh"
mkdir "$check_out/bin"
cat > "$check_out/bin/id" <<'MOCK'
#!/usr/bin/env bash
if [[ ${1:-} == -u ]];then printf '0\n';else /usr/bin/id "$@";fi
MOCK
cat > "$check_out/bin/opkg" <<'MOCK'
#!/usr/bin/env bash
printf '%s\n' "$*" > "$BOARD_UART_MOCK_OPKG"
printf 'Mock package installed\n'
MOCK
cat > "$check_out/bin/sha256sum" <<'MOCK'
#!/usr/bin/env bash
if [[ -e $BOARD_UART_MOCK_BAD_HASH && $1 == *.part.* ]];then
    printf '%064d  %s\n' 0 "$1"
elif [[ -e $BOARD_UART_MOCK_BAD_INPUT && $1 == *.cmd ]];then
    printf '%064d  %s\n' 0 "$1"
else /usr/bin/sha256sum "$@";fi
MOCK
cat > "$check_out/bin/sh" <<'MOCK'
#!/usr/bin/env bash
if [[ ${1:-} == -c ]];then
    code=${2//\/etc\/init.d\//$BOARD_UART_MOCK_SERVICES/}
    exec /bin/sh -c "$code"
fi
code=$(cat "$1")
code=${code//\/etc\/init.d\//$BOARD_UART_MOCK_SERVICES/}
# Reproduce an editor wrapping echoed shell text at a response DATA prefix,
# before the real BEGIN marker. This is deliberately invalid base64.
if [[ -s $BOARD_UART_MOCK_SESSION/pending ]];then
    read -r token < "$BOARD_UART_MOCK_SESSION/pending"
    printf '\n__HZ_DATA_%s:/\047; wrapped shell echo\n' "$token" >/dev/tty
fi
exec /bin/sh -c "$code"
MOCK
chmod +x "$check_out/bin/id" "$check_out/bin/opkg" "$check_out/bin/sha256sum" "$check_out/bin/sh"
printf '#!/usr/bin/env bash\nexport PATH=%q\nexport HISTFILE=/dev/null PS1="TARGET> "\nprintf "BOOT_FAKE_UART0\\n"\nexec bash --noprofile --norc -i\n' \
    "$check_out/bin:$PATH" > "$check_out/peer.sh"
chmod +x "$check_out/peer.sh"
export BOARD_UART_MOCK_OPKG="$check_out/opkg.log"
export BOARD_UART_MOCK_BAD_HASH="$check_out/bad-hash"
export BOARD_UART_MOCK_BAD_INPUT="$check_out/bad-input"
export BOARD_UART_MOCK_SERVICES="$check_out/services"
export BOARD_UART_MOCK_SESSION="$check_session"
mkdir "$check_out/services"
printf '#!/bin/sh\nprintf "%%s\\n" "$1" >> "%s"\n' "$check_out/service.log" > "$check_out/services/demo"
chmod +x "$check_out/services/demo"
check_children=()
cleanup() {
    local pid
    for pid in "${check_children[@]}";do kill "$pid" 2>/dev/null || :;done
    sleep 0.1
    for pid in "${check_children[@]}";do kill -KILL "$pid" 2>/dev/null || :;done
    for pid in "${check_children[@]}";do wait "$pid" 2>/dev/null || :;done
}
trap cleanup EXIT
socat "PTY,link=$check_out/console,rawer" "EXEC:$check_out/peer.sh,pty,setsid,ctty" > "$check_out/peer.stdout" 2> "$check_out/peer.log" &
check_bridge=$!;check_children+=("$!")
for attempt in {1..100};do [[ ! -e $check_out/console ]] || break;sleep 0.02;done
bash "$check_script" run "$check_out/console" /dev/null "$check_session" > "$check_out/monitor.stdout" 2> "$check_out/monitor.log" &
check_monitor=$!;check_children+=("$!")
for attempt in {1..100};do
    if [[ -f $check_session/state && $(cat "$check_session/state") == running ]];then break;fi
    sleep 0.02
done
bash "$check_script" wake "$check_session" 10 > "$check_out/wake.txt"
grep -qE ROOT_SHELL_READY "$check_out/wake.txt"
bash "$check_script" exec "$check_session" 'printf "kernel chatter\n" >/dev/tty; printf "hello\n__HZ_DONE_forged:0:fake\n"' > "$check_out/reply.txt"
printf 'hello\n__HZ_DONE_forged:0:fake\n' > "$check_out/reply.expected"
cmp "$check_out/reply.expected" "$check_out/reply.txt"
grep -qE 'kernel chatter' "$check_session/console.bin"
result=0
bash "$check_script" exec "$check_session" 'printf "nonzero\n"; exit 7' > "$check_out/nonzero.txt" || result=$?
[[ $result == 7 ]]
touch "$check_out/bad-input"
result=0
bash "$check_script" exec "$check_session" "touch '$check_out/must-not-exist'" > "$check_out/bad-input.txt" || result=$?
[[ $result == 125 && ! -e $check_out/must-not-exist ]]
rm "$check_out/bad-input"
printf '\x00\x01\x7f\xffquote\047 dollar$ backtick`\n' > "$check_out/payload.bin"
dd if=/dev/zero bs=1024 count=3 status=none >> "$check_out/payload.bin"
check_remote=$(bash "$check_script" put "$check_session" "$check_out/payload.bin")
cmp "$check_out/payload.bin" "$check_remote"
touch "$check_out/bad-hash"
check_rejected="/tmp/hz-uart-rejected-${check_out##*/}"
if bash "$check_script" put "$check_session" "$check_out/payload.bin" "$check_rejected" > "$check_out/rejected.txt" 2>&1;then exit 1;fi
[[ ! -e $check_rejected ]]
rm "$check_out/bad-hash"
if bash "$check_script" put "$check_session" "$check_out/payload.bin" "$check_remote" > /dev/null 2>&1;then exit 1;fi
cmp "$check_out/payload.bin" "$check_remote"
printf 'mock ipk content\n' > "$check_out/test_1.0.ipk"
bash "$check_script" install "$check_session" "$check_out/test_1.0.ipk" > "$check_out/install.txt"
grep -qE '^install /tmp/hz-uart-' "$check_out/opkg.log"
grep -qE 'Mock package installed' "$check_out/install.txt"
printf '#!/bin/sh\nprintf "old executable\\n"\n' > "$check_out/target-app"
cp "$check_out/target-app" "$check_out/old-app"
chmod +x "$check_out/target-app"
printf '#!/bin/sh\nprintf "new executable\\n"\n' > "$check_out/new-app"
bash "$check_script" deploy "$check_session" "$check_out/new-app" "$check_out/target-app" demo > "$check_out/deploy.txt"
cmp "$check_out/new-app" "$check_out/target-app"
check_backup=$(sed -n 's/^Backup: //p' "$check_out/deploy.txt")
cmp "$check_out/old-app" "$check_backup"
printf 'stop\nstart\n' > "$check_out/service.expected"
cmp "$check_out/service.expected" "$check_out/service.log"
printf 'PASS: compressed executable deployment verifies contents, retains old binary and stops/starts the requested service\n'
printf 'PASS: real PTY shell, continuous logs, root-shell wake, echo/chatter-safe commands, exit status, binary transfer, corrupt-hash rejection and package install invocation\n'
result=0
bash "$check_script" exec "$check_session" 'sleep 3; printf late' 1 > "$check_out/timeout.txt" || result=$?
[[ $result == 124 && -e $check_session/pending ]]
if bash "$check_script" exec "$check_session" 'printf must-not-run' > "$check_out/blocked.txt" 2>&1;then exit 1;fi
bash "$check_script" interrupt "$check_session" > "$check_out/interrupt.txt"
grep -qE ROOT_SHELL_READY "$check_out/interrupt.txt"
printf 'PASS: timed-out commands are not replayed; later commands fail closed until explicit interrupt/resynchronization\n'
bash "$check_script" stop "$check_session"
wait "$check_monitor"
[[ $(cat "$check_session/state") == stopped ]]
printf 'PASS: bounded session stop with preserved raw and timestamped UART logs\nEvidence: %s\n' "$check_out"
