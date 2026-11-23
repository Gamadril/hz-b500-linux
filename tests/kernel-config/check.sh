#!/usr/bin/env bash
# Compare a board's kernel config source with the .config Tina actually built.
# Tina adds CONFIG_KERNEL_* options from the target defconfig and package
# KCONFIG options; kconfig then drops symbols whose dependencies are unmet.
# The resulting difference is recorded in <board>.delta so it stays visible
# and cannot drift unnoticed.
#
# Usage: bash tests/kernel-config/check.sh [--update] [BOARD]
#   BOARD defaults to d1s-hz-b500_sd. KERNEL_CONFIG overrides the built
#   .config path. --update rewrites the recorded delta from the current build.
set -Eeuo pipefail
trap 'printf "FAIL at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
update=0
if [[ ${1:-} == --update ]]; then update=1; shift; fi
board=${1:-d1s-hz-b500_sd}
src=$ROOT/device/config/chips/d1s/configs/${board#d1s-}/linux/config-5.4
expected=$ROOT/tests/kernel-config/$board.delta
built=${KERNEL_CONFIG:-}
if [[ -z $built ]]; then
    for built in "$ROOT/out/$board/compile_dir/target/linux-$board"/linux-5.4.*/.config; do :; done
fi
[[ -f $src ]] || { echo "Missing kernel config source: $src" >&2; exit 1; }
[[ -f $built ]] || { echo "Missing built kernel config for $board; build the kernel first" >&2; exit 1; }

# One "SYMBOL<TAB>value" line per symbol; "is not set" is n; the last
# assignment wins, as in kconfig.
norm() {
    awk '/^CONFIG_[A-Za-z0-9_]+=/ { i = index($0, "="); v[substr($0, 1, i - 1)] = substr($0, i + 1); next }
         /^# CONFIG_[A-Za-z0-9_]+ is not set$/ { v[$2] = "n" }
         END { for (k in v) print k "\t" v[k] }' "$1" | LC_ALL=C sort
}
# A symbol missing on one side counts as n.
actual=$(LC_ALL=C join -t $'\t' -a 1 -a 2 -e n -o 0,1.2,2.2 <(norm "$src") <(norm "$built") |
    awk -F '\t' '$2 != $3 { print $1 " " $2 " -> " $3 }')

dups=$(grep -oE '^(# )?CONFIG_[A-Za-z0-9_]+' "$src" | sed 's/^# //' | LC_ALL=C sort | uniq -d | tr '\n' ' ')
[[ -z $dups ]] || printf 'Note: set more than once in %s (last wins): %s\n' "${src#"$ROOT"/}" "$dups"

if (( update )); then
    {
        printf '# Effective kernel config difference for %s: source -> built.\n' "$board"
        printf '# Source: %s\n' "${src#"$ROOT"/}"
        printf '# Regenerate with: bash tests/kernel-config/check.sh --update %s\n' "$board"
        printf '%s\n' "$actual"
    } > "$expected"
    printf 'Updated %s (%s differences)\n' "${expected#"$ROOT"/}" "$(grep -c . <<< "$actual")"
    exit 0
fi
[[ -f $expected ]] || { echo "No recorded delta for $board; run with --update" >&2; exit 1; }
if ! diff -u --label recorded --label built <(grep -v '^#' "$expected") <(printf '%s\n' "$actual"); then
    printf 'FAIL: kernel config delta for %s changed; review it, then rerun with --update\n' "$board" >&2
    exit 1
fi
printf 'PASS: %s kernel config matches the recorded delta (%s differences)\n' "$board" "$(grep -c . <<< "$actual")"
