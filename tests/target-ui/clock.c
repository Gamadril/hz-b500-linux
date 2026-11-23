/* SPDX-License-Identifier: GPL-2.0-only */
#include "lvgl/lvgl.h"
#include "ui_clock.h"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#if !LV_TICK_CUSTOM
#error "Native LVGL must use the monotonic clock"
#endif
int main(void) {
    uint32_t begin=lv_tick_get();
    usleep(30000); /* rendering/IO time with no main-loop tick increments */
    uint32_t end=lv_tick_get(),native=hz_ui_milliseconds();
    assert(end-begin>=20 && end-begin<1000);
    assert(native-end<10);
    puts("PASS: native LVGL animation clock advances during work without main-loop tick increments");
    return 0;
}
