#include "ui_update.h"
#include "Car_Dashboard.h"
#include "system_fonts.h"
#include "rk_offline.h"
#include <stdbool.h>
#include <stdio.h>

static bool vehicle_mode;
void Car_Dashboard_Vehicle_Mode(void) { vehicle_mode = true; }
static int current_speed = 0;
static lv_obj_t * speed_meter;
static lv_obj_t * rpm_meter;
static lv_meter_indicator_t * speed_indic;
static lv_meter_indicator_t * rpm_indic;
static lv_meter_scale_t * speed_scale;
static lv_meter_scale_t * rpm_scale;
static lv_obj_t * speed_label;
static lv_obj_t * rpm_label;
static lv_obj_t * left_led;
static lv_obj_t * right_led;
static bool left_turn_state = false;
static bool right_turn_state = false;
static lv_obj_t * center_speed_label;
static lv_obj_t * gear_label;
static lv_obj_t * fuel_bar;

static void create_speed_meter(lv_obj_t * parent)
{
    speed_meter = lv_meter_create(parent);
    lv_obj_set_size(speed_meter, 280, 280);
    lv_obj_set_pos(speed_meter, 40, 80);

    lv_obj_set_style_bg_color(speed_meter, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(speed_meter, LV_OPA_90, 0);
    lv_obj_set_style_border_width(speed_meter, 3, 0);
    lv_obj_set_style_border_color(speed_meter, lv_color_hex(0x00D9FF), 0);
    lv_obj_set_style_border_opa(speed_meter, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(speed_meter, 60, 0);
    lv_obj_set_style_shadow_color(speed_meter, lv_color_hex(0x00D9FF), 0);
    lv_obj_set_style_shadow_opa(speed_meter, LV_OPA_50, 0);

    speed_scale = lv_meter_add_scale(speed_meter);
    lv_meter_set_scale_ticks(speed_meter, speed_scale, 45, 2, 8, lv_color_hex(0x334155));
    lv_meter_set_scale_major_ticks(speed_meter, speed_scale, 5, 3, 12, lv_color_hex(0x94A3B8), 12);
    lv_meter_set_scale_range(speed_meter, speed_scale, 0, 220, 270, 135);

    lv_meter_indicator_t * indic_cyan = lv_meter_add_arc(speed_meter, speed_scale, 10, lv_color_hex(0x00D9FF), 0);
    lv_meter_set_indicator_start_value(speed_meter, indic_cyan, 0);
    lv_meter_set_indicator_end_value(speed_meter, indic_cyan, 140);

    lv_meter_indicator_t * indic_yellow = lv_meter_add_arc(speed_meter, speed_scale, 10, lv_color_hex(0xFACC15), 0);
    lv_meter_set_indicator_start_value(speed_meter, indic_yellow, 140);
    lv_meter_set_indicator_end_value(speed_meter, indic_yellow, 180);

    lv_meter_indicator_t * indic_red = lv_meter_add_arc(speed_meter, speed_scale, 10, lv_color_hex(0xEF4444), 0);
    lv_meter_set_indicator_start_value(speed_meter, indic_red, 180);
    lv_meter_set_indicator_end_value(speed_meter, indic_red, 220);

    speed_indic = lv_meter_add_needle_line(speed_meter, speed_scale, 4, lv_color_hex(0xFFFFFF), -10);
    lv_meter_set_indicator_value(speed_meter, speed_indic, 0);

    speed_label = lv_label_create(speed_meter);
    hz_label_text_fmt(speed_label, "%d", current_speed);
    lv_obj_set_style_text_font(speed_label, dashboard_font(32), 0);
    lv_obj_set_style_text_color(speed_label, lv_color_hex(0x00D9FF), 0);
    lv_obj_align(speed_label, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t * label_title = lv_label_create(speed_meter);
    hz_label_text(label_title, "km/h");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(label_title, dashboard_font(14), 0);
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 20);
}

static void create_rpm_meter(lv_obj_t * parent)
{
    rpm_meter = lv_meter_create(parent);
    lv_obj_set_size(rpm_meter, 280, 280);
    lv_obj_set_pos(rpm_meter, 704, 80);

    lv_obj_set_style_bg_color(rpm_meter, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rpm_meter, LV_OPA_90, 0);
    lv_obj_set_style_border_width(rpm_meter, 3, 0);
    lv_obj_set_style_border_color(rpm_meter, lv_color_hex(0xF97316), 0);
    lv_obj_set_style_border_opa(rpm_meter, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(rpm_meter, 60, 0);
    lv_obj_set_style_shadow_color(rpm_meter, lv_color_hex(0xF97316), 0);
    lv_obj_set_style_shadow_opa(rpm_meter, LV_OPA_50, 0);

    rpm_scale = lv_meter_add_scale(rpm_meter);
    lv_meter_set_scale_ticks(rpm_meter, rpm_scale, 41, 2, 8, lv_color_hex(0x334155));
    lv_meter_set_scale_major_ticks(rpm_meter, rpm_scale, 5, 3, 12, lv_color_hex(0x94A3B8), 12);
    lv_meter_set_scale_range(rpm_meter, rpm_scale, 0, 80, 270, 135);

    lv_meter_indicator_t * indic_orange = lv_meter_add_arc(rpm_meter, rpm_scale, 10, lv_color_hex(0xF97316), 0);
    lv_meter_set_indicator_start_value(rpm_meter, indic_orange, 0);
    lv_meter_set_indicator_end_value(rpm_meter, indic_orange, 50);

    lv_meter_indicator_t * indic_yellow = lv_meter_add_arc(rpm_meter, rpm_scale, 10, lv_color_hex(0xFACC15), 0);
    lv_meter_set_indicator_start_value(rpm_meter, indic_yellow, 50);
    lv_meter_set_indicator_end_value(rpm_meter, indic_yellow, 65);

    lv_meter_indicator_t * indic_red = lv_meter_add_arc(rpm_meter, rpm_scale, 10, lv_color_hex(0xEF4444), 0);
    lv_meter_set_indicator_start_value(rpm_meter, indic_red, 65);
    lv_meter_set_indicator_end_value(rpm_meter, indic_red, 80);

    rpm_indic = lv_meter_add_needle_line(rpm_meter, rpm_scale, 4, lv_color_hex(0xFFFFFF), -10);
    lv_meter_set_indicator_value(rpm_meter, rpm_indic, 0);

    rpm_label = lv_label_create(rpm_meter);
    hz_label_text(rpm_label, "0.0");
    lv_obj_set_style_text_font(rpm_label, dashboard_font(32), 0);
    lv_obj_set_style_text_color(rpm_label, lv_color_hex(0xF97316), 0);
    lv_obj_align(rpm_label, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t * label_title = lv_label_create(rpm_meter);
    hz_label_text(label_title, "x1000 RPM");
    lv_obj_set_style_text_color(label_title, lv_color_hex(0x64748B), 0);
    lv_obj_set_style_text_font(label_title, dashboard_font(14), 0);
    lv_obj_align(label_title, LV_ALIGN_CENTER, 0, 20);
}

static void create_car_icon(lv_obj_t * parent)
{
    lv_obj_t * car = lv_obj_create(parent);
    lv_obj_set_size(car, 340, 320);
    lv_obj_set_pos(car, 342, 60);

    lv_obj_set_style_bg_color(car, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(car, LV_OPA_80, 0);
    lv_obj_set_style_border_width(car, 2, 0);
    lv_obj_set_style_border_color(car, lv_color_hex(0x8B5CF6), 0);
    lv_obj_set_style_border_opa(car, LV_OPA_40, 0);
    lv_obj_set_style_radius(car, 20, 0);
    lv_obj_set_style_shadow_width(car, 50, 0);
    lv_obj_set_style_shadow_color(car, lv_color_hex(0x8B5CF6), 0);
    lv_obj_set_style_shadow_opa(car, LV_OPA_30, 0);
    lv_obj_clear_flag(car, LV_OBJ_FLAG_SCROLLABLE);

    center_speed_label = lv_label_create(car);
    hz_label_text(center_speed_label, "0");
    lv_obj_set_style_text_font(center_speed_label, dashboard_font(32), 0);
    lv_obj_set_style_text_color(center_speed_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(center_speed_label, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t * unit_label = lv_label_create(car);
    hz_label_text(unit_label, "km/h");
    lv_obj_set_style_text_font(unit_label, dashboard_font(14), 0);
    lv_obj_set_style_text_color(unit_label, lv_color_hex(0x64748B), 0);
    lv_obj_align(unit_label, LV_ALIGN_CENTER, 0, -25);

    lv_obj_t * sep = lv_obj_create(car);
    lv_obj_set_size(sep, 280, 2);
    lv_obj_align(sep, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x8B5CF6), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_30, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    gear_label = lv_label_create(car);
    hz_label_text(gear_label, vehicle_mode ? "--" : "D");
    lv_obj_set_style_text_font(gear_label, dashboard_font(32), 0);
    lv_obj_set_style_text_color(gear_label, lv_color_hex(0x22C55E), 0);
    lv_obj_align(gear_label, LV_ALIGN_CENTER, 0, 40);

    lv_obj_t * fuel_container = lv_obj_create(car);
    lv_obj_set_size(fuel_container, 280, 50);
    lv_obj_align(fuel_container, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_opa(fuel_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fuel_container, 0, 0);
    lv_obj_clear_flag(fuel_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * fuel_icon = lv_label_create(fuel_container);
    hz_label_text(fuel_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(fuel_icon, lv_color_hex(0x22C55E), 0);
    lv_obj_align(fuel_icon, LV_ALIGN_LEFT_MID, 0, 0);

    fuel_bar = lv_bar_create(fuel_container);
    lv_obj_set_size(fuel_bar, 200, 12);
    lv_obj_align(fuel_bar, LV_ALIGN_LEFT_MID, 30, 0);
    lv_bar_set_range(fuel_bar, 0, 100);
    lv_bar_set_value(fuel_bar, vehicle_mode ? 0 : 75, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(fuel_bar, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_bg_opa(fuel_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fuel_bar, 6, 0);
    lv_obj_set_style_bg_color(fuel_bar, lv_color_hex(0x22C55E), LV_PART_INDICATOR);
    lv_obj_set_style_radius(fuel_bar, 6, LV_PART_INDICATOR);

    lv_obj_t * fuel_pct = lv_label_create(fuel_container);
    hz_label_text(fuel_pct, vehicle_mode ? "--" : "75%");
    lv_obj_set_style_text_font(fuel_pct, dashboard_font(14), 0);
    lv_obj_set_style_text_color(fuel_pct, lv_color_hex(0x64748B), 0);
    lv_obj_align(fuel_pct, LV_ALIGN_RIGHT_MID, 0, 0);
}

static lv_obj_t * left_turn_container;
static lv_obj_t * right_turn_container;
static lv_obj_t * left_arrow_label;
static lv_obj_t * right_arrow_label;

static void create_turn_leds(lv_obj_t * parent)
{
    left_turn_container = lv_obj_create(parent);
    lv_obj_set_size(left_turn_container, 70, 70);
    lv_obj_set_pos(left_turn_container, 320, 185);
    lv_obj_set_style_bg_color(left_turn_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(left_turn_container, LV_OPA_90, 0);
    lv_obj_set_style_border_width(left_turn_container, 2, 0);
    lv_obj_set_style_border_color(left_turn_container, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(left_turn_container, 15, 0);
    lv_obj_set_style_shadow_width(left_turn_container, 0, 0);
    lv_obj_clear_flag(left_turn_container, LV_OBJ_FLAG_SCROLLABLE);

    left_arrow_label = lv_label_create(left_turn_container);
    hz_label_text(left_arrow_label, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(left_arrow_label, dashboard_font(32), 0);
    lv_obj_set_style_text_color(left_arrow_label, lv_color_hex(0x475569), 0);
    lv_obj_center(left_arrow_label);

    left_led = lv_led_create(parent);
    lv_obj_set_size(left_led, 1, 1);
    lv_obj_add_flag(left_led, LV_OBJ_FLAG_HIDDEN);
    lv_led_off(left_led);

    right_turn_container = lv_obj_create(parent);
    lv_obj_set_size(right_turn_container, 70, 70);
    lv_obj_set_pos(right_turn_container, 634, 185);
    lv_obj_set_style_bg_color(right_turn_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(right_turn_container, LV_OPA_90, 0);
    lv_obj_set_style_border_width(right_turn_container, 2, 0);
    lv_obj_set_style_border_color(right_turn_container, lv_color_hex(0x334155), 0);
    lv_obj_set_style_radius(right_turn_container, 15, 0);
    lv_obj_set_style_shadow_width(right_turn_container, 0, 0);
    lv_obj_clear_flag(right_turn_container, LV_OBJ_FLAG_SCROLLABLE);

    right_arrow_label = lv_label_create(right_turn_container);
    hz_label_text(right_arrow_label, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(right_arrow_label, dashboard_font(32), 0);
    lv_obj_set_style_text_color(right_arrow_label, lv_color_hex(0x475569), 0);
    lv_obj_center(right_arrow_label);

    right_led = lv_led_create(parent);
    lv_obj_set_size(right_led, 1, 1);
    lv_obj_add_flag(right_led, LV_OBJ_FLAG_HIDDEN);
    lv_led_off(right_led);
}

static void update_speed_display(void)
{
    if(current_speed < 0) current_speed = 0;
    if(current_speed > 220) current_speed = 220;

    lv_meter_set_indicator_value(speed_meter, speed_indic, (int32_t)current_speed);

    char speed_buf[32];
    snprintf(speed_buf, sizeof(speed_buf), "%d", current_speed);
    hz_label_text(speed_label, speed_buf);

    hz_label_text(center_speed_label, speed_buf);

    float rpm_value = ((float)current_speed / 220.0f) * 8.0f;
    if(rpm_value > 8.0f) rpm_value = 8.0f;

    lv_meter_set_indicator_value(rpm_meter, rpm_indic, (int32_t)(rpm_value * 10));

    char rpm_buf[32];
    snprintf(rpm_buf, sizeof(rpm_buf), "%.1f", rpm_value);
    hz_label_text(rpm_label, rpm_buf);
}

static void speed_up_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        if(current_speed < 220) {
            int increment = (code == LV_EVENT_LONG_PRESSED_REPEAT) ? 5 : 10;
            current_speed += increment;
            if(current_speed > 220) current_speed = 220;
            update_speed_display();
        }
    }
}

static void speed_down_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        if(current_speed > 0) {
            int decrement = (code == LV_EVENT_LONG_PRESSED_REPEAT) ? 5 : 10;
            current_speed -= decrement;
            if(current_speed < 0) current_speed = 0;
            update_speed_display();
        }
    }
}

static void left_turn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    left_turn_state = !left_turn_state;

    if(left_turn_state) {
        lv_obj_set_style_border_color(left_turn_container, lv_color_hex(0xFACC15), 0);
        lv_obj_set_style_shadow_width(left_turn_container, 30, 0);
        lv_obj_set_style_shadow_color(left_turn_container, lv_color_hex(0xFACC15), 0);
        lv_obj_set_style_shadow_opa(left_turn_container, LV_OPA_70, 0);
        lv_obj_set_style_text_color(left_arrow_label, lv_color_hex(0xFACC15), 0);
        lv_led_on(left_led);
        led_ctrl(0, 1);
    } else {
        lv_obj_set_style_border_color(left_turn_container, lv_color_hex(0x334155), 0);
        lv_obj_set_style_shadow_width(left_turn_container, 0, 0);
        lv_obj_set_style_text_color(left_arrow_label, lv_color_hex(0x475569), 0);
        lv_led_off(left_led);
        led_ctrl(0, 0);
    }
}

static void right_turn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    right_turn_state = !right_turn_state;

    if(right_turn_state) {
        lv_obj_set_style_border_color(right_turn_container, lv_color_hex(0xFACC15), 0);
        lv_obj_set_style_shadow_width(right_turn_container, 30, 0);
        lv_obj_set_style_shadow_color(right_turn_container, lv_color_hex(0xFACC15), 0);
        lv_obj_set_style_shadow_opa(right_turn_container, LV_OPA_70, 0);
        lv_obj_set_style_text_color(right_arrow_label, lv_color_hex(0xFACC15), 0);
        lv_led_on(right_led);
        led_ctrl(1, 1);
    } else {
        lv_obj_set_style_border_color(right_turn_container, lv_color_hex(0x334155), 0);
        lv_obj_set_style_shadow_width(right_turn_container, 0, 0);
        lv_obj_set_style_text_color(right_arrow_label, lv_color_hex(0x475569), 0);
        lv_led_off(right_led);
        led_ctrl(1, 0);
    }
}

static void create_control_buttons(lv_obj_t * parent)
{
    if (vehicle_mode) return;
    int btn_width = 130;
    int btn_height = 50;
    int btn_y = 420;
    int btn_radius = 25;
    int btn_gap = 15;

    int total_width = btn_width * 4 + btn_gap * 3;
    int start_x = (1024 - total_width) / 2;

    lv_obj_t * btn_speed_up = lv_btn_create(parent);
    lv_obj_set_size(btn_speed_up, btn_width, btn_height);
    lv_obj_set_pos(btn_speed_up, start_x, btn_y);
    lv_obj_set_style_bg_color(btn_speed_up, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn_speed_up, 2, 0);
    lv_obj_set_style_border_color(btn_speed_up, lv_color_hex(0x00D9FF), 0);
    lv_obj_set_style_radius(btn_speed_up, btn_radius, 0);
    lv_obj_set_style_shadow_width(btn_speed_up, 15, 0);
    lv_obj_set_style_shadow_color(btn_speed_up, lv_color_hex(0x00D9FF), 0);
    lv_obj_set_style_shadow_opa(btn_speed_up, LV_OPA_40, 0);

    lv_obj_t * label_up = lv_label_create(btn_speed_up);
    hz_label_text(label_up, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(label_up, dashboard_font(22), 0);
    lv_obj_set_style_text_color(label_up, lv_color_hex(0x00D9FF), 0);
    lv_obj_center(label_up);

    lv_obj_add_event_cb(btn_speed_up, speed_up_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_speed_up, speed_up_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    lv_obj_t * btn_speed_down = lv_btn_create(parent);
    lv_obj_set_size(btn_speed_down, btn_width, btn_height);
    lv_obj_set_pos(btn_speed_down, start_x + btn_width + btn_gap, btn_y);
    lv_obj_set_style_bg_color(btn_speed_down, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn_speed_down, 2, 0);
    lv_obj_set_style_border_color(btn_speed_down, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_radius(btn_speed_down, btn_radius, 0);
    lv_obj_set_style_shadow_width(btn_speed_down, 15, 0);
    lv_obj_set_style_shadow_color(btn_speed_down, lv_color_hex(0xEF4444), 0);
    lv_obj_set_style_shadow_opa(btn_speed_down, LV_OPA_40, 0);

    lv_obj_t * label_down = lv_label_create(btn_speed_down);
    hz_label_text(label_down, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(label_down, dashboard_font(22), 0);
    lv_obj_set_style_text_color(label_down, lv_color_hex(0xEF4444), 0);
    lv_obj_center(label_down);

    lv_obj_add_event_cb(btn_speed_down, speed_down_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_speed_down, speed_down_event_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    lv_obj_t * btn_left_turn = lv_btn_create(parent);
    lv_obj_set_size(btn_left_turn, btn_width, btn_height);
    lv_obj_set_pos(btn_left_turn, start_x + (btn_width + btn_gap) * 2, btn_y);
    lv_obj_set_style_bg_color(btn_left_turn, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn_left_turn, 2, 0);
    lv_obj_set_style_border_color(btn_left_turn, lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_radius(btn_left_turn, btn_radius, 0);
    lv_obj_set_style_shadow_width(btn_left_turn, 15, 0);
    lv_obj_set_style_shadow_color(btn_left_turn, lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_shadow_opa(btn_left_turn, LV_OPA_40, 0);

    lv_obj_t * label_left = lv_label_create(btn_left_turn);
    hz_label_text(label_left, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(label_left, dashboard_font(22), 0);
    lv_obj_set_style_text_color(label_left, lv_color_hex(0xFACC15), 0);
    lv_obj_center(label_left);

    lv_obj_add_event_cb(btn_left_turn, left_turn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_right_turn = lv_btn_create(parent);
    lv_obj_set_size(btn_right_turn, btn_width, btn_height);
    lv_obj_set_pos(btn_right_turn, start_x + (btn_width + btn_gap) * 3, btn_y);
    lv_obj_set_style_bg_color(btn_right_turn, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn_right_turn, 2, 0);
    lv_obj_set_style_border_color(btn_right_turn, lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_radius(btn_right_turn, btn_radius, 0);
    lv_obj_set_style_shadow_width(btn_right_turn, 15, 0);
    lv_obj_set_style_shadow_color(btn_right_turn, lv_color_hex(0xFACC15), 0);
    lv_obj_set_style_shadow_opa(btn_right_turn, LV_OPA_40, 0);

    lv_obj_t * label_right = lv_label_create(btn_right_turn);
    hz_label_text(label_right, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(label_right, dashboard_font(22), 0);
    lv_obj_set_style_text_color(label_right, lv_color_hex(0xFACC15), 0);
    lv_obj_center(label_right);

    lv_obj_add_event_cb(btn_right_turn, right_turn_event_cb, LV_EVENT_CLICKED, NULL);
}

void Car_Dashboard_Gui(lv_style_t * style)
{
    led_init();

    lv_obj_t * scr = lv_scr_act();

    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    create_speed_meter(scr);
    create_rpm_meter(scr);
    create_car_icon(scr);
    create_turn_leds(scr);
    create_control_buttons(scr);
}

void Car_Dashboard_Build(lv_obj_t * parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    create_speed_meter(parent);
    create_rpm_meter(parent);
    create_car_icon(parent);
    create_turn_leds(parent);
    create_control_buttons(parent);
}

/* Vehicle mode: called only from the LVGL thread, with -1 for unknown values. */
void Car_Dashboard_Vehicle(int speed, int rpm, int left, int right)
{
    if (!speed_meter || !lv_obj_is_valid(speed_meter)) return;
    if (speed > 220) speed = 220;
    if (rpm > 8000) rpm = 8000;
    int speed_value=speed<0?0:speed,rpm_value=rpm<0?0:rpm/100;
    if(speed_indic->end_value!=speed_value)lv_meter_set_indicator_value(speed_meter,speed_indic,speed_value);
    if(rpm_indic->end_value!=rpm_value)lv_meter_set_indicator_value(rpm_meter,rpm_indic,rpm_value);
    if (speed < 0) {
        hz_label_text(speed_label, "--"); hz_label_text(center_speed_label, "--");
    } else {
        hz_label_text_fmt(speed_label, "%d", speed);
        hz_label_text_fmt(center_speed_label, "%d", speed);
    }
    if (rpm < 0) hz_label_text(rpm_label, "--");
    else hz_label_text_fmt(rpm_label, "%d.%d", rpm / 1000, (rpm % 1000) / 100);
    hz_style_opa(left_turn_container,left?LV_OPA_COVER:LV_OPA_20);
    hz_style_opa(right_turn_container,right?LV_OPA_COVER:LV_OPA_20);
}
