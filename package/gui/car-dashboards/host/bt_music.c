/* SPDX-License-Identifier: GPL-2.0-only */
/* Phone music through the AB5301B (A2DP decoded by the module itself): source
 * bt-music, AVRCP-style controls and the phone's MI/SA reports via ab5301b. */
#include "bt_music.h"
#include "navigation.h"
#include "page_header.h"
#include "system_fonts.h"
#include "transition.h"
#include "ui_update.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct state { bool known, connections, playing; int length; char phone[49], title[97], artist[97], album[97]; };
static int (*request)(const char *, char *, size_t);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static struct state latest;
static char queue[4][24];
static unsigned queued;
static bool page_open, source_bt;
static lv_obj_t *screen, *phone_label, *title_label, *artist_label, *album_label, *length_label, *play_icon;

static void field(const char *reply, const char *key, char *out, size_t size)
{
    char name[16]; snprintf(name, sizeof(name), "%s=", key);
    const char *start = strstr(reply, name); *out = 0;
    if (!start) return;
    start += strlen(name);
    size_t length = strcspn(start, "\t\n");
    if (length >= size) length = size - 1;
    memcpy(out, start, length); out[length] = 0;
}
static void parse(const char *reply, struct state *s)
{
    char text[16];
    field(reply, "state", text, sizeof(text)); s->playing = !strcmp(text, "playing");
    field(reply, "connections", text, sizeof(text)); s->connections = !strcmp(text, "on");
    field(reply, "length", text, sizeof(text)); s->length = atoi(text);
    field(reply, "phone", s->phone, sizeof(s->phone));
    field(reply, "title", s->title, sizeof(s->title));
    field(reply, "artist", s->artist, sizeof(s->artist));
    field(reply, "album", s->album, sizeof(s->album));
    s->known = true;
}
/* All socket I/O stays here: a busy UART exchange must never stall LVGL. */
static void *worker(void *arg)
{
    (void)arg;
    for (;;) {
        char line[24] = "", reply[512];
        pthread_mutex_lock(&lock);
        if (!queued && !page_open) pthread_cond_wait(&wake, &lock);
        if (queued) { snprintf(line, sizeof(line), "%s", queue[0]); memmove(queue, queue + 1, sizeof(queue[0]) * --queued); }
        pthread_mutex_unlock(&lock);
        if (*line) { request(line, reply, sizeof(reply)); continue; }
        struct state s = {0};
        if (!request("bt\n", reply, sizeof(reply))) parse(reply, &s);
        pthread_mutex_lock(&lock); latest = s; pthread_mutex_unlock(&lock);
        usleep(500000); /* MI arrives about once per second */
    }
    return NULL;
}
static void enqueue(const char *line)
{
    if (!request) return;
    pthread_mutex_lock(&lock);
    if (queued < sizeof(queue) / sizeof(queue[0])) snprintf(queue[queued++], sizeof(queue[0]), "%s", line);
    pthread_cond_signal(&wake); pthread_mutex_unlock(&lock);
}
void hz_bt_music_init(int (*backend)(const char *, char *, size_t))
{
    pthread_t thread;
    request = backend;
    if (pthread_create(&thread, NULL, worker, NULL)) request = NULL; else pthread_detach(thread);
}
void hz_bt_music_release(void)
{
    if (!source_bt) return;
    source_bt = false; enqueue("bt pause\n"); enqueue("source sd\n");
}
static void refresh(lv_timer_t *timer)
{
    (void)timer;
    struct state s; pthread_mutex_lock(&lock); s = latest; pthread_mutex_unlock(&lock);
    char text[64];
    if (!request) hz_label_text(phone_label, "Companion service not available");
    else if (!s.known) hz_label_text(phone_label, "Waiting for the audio module...");
    else if (*s.phone) { snprintf(text, sizeof(text), LV_SYMBOL_BLUETOOTH " %s", s.phone); hz_label_text(phone_label, text); }
    /* Opening the page sent "bt connect" (AT#CC): the module calls the last
     * phone itself, independent of Settings > Allow connections. */
    else hz_label_text(phone_label, "Connecting to the last phone...");
    hz_label_text(title_label, *s.title ? s.title : "-");
    hz_label_text(artist_label, s.artist);
    hz_label_text(album_label, s.album);
    if (s.length > 0) snprintf(text, sizeof(text), "%02d:%02d", s.length / 60, s.length % 60); else *text = 0;
    hz_label_text(length_label, text);
    hz_label_text(play_icon, s.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}
static void back(lv_event_t *event)
{
    (void)event;
    /* Phone audio keeps playing; local Music/Video take the speakers back. */
    pthread_mutex_lock(&lock); page_open = false; pthread_mutex_unlock(&lock);
    hz_home_open();
}
static void control(lv_event_t *event)
{
    const char *action = lv_event_get_user_data(event);
    if (!strcmp(action, "toggle")) {
        bool playing; pthread_mutex_lock(&lock); playing = latest.playing; pthread_mutex_unlock(&lock);
        enqueue(playing ? "bt pause\n" : "bt play\n");
    } else if (!strcmp(action, "next")) enqueue("bt next\n");
    else enqueue("bt prev\n");
}
static lv_obj_t *label(lv_obj_t *parent, int size, uint32_t color, int y)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_style_text_font(object, dashboard_font(size), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_DOT);
    lv_obj_set_width(object, parent == screen ? 600 : 240);
    lv_obj_align(object, LV_ALIGN_CENTER, 0, y);
    hz_label_text(object, "");
    return object;
}
/* Same look as the local Music page: round card, outlined skip buttons, white play button. */
static lv_obj_t *button(lv_obj_t *parent, int size, bool primary, const char *symbol, const char *action)
{
    lv_obj_t *object = lv_btn_create(parent);
    lv_obj_set_size(object, size, size);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_bg_color(object, primary ? lv_color_white() : lv_color_black(), 0);
    if (!primary) {
        lv_obj_set_style_border_width(object, 2, 0);
        lv_obj_set_style_border_color(object, lv_color_white(), 0);
    }
    lv_obj_t *icon = lv_label_create(object);
    hz_label_text(icon, symbol);
    lv_obj_set_style_text_font(icon, dashboard_font(primary ? 32 : 22), 0);
    lv_obj_set_style_text_color(icon, primary ? lv_color_black() : lv_color_white(), 0);
    lv_obj_center(icon);
    lv_obj_add_event_cb(object, control, LV_EVENT_CLICKED, (void *)action);
    return icon;
}
void hz_bt_music_open(void)
{
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        hz_page_header_create(screen, "BT Music", NULL, back, true);
        lv_obj_t *card = lv_obj_create(screen);
        lv_obj_set_size(card, 280, 280);
        lv_obj_align(card, LV_ALIGN_CENTER, 0, -60);
        lv_obj_set_style_bg_color(card, lv_color_black(), 0);
        lv_obj_set_style_border_width(card, 3, 0);
        lv_obj_set_style_border_color(card, lv_color_white(), 0);
        lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
        lv_obj_set_style_radius(card, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *note = lv_label_create(card);
        hz_label_text(note, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_font(note, dashboard_font(32), 0);
        lv_obj_set_style_text_color(note, lv_color_white(), 0);
        lv_obj_align(note, LV_ALIGN_CENTER, 0, -60);
        title_label = label(card, 22, 0xffffff, 0);
        artist_label = label(card, 17, 0x727272, 34);
        album_label = label(card, 17, 0x727272, 60);
        phone_label = label(screen, 17, 0xa1a1a1, 105);
        length_label = label(screen, 14, 0x727272, 135);
        lv_obj_t *controls = lv_obj_create(screen);
        lv_obj_remove_style_all(controls);
        lv_obj_set_size(controls, 400, 100);
        lv_obj_align(controls, LV_ALIGN_CENTER, 0, 200);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        button(controls, 70, false, LV_SYMBOL_PREV, "prev");
        play_icon = button(controls, 90, true, LV_SYMBOL_PLAY, "toggle");
        button(controls, 70, false, LV_SYMBOL_NEXT, "next");
        lv_timer_create(refresh, 250, NULL);
        refresh(NULL);
    }
    enqueue("source bt-music\n");
    enqueue("bt connect\n"); /* reconnect the last phone; ignored while connected */
    pthread_mutex_lock(&lock); page_open = true; source_bt = true; pthread_cond_signal(&wake); pthread_mutex_unlock(&lock);
    hz_screen_load_anim(screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    fprintf(stderr, "UI page: bt-music\n");
}
