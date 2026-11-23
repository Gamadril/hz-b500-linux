/* SPDX-License-Identifier: GPL-2.0-only */
#include "page_header.h"
#include "navigation.h"
#include "system_fonts.h"

static void home(lv_event_t *event) {(void)event;hz_home_open();}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int size,uint32_t color)
{
    lv_obj_t *object=lv_label_create(parent);lv_label_set_text(object,text?text:"");
    lv_obj_set_style_text_font(object,dashboard_font(size),0);
    lv_obj_set_style_text_color(object,lv_color_hex(color),0);
    lv_obj_clear_flag(object,LV_OBJ_FLAG_CLICKABLE);return object;
}
struct hz_page_header hz_page_header_create(lv_obj_t *screen,const char *title,
    const char *context,lv_event_cb_t leave,bool navigation)
{
    struct hz_page_header header={0};
    header.bar=lv_obj_create(screen);lv_obj_remove_style_all(header.bar);
    lv_obj_set_pos(header.bar,0,0);lv_obj_set_size(header.bar,lv_pct(100),HZ_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(header.bar,lv_color_black(),0);
    lv_obj_set_style_bg_opa(header.bar,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(header.bar,1,0);
    lv_obj_set_style_border_color(header.bar,lv_color_hex(0x4e4e4e),0);
    lv_obj_set_style_border_side(header.bar,LV_BORDER_SIDE_BOTTOM,0);
    lv_obj_clear_flag(header.bar,LV_OBJ_FLAG_SCROLLABLE);
    if(navigation) {
        lv_obj_t *button=lv_btn_create(header.bar);lv_obj_remove_style_all(button);
        lv_obj_set_pos(button,12,6);lv_obj_set_size(button,64,50);
        lv_obj_set_style_bg_color(button,lv_color_black(),0);
        lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);
        lv_obj_set_style_bg_color(button,lv_color_hex(0x404040),LV_STATE_PRESSED);
        lv_obj_clear_flag(button,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *text=label(button,LV_SYMBOL_HOME,28,0xf4f4f4);lv_obj_center(text);
        lv_obj_add_event_cb(button,leave?leave:home,LV_EVENT_CLICKED,NULL);
    }
    header.title=label(header.bar,title,24,0xf4f4f4);
    lv_obj_set_width(header.title,600);lv_label_set_long_mode(header.title,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(header.title,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(header.title);
    header.context=label(header.bar,context,20,0xb1b1b1);
    lv_obj_set_width(header.context,176);lv_label_set_long_mode(header.context,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(header.context,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_align(header.context,LV_ALIGN_RIGHT_MID,-24,0);
    return header;
}
