/* SPDX-License-Identifier: GPL-2.0-only */
#include "lvgl/lvgl.h"
#include "lvgl/src/draw/sw/lv_draw_sw_gradient.h"
#include <assert.h>
#include <stdio.h>
static void check(const lv_grad_dsc_t *descriptor,unsigned width,unsigned height) {
    lv_grad_t *gradient=lv_gradient_get(descriptor,width,height);assert(gradient);
    unsigned size=descriptor->dir==LV_GRAD_DIR_HOR?width:height;
    for(unsigned i=0;i<size;i++) {
        lv_color_t expected=lv_gradient_calculate(descriptor,size,i);
        if(gradient->map[i].full!=expected.full) {
            fprintf(stderr,"Gradient cache mismatch: position %u, expected %08x, got %08x\n",i,expected.full,gradient->map[i].full);
            assert(gradient->map[i].full==expected.full);
        }
    }
    lv_gradient_cleanup(gradient);
}
int main(void) {
    lv_init();
    /* LVGL's rectangle renderer reuses a stack descriptor for distinct icons
     * and for pressed-state animations. The pointer cannot identify its colors. */
    lv_grad_dsc_t gradient={.stops_count=2,.dir=LV_GRAD_DIR_VER};
    gradient.stops[0]=(lv_gradient_stop_t){lv_color_hex(0xff5500),0};
    gradient.stops[1]=(lv_gradient_stop_t){lv_color_hex(0x773300),255};
    check(&gradient,120,120);
    gradient.stops[0].color=lv_color_hex(0x22cc55);check(&gradient,120,120);
    gradient.stops[1].color=lv_color_hex(0x116633);check(&gradient,120,120);
    gradient.stops[0].frac=30;check(&gradient,120,120);
    gradient.stops[1].frac=220;check(&gradient,120,120);
    gradient.dir=LV_GRAD_DIR_HOR;check(&gradient,120,120);
    /* Force eviction with several dimensions and dynamic/pressed-state colors. */
    for(unsigned i=0;i<200;i++) {
        gradient.stops[0].color=lv_color_hex(i*23917);
        gradient.stops[1].color=lv_color_hex(i*13009);
        gradient.dir=i%2?LV_GRAD_DIR_HOR:LV_GRAD_DIR_VER;
        check(&gradient,110+i%31,100+i%57);
    }
    lv_gradient_free_cache();
    puts("PASS: gradient cache follows content changes at reused descriptor addresses, stops/direction, dynamic colors and eviction");
    return 0;
}
