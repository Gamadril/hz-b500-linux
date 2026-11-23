#include "rk_offline.h"
#include <stdio.h>

void led_init(void) { fprintf(stderr, "[preview] GPIO initialization disabled\n"); }
void buzz_init(void) {}
void led_ctrl(int id, int enable) { (void)id; (void)enable; }
void buzz_ctrl(int id, int enable) { (void)id; (void)enable; }
void Font_Init(char *path, int size, lv_style_t *style)
{
    (void)path;
    lv_style_set_text_font(style, dashboard_font(size));
}
