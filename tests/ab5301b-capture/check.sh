#!/usr/bin/env bash
set -Eeuo pipefail
capture_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$capture_root"
capture_out="$capture_root/out/ab5301b-capture/tests"
mkdir -p "$capture_out"
capture_run=$(mktemp -d "$capture_out/run.XXXXXXXX")
cc -std=gnu11 -Wall -Wextra -Werror tests/ab5301b-capture/session.c -lutil -o "$capture_out/session-test"
timeout 15s "$capture_out/session-test" "$capture_root/scripts/ab5301b-capture.sh" "$capture_run/session" > "$capture_run/test.log" 2>&1
grep -E '^PASS:' "$capture_run/test.log"
# Byte offsets in the marker log must refer to the raw per-direction files.
awk -F '\t' '$2=="BEGIN" {begin++;if($4!=2 || $5!=0)exit 1} $2=="DONE" {done++;if($4!=6 || $5!=12)exit 1} END {if(begin!=1 || done!=1)exit 1}' "$capture_run/session/events.tsv"
mapfile -t capture_frames < <(printf '%s\n' "$capture_run"/session/frames-*.tsv)
[[ ${#capture_frames[@]} == 1 ]]
awk -F '\t' 'NR>1 {frames++;if($7!="VALID")exit 1} END {if(frames!=3)exit 1}' "${capture_frames[0]}"
grep -qE 'length=.*from=0 to=' "$capture_run/session/f133-to-ab5301b.log"
printf 'PASS: XOR-checked decoded frames and action byte offsets match the original raw evidence\n'
mkdir "$capture_run/malformed"
: > "$capture_run/malformed/events.tsv"
printf '\xff\x02\x80\xaa\x01\x00\x00\x02\x80\xc3\x00\x43\x02\x80\xaa' > "$capture_run/malformed/f133-to-ab5301b.bin"
: > "$capture_run/malformed/ab5301b-to-f133.bin"
capture_bad=$(bash scripts/ab5301b-capture.sh decode "$capture_run/malformed")
awk -F '\t' 'NR>1 {result[$7]++} END {if(result["BAD"]!=1 || result["VALID"]!=1 || result["TRUNCATED"]!=1 || !result["STRAY"])exit 1}' "$capture_bad"
printf 'PASS: decoder flags checksum errors, stray bytes and truncation and recovers the next valid frame\n'
printf 'Evidence: %s\n' "$capture_run"
