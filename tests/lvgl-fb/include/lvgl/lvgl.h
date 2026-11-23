#ifndef TEST_LVGL_H
#define TEST_LVGL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int16_t lv_coord_t;
typedef union { uint32_t full; } lv_color_t;
typedef struct { lv_coord_t x1, y1, x2, y2; } lv_area_t;
typedef struct { bool flushing; } lv_disp_draw_buf_t;
typedef struct { lv_disp_draw_buf_t *draw_buf; } lv_disp_drv_t;
enum { LV_DISP_ROT_NONE, LV_DISP_ROT_90, LV_DISP_ROT_180, LV_DISP_ROT_270 };
void lv_disp_flush_ready(lv_disp_drv_t *drv);
bool lv_disp_flush_is_last(lv_disp_drv_t *drv);
#endif
