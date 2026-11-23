#!/usr/bin/env bash
# Passive, two-direction AB5301B capture. No writes are made to either UART.
set -Eeuo pipefail
export LC_ALL=C TZ=UTC
capture_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
(( BASH_VERSINFO[0]>=5 )) || { printf 'Bash 5 or later is required\n' >&2;exit 1; }
usage() {
    cat <<'HELP'
Usage:
  bash scripts/ab5301b-capture.sh run F133_TX_DEVICE AB5301B_TX_DEVICE [NEW_SESSION_DIR]
  bash scripts/ab5301b-capture.sh status SESSION_DIR
  bash scripts/ab5301b-capture.sh begin SESSION_DIR 'Action and initial/target settings'
  bash scripts/ab5301b-capture.sh done SESSION_DIR ['Observed result, sound, displayed values']
  bash scripts/ab5301b-capture.sh note SESSION_DIR 'Additional observation'
  bash scripts/ab5301b-capture.sh stop SESSION_DIR
  bash scripts/ab5301b-capture.sh decode SESSION_DIR

run stays active: launch it in a managed background terminal before booting Melis.
Port order is F133 -> AB5301B, then AB5301B -> F133. Format: 115200 8N1.
Leave both adapters' TX pins disconnected; connect RX probes and common GND.
Logs and markers use UTC host arrival time, not precise wire timing.
HELP
}
fail() { printf '%s\n' "$*" >&2;exit 1; }
size() { if [[ -f $1 ]]; then stat -c %s -- "$1";else printf '0\n';fi; }
# Called with the event lock held. Fields cannot contain tabs/newlines.
event() {
    local kind=$1 action=$2 description=$3
    description=${description//$'\t'/ };description=${description//$'\n'/ };description=${description//$'\r'/ }
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$(date -u +%Y-%m-%dT%H:%M:%S.%NZ)" \
        "$kind" "$action" "$(size "$session/f133-to-ab5301b.bin")" \
        "$(size "$session/ab5301b-to-f133.bin")" "$description" >> "$session/events.tsv"
}
locked_event() { (flock -x 8;event "$@") 8>"$session/events.lock"; }
live() {
    [[ $(cat "$session/state" 2>/dev/null) == running ]] || return 1
    local beat=0
    beat=$(stat -c %Y -- "$session/heartbeat" 2>/dev/null) || return 1
    [[ $beat =~ ^[0-9]+$ ]] && (( EPOCHSECONDS>=beat && EPOCHSECONDS-beat<=3 ))
}
run() {
    (($#>=2 && $#<=3)) || fail 'run requires two device paths and an optional new session directory'
    command -v socat >/dev/null || fail 'socat is required'
    local f133_device ab_device
    f133_device=$(readlink -f -- "$1");ab_device=$(readlink -f -- "$2")
    [[ -c $f133_device && -r $f133_device && -c $ab_device && -r $ab_device ]] || fail 'Both inputs must be readable serial character devices'
    [[ ! $f133_device -ef $ab_device ]] || fail 'Two distinct serial interfaces are required'
    [[ $f133_device != *[:,]* && $ab_device != *[:,]* ]] || fail 'Device paths must not contain colon or comma'
    session=${3:-"$capture_root/out/ab5301b-capture/$(date -u +%Y%m%dT%H%M%S)-$$"}
    mkdir -p -- "$(dirname -- "$session")"
    mkdir -- "$session" || fail 'Use a new session directory; captures are never overwritten'
    session=$(cd -- "$session" && pwd)
    printf 'starting\n' > "$session/state"
    printf 'utc\tevent\taction_id\tf133_tx_bytes\tab5301b_tx_bytes\tdescription\n' > "$session/events.tsv"
    printf 'direction\tdevice\tformat\nF133->AB5301B\t%s\t115200 8N1 no flow control\nAB5301B->F133\t%s\t115200 8N1 no flow control\n' \
        "$f133_device" "$ab_device" > "$session/ports.tsv"
    socat -V > "$session/recorder-version.txt" 2>&1
    : > "$session/current-action.tsv"
    mkfifo -- "$session/control"
    exec 9<>"$session/control"
    # These survive run's return so the EXIT trap can close the readers.
    readers=()
    ending=STOPPED
    local result=0
    cleanup() {
        trap - EXIT INT TERM HUP
        local pid attempt alive forced=0
        for pid in "${readers[@]}";do kill "$pid" 2>/dev/null || :;done
        # Bound shutdown even if a USB driver/reader does not finish on TERM.
        for attempt in {1..20};do
            alive=0
            for pid in "${readers[@]}";do if kill -0 "$pid" 2>/dev/null;then alive=1;fi;done
            ((alive)) || break
            sleep 0.05
        done
        for pid in "${readers[@]}";do
            if kill -0 "$pid" 2>/dev/null;then kill -KILL "$pid" 2>/dev/null || :;forced=1;fi
        done
        for pid in "${readers[@]}";do wait "$pid" 2>/dev/null || :;done
        if ((forced));then locked_event FORCED_READER_STOP - 'Reader did not finish on TERM; check capture tail';fi
        locked_event "SESSION_$ending" - 'Readers closed; final byte counts'
        printf '%s\n' "${ending,,}" > "$session/state"
        exec 9>&-
    }
    trap cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
    local index direction device
    for index in 0 1;do
        if ((index==0));then direction=f133-to-ab5301b;device=$f133_device
        else direction=ab5301b-to-f133;device=$ab_device;fi
        socat -u -d -d -v -x \
            "OPEN:$device,rdonly,rawer,b115200,cs8,parenb=0,cstopb=0,crtscts=0,ixon=0,ixoff=0,ixany=0,clocal=1,hupcl=0" \
            STDOUT < /dev/null > "$session/$direction.bin" 2> "$session/$direction.log" 9>&- &
        readers+=("$!")
    done
    # Do not declare ready merely because the processes were spawned.
    local ready=0 attempt
    for attempt in {1..40};do
        ready=0
        for direction in f133-to-ab5301b ab5301b-to-f133;do
            if grep -qE 'starting data transfer loop' "$session/$direction.log";then ready=$((ready+1));fi
        done
        ((ready==2)) && break
        sleep 0.05
    done
    if ((ready!=2)) || ! kill -0 "${readers[0]}" 2>/dev/null || ! kill -0 "${readers[1]}" 2>/dev/null;then
        ending=ERROR;fail "Recorder failed to open both ports; inspect $session/*.log"
    fi
    locked_event SESSION_STARTED - 'Both receive-only serial readers ready'
    touch -- "$session/heartbeat"
    printf 'running\n' > "$session/state"
    printf 'READY: %s\nF133 -> AB5301B: %s\nAB5301B -> F133: %s\n' "$session" "$f133_device" "$ab_device"
    local request
    while :;do
        touch -- "$session/heartbeat"
        if IFS= read -r -t 0.25 -u 9 request && [[ $request == stop ]];then break;fi
        for index in 0 1;do
            if ! kill -0 "${readers[index]}" 2>/dev/null;then
                ending=ERROR;result=1
                printf 'Serial reader exited: %s; capture is incomplete\n' "${readers[index]}" >&2
                break 2
            fi
        done
    done
    return "$result"
}
decode() {
    # Raw evidence remains untouched; each run creates a separate decoded table.
    local output="$session/frames-$(date -u +%Y%m%dT%H%M%S)-$$.tsv"
    printf 'direction\toffset\tclass\tcommand\tlength\tpayload\txor\tframe\n' > "$output"
    local direction offset length end index checksum payload frame status
    local -a bytes
    for direction in f133-to-ab5301b ab5301b-to-f133;do
        mapfile -t bytes < <(od -An -v -tu1 -w1 -- "$session/$direction.bin")
        offset=0
        while ((offset<${#bytes[@]}));do
            if (( bytes[offset]!=2 || offset+1>=${#bytes[@]} ||
                  (bytes[offset+1]!=128 && bytes[offset+1]!=129) ));then
                printf '%s\t%d\t-\t-\t-\t-\tSTRAY\t%02X\n' "$direction" "$offset" "${bytes[offset]}" >> "$output"
                offset=$((offset+1));continue
            fi
            if ((offset+4>=${#bytes[@]}));then
                printf '%s\t%d\t-\t-\t-\t-\tTRUNCATED\t-\n' "$direction" "$offset" >> "$output";break
            fi
            length=${bytes[offset+3]};end=$((offset+length+5))
            if ((end>${#bytes[@]}));then
                printf '%s\t%d\t-\t-\t%d\t-\tTRUNCATED\t-\n' "$direction" "$offset" "$length" >> "$output";break
            fi
            checksum=0;payload=;frame=
            for ((index=offset;index<end;index++));do
                printf -v frame '%s%02X ' "$frame" "${bytes[index]}"
                if ((index>offset && index<end-1));then checksum=$((checksum ^ bytes[index]));fi
                if ((index>=offset+4 && index<end-1));then printf -v payload '%s%02X ' "$payload" "${bytes[index]}";fi
            done
            status=VALID
            if ((checksum!=bytes[end-1]));then status=BAD;fi
            printf '%s\t%d\t%02X\t%02X\t%d\t%s\t%s\t%s\n' "$direction" "$offset" \
                "${bytes[offset+1]}" "${bytes[offset+2]}" "$length" "${payload% }" "$status" "${frame% }" >> "$output"
            # On corruption rescan, rather than trusting a possibly damaged LEN.
            if [[ $status == VALID ]];then offset=$end;else offset=$((offset+1));fi
        done
    done
    printf '%s\n' "$output"
}
(($#)) || { usage;exit 2; }
command=$1;shift
if [[ $command == run ]];then run "$@";exit;fi
if [[ $command == --help || $command == help ]];then usage;exit;fi
(($#)) || fail 'A session directory is required'
session=$1;shift
[[ -d $session && -f $session/events.tsv ]] || fail 'Not a capture session'
case "$command" in
    status)
        cat "$session/ports.tsv"
        printf 'state: %s; F133 TX: %s bytes; AB5301B TX: %s bytes\n' "$(cat "$session/state")" \
            "$(size "$session/f133-to-ab5301b.bin")" "$(size "$session/ab5301b-to-f133.bin")"
        if [[ $(cat "$session/state") == running ]];then live || fail 'Recorder heartbeat is stale; do not assume capture is active';fi
        ;;
    begin|done|note)
        live || fail 'Recorder is not running'
        exec 8>"$session/events.lock";flock -x 8
        action=;label=
        IFS=$'\t' read -r action label < "$session/current-action.tsv" || :
        if [[ $command == begin ]];then
            (($#)) || fail 'An action description is required'
            [[ -z $action ]] || fail 'Finish the previous action with done first'
            action="$(date -u +%Y%m%dT%H%M%S.%N)"
            label="$*";label=${label//$'\t'/ };label=${label//$'\n'/ };label=${label//$'\r'/ }
            event BEGIN "$action" "$label"
            printf '%s\t%s\n' "$action" "$label" > "$session/current-action.tsv"
        elif [[ $command == done ]];then
            [[ -n $action ]] || fail 'No announced action is pending'
            event DONE "$action" "$label${*:+; $*}"
            : > "$session/current-action.tsv"
        else
            (($#)) || fail 'A note is required'
            event NOTE "${action:--}" "$*"
        fi
        tail -n 1 "$session/events.tsv"
        ;;
    stop)
        live || fail 'Recorder is not running'
        timeout 2s bash -c 'printf "stop\n" > "$1/control"' _ "$session"
        ;;
    decode) decode ;;
    *) usage;exit 2 ;;
esac
