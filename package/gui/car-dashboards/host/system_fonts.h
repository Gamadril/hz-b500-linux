#ifndef DASHBOARD_SYSTEM_FONTS_H
#define DASHBOARD_SYSTEM_FONTS_H
#include "lvgl/lvgl.h"

extern lv_font_t dashboard_default_font;
void dashboard_fonts_init(void);
const lv_font_t *dashboard_font(unsigned size);
#endif
