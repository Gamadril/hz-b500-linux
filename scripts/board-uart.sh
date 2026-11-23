#!/usr/bin/env bash
# One persistent UART0 owner; commands/transfers use a framed shell protocol.
set -Eeuo pipefail
export LC_ALL=C
board_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
fail() { printf 'board-uart: %s\n' "$*" >&2;exit 1; }
usage() {
    cat <<'HELP'
Usage:
  bash scripts/board-uart.sh run CONSOLE_DEVICE CAN_DEVICE [NEW_SESSION_DIR]
  bash scripts/board-uart.sh status SESSION
  bash scripts/board-uart.sh wake SESSION [SECONDS]
  bash scripts/board-uart.sh exec SESSION 'SHELL COMMAND' [SECONDS]
  bash scripts/board-uart.sh put SESSION LOCAL_FILE [NEW_REMOTE_PATH]
  bash scripts/board-uart.sh install SESSION PACKAGE.ipk [SERVICE]
  bash scripts/board-uart.sh deploy SESSION EXECUTABLE REMOTE_EXECUTABLE SERVICE
  bash scripts/board-uart.sh test SESSION [BAUD]
  bash scripts/board-uart.sh interrupt SESSION
  bash scripts/board-uart.sh stop SESSION

run stays active, capturing UART0 at 115200 8N1 before/during boot.
CAN simulation uses its separate supplied adapter at 750000 baud.
Commands require the root UART shell. wake sends Enter, never a password.
put publishes only after byte-count and SHA-256 checks; existing files are
not replaced. install uses target opkg and optionally restarts one service.
Timed-out commands are not replayed. interrupt sends Ctrl-C and resynchronizes.
Host prerequisites: Bash 5, socat, flock, jq, base64, sha256sum, GNU coreutils.
Target image needs base64, sha256sum, mktemp and opkg for package installation.
deploy sends a compressed executable, keeps a target backup and restarts its
service; it leaves package metadata unchanged. Use it for HMI-only iterations
without retransmitting the packaged movie/fonts. put is limited to 8 MiB.
HELP
}
live() {
    [[ $(cat "$session/state" 2>/dev/null) == running ]] || return 1
    local beat
    beat=$(stat -c %Y "$session/heartbeat") || return 1
    ((EPOCHSECONDS-beat<=3))
}
load_session() {
    [[ -d $1 ]] || fail "No session: $1"
    session=$(cd -- "$1" && pwd)
    live || fail "Session is not active: $session"
}
request_lock() {
    exec 7>"$session/command.lock"
    flock -w 5 7 || fail 'Another command or transfer owns this console'
    [[ ! -e $session/pending ]] || fail 'Previous command timed out; inspect its log, then use interrupt'
}
run() {
    (($#>=2 && $#<=3)) || fail 'run needs console and CAN device paths'
    local console can original reader writer
    console=$(readlink -f -- "$1");can=$(readlink -f -- "$2")
    [[ -c $console && -r $console && -w $console ]] || fail 'Console must be a readable/writable character device'
    [[ -c $can && -r $can && -w $can ]] || fail 'CAN adapter must be a readable/writable character device'
    [[ ! $console -ef $can ]] || fail 'Console and CAN adapters must be distinct'
    session=${3:-"$board_root/out/hardware-test/$(date -u +%Y%m%dT%H%M%S)-$$"}
    mkdir -p -- "$(dirname -- "$session")"
    mkdir -- "$session" || fail 'Use a new session directory'
    session=$(cd -- "$session" && pwd)
    mkdir "$session/commands" "$session/transfers"
    printf '%s\n' "$console" > "$session/console-device"
    printf '%s\n' "$can" > "$session/can-device"
    printf 'starting\n' > "$session/state"
    exec 9<>"$console"
    flock -n 9 || fail 'Console is already owned by another session'
    original=$(stty -g <&9)
    stty raw 115200 cs8 -parenb -cstopb -crtscts -ixon -ixoff -echo clocal -hupcl <&9
    mkfifo "$session/tx" "$session/control"
    exec 8<>"$session/tx"
    exec 6<>"$session/control"
    local children=()
    cleanup() {
        trap - EXIT INT TERM HUP
        local pid attempt alive
        for pid in "${children[@]}";do kill "$pid" 2>/dev/null || :;done
        for attempt in {1..20};do
            alive=0
            for pid in "${children[@]}";do if kill -0 "$pid" 2>/dev/null;then alive=1;fi;done
            ((alive)) || break
            sleep 0.05
        done
        for pid in "${children[@]}";do
            if kill -0 "$pid" 2>/dev/null;then
                kill -KILL "$pid" 2>/dev/null || :
                printf 'Forced stop of transport PID %s; inspect capture tail\n' "$pid" >> "$session/transport-errors.log"
            fi
        done
        for pid in "${children[@]}";do wait "$pid" 2>/dev/null || :;done
        stty "$original" <&9 || :
        printf 'stopped\n' > "$session/state"
        exec 9>&- 8>&- 6>&-
    }
    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
    socat -u -d -d -v -x FD:9 STDOUT < /dev/null > "$session/console.bin" 2> "$session/console.log" 8>&- 6>&- &
    reader=$!;children+=("$reader")
    # socat handles partial/EAGAIN writes on USB UARTs with small TX queues.
    socat -u FD:8 FD:9 < /dev/null > /dev/null 2> "$session/writer.log" 6>&- &
    writer=$!;children+=("$writer")
    printf '%s\n' "$$" "$reader" "$writer" > "$session/processes"
    sleep 0.1
    kill -0 "$reader" "$writer" || fail 'Console transport failed to start'
    touch "$session/heartbeat"
    printf 'running\n' > "$session/state"
    printf 'READY: %s\nUART0: %s (115200)\nCAN: %s (750000)\n' "$session" "$console" "$can"
    local control
    while :;do
        touch "$session/heartbeat"
        if IFS= read -r -t 0.2 -u 6 control && [[ $control == stop ]];then break;fi
        kill -0 "$reader" "$writer" 2>/dev/null || fail 'Console disconnected; logs preserved'
    done
    cleanup
}
# Command output is base64 tagged by a unique token and hashed at the target.
# Echoed input and interleaved boot/kernel lines cannot impersonate a response.
remote() {
    local command=$1 limit=${2:-30} token encoded command_hash offset start request line result digest found=0
    [[ $limit =~ ^[0-9]+$ ]] && ((limit>=1 && limit<=600)) || fail 'Timeout must be 1..600 seconds'
    live || fail 'Console session stopped'
    token="$(date +%s%N)-$BASHPID-$RANDOM"
    encoded=$(printf '%s' "$command" | base64 -w0)
    command_hash=$(printf '%s' "$command" | sha256sum | cut -d' ' -f1)
    request="o=\$(mktemp /tmp/hz-uart.XXXXXXXX) && { printf '%s' '$encoded' | base64 -d >\"\$o.cmd\"; h=\$(sha256sum \"\$o.cmd\"); if [ \"\${h%% *}\" = '$command_hash' ]; then sh \"\$o.cmd\" >\"\$o\" 2>&1; r=\$?; else printf 'Command input checksum failed\\n' >\"\$o\"; r=125; fi; printf '\\n__HZ_BEGIN_%s\\n' '$token'; base64 \"\$o\" | sed 's/^/__HZ_DATA_$token:/'; h=\$(sha256sum \"\$o\"); h=\${h%% *}; printf '\\n__HZ_DONE_%s:%s:%s\\n' '$token' \"\$r\" \"\$h\"; rm -f \"\$o\" \"\$o.cmd\"; }"
    ((${#request}<3800)) || fail 'Command is too long for the UART shell; transfer a script instead'
    printf '%s\n' "$command" > "$session/commands/$token.command"
    printf '%s\n' "$token" > "$session/pending"
    offset=$(stat -c %s "$session/console.bin")
    printf '%s\n' "$request" > "$session/tx"
    start=$SECONDS
    while ((SECONDS-start<limit));do
        live || fail 'Console stopped during command'
        tail -c +"$((offset+1))" "$session/console.bin" | tr -d '\r' > "$session/commands/$token.wire"
        while IFS= read -r line;do
            if [[ $line =~ ^__HZ_DONE_${token}:([0-9]+):([0-9a-f]{64})$ ]];then
                result=${BASH_REMATCH[1]};digest=${BASH_REMATCH[2]};found=1;break
            fi
        done < "$session/commands/$token.wire"
        ((found)) && break
        sleep 0.05
    done
    if ((!found));then
        printf 'board-uart: command timed out (not replayed): %s\n' "$token" >&2
        return 124
    fi
    # BusyBox's editor can wrap an echoed request at a DATA prefix. Only
    # accept data after the actual response BEGIN marker, through its DONE.
    awk -v b="__HZ_BEGIN_$token" -v d="__HZ_DONE_$token:" -v p="__HZ_DATA_$token:" \
        '$0==b {reply=1;next} reply && index($0,d)==1 {exit} reply && index($0,p)==1 {print substr($0,length(p)+1)}' \
        "$session/commands/$token.wire" > "$session/commands/$token.base64"
    base64 -d "$session/commands/$token.base64" > "$session/commands/$token.out" || fail 'Corrupt command output'
    [[ $(sha256sum "$session/commands/$token.out" | cut -d' ' -f1) == "$digest" ]] || fail 'Command output SHA-256 mismatch'
    printf '%s\n' "$result" > "$session/commands/$token.exit"
    rm -f "$session/pending"
    cat "$session/commands/$token.out"
    return "$result"
}
put() {
    local source=$1 destination=${2:-} token size digest part chunk index=0 data
    [[ -f $source ]] || fail "No regular input file: $source"
    token="$(date +%s%N)-$BASHPID"
    destination=${destination:-"/tmp/hz-uart-$token-${source##*/}"}
    [[ $destination =~ ^/[a-zA-Z0-9_./-]+$ ]] || fail 'Remote path must be absolute and contain only letters, digits, dot, slash, dash or underscore'
    size=$(stat -c %s "$source");digest=$(sha256sum "$source" | cut -d' ' -f1)
    ((size<=8388608)) || fail 'UART transfer is limited to 8 MiB; use deploy for an executable-only HMI update'
    part="$destination.part.$token"
    remote "[ ! -e '$destination' ] || exit 1; (set -C; : > '$part')" > /dev/null || return
    mkdir "$session/transfers/$token"
    split -b 1024 -d -a 8 -- "$source" "$session/transfers/$token/chunk-"
    for chunk in "$session/transfers/$token"/chunk-*;do
        [[ -f $chunk ]] || continue
        data=$(base64 -w0 "$chunk")
        remote "printf '%s' '$data' | base64 -d >> '$part'" > /dev/null || return
        index=$((index+1))
        if ((index%16==0));then printf 'Transferred %d / %d bytes\n' "$((index*1024))" "$size" >&2;fi
    done
    remote "h=\$(sha256sum '$part'); [ \"\${h%% *}\" = '$digest' ] && [ \"\$(wc -c < '$part')\" -eq '$size' ] && ln '$part' '$destination' && rm -f '$part' && printf '%s\\n' '$destination'" 30 || return
    printf '%s %s %s\n' "$digest" "$size" "$destination" > "$session/transfers/$token/verified"
}
wake() {
    local limit=${1:-90} start=$SECONDS
    [[ $limit =~ ^[0-9]+$ ]] || fail 'wake timeout must be seconds'
    while ((SECONDS-start<limit));do
        printf '\n' > "$session/tx"
        sleep 0.3
        if remote 'test "$(id -u)" = 0 && printf "ROOT_SHELL_READY\n"' 3;then return 0;fi
        # Probing is read-only. Forget its nonce before another boot-time probe.
        rm -f "$session/pending"
        sleep 0.7
    done
    fail 'No root shell; inspect console.bin (automatic login/password entry is not attempted)'
}
hardware_test() {
    source "$board_root/scripts/lib/board-uart-test.sh"
    board_hardware_test "$@"
}
operation=${1:-help};shift || :
case $operation in
    help|--help|-h) usage;;
    run) run "$@";;
    status) (($#==1)) || fail 'status needs SESSION';load_session "$1";printf 'running: %s\n' "$session";;
    stop) (($#==1)) || fail 'stop needs SESSION';load_session "$1";printf 'stop\n' > "$session/control";;
    wake|exec|put|install|deploy|test|interrupt)
        (($#>=1)) || fail "$operation needs SESSION"
        load_session "$1";shift
        if [[ $operation == interrupt ]];then
            exec 7>"$session/command.lock";flock -w 5 7 || fail 'Console command is still active'
            printf '\003\n' > "$session/tx";sleep 0.2;rm -f "$session/pending";wake 10;exit
        fi
        request_lock
        case $operation in
            wake) wake "${1:-90}";;
            exec) (($#>=1 && $#<=2)) || fail 'exec needs COMMAND [SECONDS]';remote "$@";;
            put) (($#>=1 && $#<=2)) || fail 'put needs FILE [NEW_REMOTE_PATH]';put "$@";;
            deploy)
                (($#==3)) || fail 'deploy needs EXECUTABLE REMOTE_EXECUTABLE SERVICE'
                [[ -f $1 && $2 =~ ^/[a-zA-Z0-9_./-]+$ && $3 =~ ^[a-zA-Z0-9_-]+$ ]] || fail 'Invalid executable path or service name'
                deployment="$(date +%s%N)-$BASHPID"
                gzip -n -c "$1" > "$session/transfers/$deployment.gz"
                destination=$(put "$session/transfers/$deployment.gz")
                digest=$(sha256sum "$1" | cut -d' ' -f1)
                remote "test -x '$2' && cp -p '$2' '$2.uart-backup-$deployment' && gunzip -c '$destination' > '$2.uart-new-$deployment' || exit 1; h=\$(sha256sum '$2.uart-new-$deployment'); [ \"\${h%% *}\" = '$digest' ] && chmod 755 '$2.uart-new-$deployment' || exit 1; /etc/init.d/$3 stop || exit 1; mv '$2.uart-new-$deployment' '$2' || { /etc/init.d/$3 start; exit 1; }; /etc/init.d/$3 start; r=\$?; sync; printf 'Backup: %s\\n' '$2.uart-backup-$deployment'; exit \$r" 60;;
            install)
                (($#>=1 && $#<=2)) || fail 'install needs PACKAGE.ipk [SERVICE]'
                [[ $1 == *.ipk ]] || fail 'Expected an .ipk package'
                destination=$(put "$1")
                service=${2:-}
                if [[ -z $service ]];then
                    case ${1##*/} in vehicle_*) service=vehicle;;hz-ui_*) service=hz-ui;;esac
                fi
                if [[ -n $service ]];then
                    [[ $service =~ ^[a-zA-Z0-9_-]+$ ]] || fail 'Invalid service name'
                    remote "/etc/init.d/$service stop || exit; opkg install '$destination'; r=\$?; /etc/init.d/$service start; exit \$r" 120
                else remote "opkg install '$destination'" 120;fi;;
            test) (($#<=1)) || fail 'test takes SESSION [BAUD]';hardware_test "$@";;
        esac;;
    *) usage >&2;exit 2;;
esac
