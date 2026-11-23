#!/usr/bin/env bash
# Compare "hz-pack boot0-header" with the vendor update_boot0 (fed by
# the vendor script compiler) on the board's sys_config.fex and on variants
# that exercise the other code paths. Both binaries are kept only as references.
set -Eeuo pipefail
trap 'printf "FAIL at line %s: %s\n" "$LINENO" "$BASH_COMMAND" >&2' ERR
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$ROOT/out/boot0-header-check"
TOOLS="$ROOT/tools/pack-bintools/src"
BOOT0="$ROOT/device/config/chips/d1s/bin/boot0_sdcard_sun20iw1p1.bin"
FEX="$ROOT/device/config/chips/d1s/configs/hz-b500_sd/sys_config.fex"
rm -rf "$OUT"
mkdir -p "$OUT"

# Each variant is "name|sed expression|[section]%line%line…": the sed
# expression edits a copy of the board file, and the extra lines are inserted
# right after that section's header.
variants=(
    'board||'
    'no-card2|/^\[card2_boot_para\]/,/^\[/{/^\[card2_boot_para\]/d;/^\[/!d}|'
    'card0-8bit|s/^card_line = 4/card_line = 8/|[card0_boot_para]%sdc_d4 = port:PF6<2><1><default><default>%sdc_d5 = port:PF7<2><1><default><default>%sdc_d6 = port:PF8<2><1><default><default>%sdc_d7 = port:PF9<2><1><default><default>%sdc_emmc_rst = port:PF10<2><1><default><default>%sdc_ds = port:PF11<2><1><default><default>'
    'card0-4bit-emmc-pins|s/^card_line = 4/card_line = 4/|[card0_boot_para]%sdc_emmc_rst = port:PF10<2><1><default><default>%sdc_ds = port:PF11<2><1><default><default>'
    'no-debug-mode|/^debug_mode/d|'
    'hex-negative|s/^dram_clk *= *528/dram_clk = 0x210/;s/^dram_tpr4 *= *0/dram_tpr4 = -5/|'
    'uppercase-pins|s/^sdc_clk = port:PF2/SDC_CLK = port:PF2/;s/^sdc_cmd = port:PF3/SDC_CMD = port:PF3/|'
    'short-gpio|s/^uart_debug_tx = .*/uart_debug_tx = port:PE2<6>/;s/^jtag_ms = .*/jtag_ms = port:PE4<7><none><null>/|'
    'tuning|s/^card_high_speed = 1/card_high_speed = 0/|[card0_boot_para]%sdc_2xmode = 1%sdc_ddrmode = 0%sdc_f_max = 50%sdc_ex_dly_used = 2%sdc_odly_25M = 3%sdc_sdly_25M = 4%sdc_odly_50M = 5%sdc_sdly_50M = 6'
    'jtag-on|s/^jtag_enable = 0/jtag_enable = 1/;s/^uart_debug_port = 0/uart_debug_port = 1/|'
    'power-mode|/^\[target\]/a power_mode = 1|'
)
make_variant() {
    local name=$1 expr=$2 extra=$3 out=$4 section lines
    tr -d '\r' < "$FEX" > "$out"
    [[ -z $expr ]] || sed -i -e "$expr" "$out"
    if [[ -n $extra ]]; then
        section=${extra%%%*}
        lines=${extra#*%}
        awk -v s="$section" -v add="$lines" '
            { print }
            $0 == s { n = split(add, a, "%"); for (i = 1; i <= n; i++) print a[i] }' "$out" > "$out.tmp"
        mv "$out.tmp" "$out"
    fi
    # The vendor script/update_boot0 pair needs CRLF lines, as the pack provides.
    sed -i 's/$/\r/' "$out"
}

count=0
for v in "${variants[@]}"; do
    IFS='|' read -r name expr extra <<< "$v"
    dir="$OUT/$name"
    mkdir -p "$dir"
    make_variant "$name" "$expr" "$extra" "$dir/sys_config.fex"
    cp "$BOOT0" "$dir/vendor.fex"
    cp "$BOOT0" "$dir/open.fex"
    (cd "$dir"; "$TOOLS/script" sys_config.fex > script.log 2>&1
     "$TOOLS/update_boot0" vendor.fex sys_config.bin SDMMC_CARD > update_boot0.log 2>&1)
    # Guard against comparing two unpatched copies.
    ! cmp -s "$BOOT0" "$dir/vendor.fex"
    "$ROOT/scripts/hz-pack" boot0-header "$dir/open.fex" "$dir/sys_config.fex"
    if ! cmp "$dir/vendor.fex" "$dir/open.fex"; then
        printf 'FAIL: variant %s differs from update_boot0\n' "$name" >&2
        exit 1
    fi
    count=$((count + 1))
done
# The variant edits must really change the patched header.
[[ $(cmp -l "$OUT/board/open.fex" "$OUT/card0-8bit/open.fex" | wc -l) -gt 0 ]]
[[ $(cmp -l "$OUT/board/open.fex" "$OUT/tuning/open.fex" | wc -l) -gt 0 ]]
printf 'PASS: %s sys_config variants patch Boot0 identically to update_boot0\n' "$count"
