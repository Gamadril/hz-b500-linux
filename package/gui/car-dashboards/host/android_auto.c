/* SPDX-License-Identifier: GPL-2.0-only */
/* Android Auto tiles (wireless and USB): like fbDOOM, the HMI blocks while the
 * receiver owns video, audio and touch. This page stays on screen while it waits. */
#include "android_auto.h"
#include "bt_music.h"
#include "navigation.h"
#include "page_header.h"
#include "system_fonts.h"
#include "ui_update.h"
#include <stdio.h>

static int (*launch)(bool, char *, size_t);
static lv_obj_t *screen, *status;

void hz_android_auto_init(int (*backend)(bool, char *, size_t)) { launch = backend; }

static void back(lv_event_t *event) { (void)event; hz_home_open(); }

void hz_android_auto_open(bool usb)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        hz_page_header_create(screen, "Android Auto", NULL, back, true);
        status = lv_label_create(screen);
        lv_obj_set_width(status, 800);
        lv_obj_set_style_text_font(status, dashboard_font(22), 0);
        lv_obj_set_style_text_color(status, lv_color_hex(0xb1b1b1), 0);
        lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(status, LV_ALIGN_CENTER, 0, 30);
    }
    if (!launch) {
        hz_label_text(status, "Android Auto runs only on the device.");
        lv_scr_load(screen);
        return;
    }
    hz_label_text(status, usb ? "Waiting for your phone...\n\n"
                  "Connect the phone to the USB socket and accept its prompts.\n"
                  "Hold the top-left corner for 2 seconds to return."
                  : "Waiting for your phone...\n\n"
                  "Start Android Auto on the phone; it connects over Bluetooth and Wi-Fi.\n"
                  "Hold the top-left corner for 2 seconds to return.");
    hz_bt_music_release(); /* AA plays through Linux audio on source sd */
    lv_scr_load(screen);
    lv_refr_now(NULL); /* shown while the HMI is blocked */
    fprintf(stderr, "UI page: android-auto (%s)\n", usb ? "usb" : "wireless");
    char message[160] = "";
    if (launch(usb, message, sizeof(message))) {
        hz_label_text(status, message);
        lv_obj_invalidate(screen);
        return;
    }
    hz_home_open();
    lv_obj_invalidate(lv_scr_act());
}
