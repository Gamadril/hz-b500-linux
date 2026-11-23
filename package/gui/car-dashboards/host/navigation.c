/* SPDX-License-Identifier: GPL-2.0-only */
#include "navigation.h"
#include "page_header.h"
#include "ui_update.h"
#include "transition.h"
#include "page_art.h"
#include "lvgl/lvgl.h"
#include "mode/Car_Launcher.h"
#include "UI/ui.h"
#include "obj/head.h"
#include "system_fonts.h"
#include <stdio.h>

static int vehicle_mode=1;

void hz_ui_set_vehicle_mode(int enabled) {vehicle_mode=!!enabled;}
int hz_ui_vehicle_mode(void) {return vehicle_mode;}
void hz_ui_poll(void)
{
    if(!vehicle_mode && ui_Screen1)handle_console_input();
}

/* Keep the original widget and artwork dimensions. Screens have the native
 * display size; an 800x480 container centres the dashboard without resampling. */
static void fit_screen(lv_obj_t *screen)
{
    uint32_t count = lv_obj_get_child_cnt(screen);
    lv_obj_t *canvas = lv_obj_create(screen);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_size(canvas, 800, 480);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_set_parent(lv_obj_get_child(screen,0),canvas);
    }
    lv_obj_center(canvas);
    lv_obj_update_layout(screen);
}

static void back(lv_event_t *event)
{
    (void)event;
    hz_home_open();
}

void hz_home_open(void)
{
    lv_obj_t *home=Car_Launcher_Get_Screen();
    if(!hz_page_art_ready(home)) { /* Released for video playback. */
        lv_obj_t *live[16];unsigned count=hz_launcher_live("home",live,16);hz_page_art_bind(home,"home",live,count);
    }
    hz_screen_load_anim(home,LV_SCR_LOAD_ANIM_MOVE_RIGHT,300,0,false);
    fprintf(stderr, "UI page: hmi\n");
}

void hz_dashboard_open(void)
{
    uint32_t started=lv_tick_get();bool first=ui_Screen1==NULL;
    if (!ui_Screen1) {
        lv_obj_t *home = Car_Launcher_Get_Screen();
        ui_init();
        fit_screen(ui_Screen1);
        hz_page_header_create(ui_Screen1,"Dashboard",NULL,back,true);
        if(vehicle_mode) {
            /* Unknown real readings must not become synthetic gauge values. */
            lv_obj_t *controls[] = {ui_HBChange, ui_LBChange,
                ui_EngineChange, ui_BeltChange, ui_LowEgOilChange,
                ui_Engine, ui_seat_belt, ui_Low_engine_oil};
            for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
                lv_obj_clear_flag(controls[i], LV_OBJ_FLAG_CLICKABLE);
        } else {
            init_all_lights_test();
            lv_timer_create(update_ui_from_state,100,NULL);
        }
        lv_scr_load(home);
    }
    hz_page_profile("dashboard",started,first);
    hz_screen_load_anim(ui_Screen1,LV_SCR_LOAD_ANIM_MOVE_LEFT,300,0,false);
    fprintf(stderr, "UI page: dashboard (native 800x480 centred; %s)\n",vehicle_mode?"vehicle data":"manual demo");
}
