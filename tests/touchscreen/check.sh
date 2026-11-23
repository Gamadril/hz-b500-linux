#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${TOUCHSCREEN_CHECK_OUT:-${ROOT}/out/d1s-hz-b500_sd/touchscreen-irq-check}"
SOURCE="${ROOT}/lichee/linux-5.4/drivers/input/touchscreen/gt9xxnew/gt9xx.c"
mkdir -p "$OUT"
awk '/^static int gt91xx_config_show\(/ { copy=1 }
     /^static s8 gtp_request_irq\(/ { copy=1 }
     copy { print } copy && /^}/ { copy=0 }' "$SOURCE" > "$OUT/functions.inc"
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -I "$OUT" \
    "${ROOT}/tests/touchscreen/irq-config.c" -o "$OUT/irq-config"
"$OUT/irq-config"
awk '/^static int gtp_power_on_reset\(/ { copy=1 }
     /^static int gtp_read_ready_report\(/ { copy=1 }
     copy { print } copy && /^}/ { copy=0 }' "$SOURCE" > "$OUT/startup-functions.inc"
gcc -std=gnu11 -Wall -Wextra -Werror -I "$OUT" \
    "${ROOT}/tests/touchscreen/startup.c" -o "$OUT/startup"
"$OUT/startup"
awk '/^static int sunxi_pinctrl_get_debounce_div\(/ { copy=1 }
     /^static int sunxi_pinctrl_setup_debounce\(/ { copy=1 }
     copy { print } copy && /^}/ { copy=0 }' \
    "${ROOT}/lichee/linux-5.4/drivers/pinctrl/sunxi/pinctrl-sunxi.c" > "$OUT/clock-functions.inc"
awk '/^static inline u32 sunxi_irq_hw_bank_num\(/ { copy=1 }
     /^static inline u32 sunxi_irq_debounce_reg_from_bank\(/ { copy=1 }
     /^#define IRQ_DEBOUNCE_REG/ || /^#define IRQ_MEM_SIZE/ { print }
     copy { print } copy && /^}/ { copy=0 }' \
    "${ROOT}/lichee/linux-5.4/drivers/pinctrl/sunxi/pinctrl-sunxi.h" > "$OUT/clock-registers.inc"
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare -Wno-pointer-sign -I "$OUT" \
    "${ROOT}/tests/touchscreen/clock.c" -o "$OUT/clock"
"$OUT/clock"
