#ifndef DASHBOARD_RK_OFFLINE_H
#define DASHBOARD_RK_OFFLINE_H
#include "system_fonts.h"
void led_init(void);
void buzz_init(void);
void led_ctrl(int id, int enable);
void buzz_ctrl(int id, int enable);
void Font_Init(char *path, int size, lv_style_t *style);
#endif
