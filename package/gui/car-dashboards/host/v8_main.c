#include "lvgl/lvgl.h"
#include "lv_drivers/sdl/sdl.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "system_fonts.h"
#include "vehicle_ui.h"
#ifdef HZ_COMBINED_UI
#include "navigation.h"
#include "settings.h"
#endif

#ifdef PREVIEW_PDC_APP
#elif defined(PREVIEW_LVGL_DASHBOARD)
#include "obj/head.h"
#include "UI/ui.h"
#else
#include "mode/Car_Launcher.h"
int preview_rk_page(const char *page);
void Car_Dashboard_Vehicle_Mode(void);
#endif

static void print_lvgl_log(const char *message) { fputs(message, stderr); }
static unsigned rendered_areas;
#ifdef HZ_COMBINED_UI
static void read_pointer(lv_indev_drv_t *driver,lv_indev_data_t *data)
{
    sdl_mouse_read(driver,data);
    lv_indev_data_t wheel={0};sdl_mousewheel_read(driver,&wheel);
    if(wheel.enc_diff)hz_settings_scroll_at(data->point.x,data->point.y,wheel.enc_diff*48);
}
#endif
static void flush_display(lv_disp_drv_t *display, const lv_area_t *area, lv_color_t *pixels)
{
    rendered_areas++;
    sdl_display_flush(display, area, pixels);
}

int main(int argc, char **argv)
{
    const char *page = "main";
    const char *vehicle_socket = getenv("VEHICLE_UBUS_SOCKET");
    int vehicle = vehicle_socket != NULL;
#ifdef PREVIEW_PDC_APP
    vehicle = 1;
#endif
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--page") && i + 1 < argc) page = argv[++i];
        else if (!strcmp(argv[i], "--vehicle")) vehicle = 1;
        else if (!strcmp(argv[i], "--demo")) {vehicle = 0;vehicle_socket = NULL;}
        else if (!strcmp(argv[i], "--ubus-socket") && i + 1 < argc) {
            vehicle_socket = argv[++i]; vehicle = 1;
        } else {
            fprintf(stderr, "Usage: %s [--page NAME] [--vehicle|--demo] [--ubus-socket PATH]\n", argv[0]);
            return 2;
        }
    }
    lv_init();
    dashboard_fonts_init();
    lv_log_register_print_cb(print_lvgl_log);
    sdl_init();
    static lv_color_t pixels[SDL_HOR_RES * SDL_VER_RES];
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, pixels, NULL, SDL_HOR_RES * SDL_VER_RES);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.draw_buf = &buffer;
    display.flush_cb = flush_display;
    display.hor_res = SDL_HOR_RES;
    display.ver_res = SDL_VER_RES;
    lv_disp_drv_register(&display);
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    static lv_indev_drv_t pointer;
    lv_indev_drv_init(&pointer);
    pointer.type = LV_INDEV_TYPE_POINTER;
#ifdef HZ_COMBINED_UI
    pointer.read_cb = read_pointer;
#else
    pointer.read_cb = sdl_mouse_read;
#endif
    lv_indev_drv_register(&pointer);
    static lv_indev_drv_t keyboard;
    lv_indev_drv_init(&keyboard);
    keyboard.type = LV_INDEV_TYPE_KEYPAD;
    keyboard.read_cb = sdl_keyboard_read;
    lv_indev_set_group(lv_indev_drv_register(&keyboard), group);
#ifdef PREVIEW_PDC_APP
    (void)page;
#elif defined(PREVIEW_LVGL_DASHBOARD)
    if (strcmp(page, "main")) return 2;
    ui_init();
    if (!vehicle) init_all_lights_test();
    if (!vehicle) lv_timer_create(update_ui_from_state, 100, NULL);
#else
#ifdef HZ_COMBINED_UI
    hz_ui_set_vehicle_mode(vehicle);
#endif
    if (vehicle) Car_Dashboard_Vehicle_Mode();
    Car_Launcher_Gui();
    if (preview_rk_page(page)) return 2;
#endif
    if (vehicle && preview_vehicle_start(vehicle_socket,
#ifdef PREVIEW_PDC_APP
            1
#else
            0
#endif
        )) return 1;
    fprintf(stderr, "SDL preview ready: %dx%d, page %s\n", SDL_HOR_RES, SDL_VER_RES, page);
#ifndef PREVIEW_LVGL_DASHBOARD
    /* The RK driver leaves the LVGL clock to the application. */
    uint32_t previous_tick = SDL_GetTicks();
    lv_tick_inc(1);
#endif
    uint32_t started = lv_tick_get();
    int reported = 0;
    for (;;) {
#ifdef PREVIEW_LVGL_DASHBOARD
        if (!vehicle) handle_console_input();
#else
        uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - previous_tick);
        previous_tick = now;
#endif
        preview_vehicle_poll();
#ifdef HZ_COMBINED_UI
        hz_ui_poll();
#endif
        lv_timer_handler();
        if (!reported && lv_tick_elaps(started) >= 1000) {
            fprintf(stderr, "SDL clock running; rendered %u areas\n", rendered_areas);
            reported = 1;
        }
        SDL_Delay(5);
    }
}
