#include "transition.h"
#include "page_art.h"
#include "ui_update.h"
#include "Car_Launcher.h"
#include "Car_Dashboard.h"
#include "rk_offline.h"
#include "navigation.h"
#include "vehicle_ui.h"
#include "music_player.h"
#include "bt_music.h"
#include "android_auto.h"
#include "video_ui.h"
#include "settings.h"
#include "gear_calibration.h"
#include "page_header.h"
#include "vehicle_body.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

static lv_obj_t * launcher_scr = NULL;
static lv_obj_t * dashboard_scr = NULL;
static lv_timer_t * clock_timer = NULL;

static lv_obj_t * status_time_label = NULL;
static lv_obj_t * car_panel = NULL;

static lv_obj_t * music_scr = NULL;
static lv_obj_t * music_title_label = NULL;
static lv_obj_t * music_artist_label = NULL;
static lv_obj_t * music_progress_slider = NULL;
static lv_obj_t * music_time_current = NULL;
static lv_obj_t * music_time_total = NULL;
static lv_obj_t * music_play_btn = NULL;
static lv_obj_t * music_play_icon = NULL;
static lv_obj_t * music_lyrics_label = NULL;
static lv_timer_t * music_progress_timer = NULL;
static int music_current_index = 0;
static bool music_playing = false;
static uint32_t music_elapsed_sec = 0;
static bool music_slider_dragging = false;
#define MUSIC_MAX_TRACKS 1

static const int music_durations[] = {HZ_MUSIC_DURATION};

static char music_names[MUSIC_MAX_TRACKS][64];
static char music_artists[MUSIC_MAX_TRACKS][64];
static char music_lyrics_text[MUSIC_MAX_TRACKS][128];

#include <iconv.h>
/* Try compatible GBK aliases before retaining the original ID3 metadata. */
static int gbk_to_utf8(const char * gbk, char * utf8, size_t utf8_len)
{
    const char * encodings[] = {"GB18030", "GBK", "GB2312", NULL};
    iconv_t cd = (iconv_t)-1;

    for(int i = 0; encodings[i] != NULL; i++) {
        cd = iconv_open("UTF-8", encodings[i]);
        if(cd != (iconv_t)-1) break;
    }
    if(cd == (iconv_t)-1) return -1;

    char * in = (char *)gbk;
    char * out = utf8;
    size_t in_len = strlen(gbk);
    size_t out_len = utf8_len - 1;

    if(iconv(cd, &in, &in_len, &out, &out_len) == (size_t)-1) {
        iconv_close(cd);
        return -1;
    }
    *out = '\0';
    iconv_close(cd);
    return 0;
}

static void read_id3_tag(const char * filepath, char * title, char * artist, size_t max_len)
{
    snprintf(title, max_len, "Unknown");
    snprintf(artist, max_len, "Unknown");

    FILE * fp = fopen(filepath, "rb");
    if(!fp) return;

    fseek(fp, -128, SEEK_END);
    char tag[128];
    if(fread(tag, 1, 128, fp) != 128) {
        fclose(fp);
        return;
    }
    fclose(fp);

    if(tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G') {
        char raw_title[31] = {0};
        char raw_artist[31] = {0};

        // Title: 3-32 (30 bytes)
        strncpy(raw_title, &tag[3], 30);
        for(int i = 29; i >= 0 && (raw_title[i] == ' ' || raw_title[i] == '\0'); i--) raw_title[i] = '\0';

        // Artist: 33-62 (30 bytes)
        strncpy(raw_artist, &tag[33], 30);
        for(int i = 29; i >= 0 && (raw_artist[i] == ' ' || raw_artist[i] == '\0'); i--) raw_artist[i] = '\0';

        if(strlen(raw_title) > 0) {
            if(gbk_to_utf8(raw_title, title, max_len) != 0) {
                strncpy(title, raw_title, max_len - 1);
                title[max_len - 1] = '\0';
            }
        }
        if(strlen(raw_artist) > 0) {
            if(gbk_to_utf8(raw_artist, artist, max_len) != 0) {
                strncpy(artist, raw_artist, max_len - 1);
                artist[max_len - 1] = '\0';
            }
        }
    }
}

static void read_lrc_file(const char * mp3_path, char * lyrics, size_t max_len)
{
    /* Show the first timestamped lyric, skipping track/artist metadata. */
    char lrc_path[128];
    strncpy(lrc_path, mp3_path, sizeof(lrc_path) - 1);

    char * ext = strstr(lrc_path, ".mp3");
    if(ext) {
        strcpy(ext, ".lrc");
    }

    FILE * fp = fopen(lrc_path, "r");
    if(!fp) {
        snprintf(lyrics, max_len, "No lyrics available");
        return;
    }

    char line[256];
    while(fgets(line, sizeof(line), fp)) {
        if(line[0] == '[' && (line[1] < '0' || line[1] > '9')) continue;

        if(line[0] == '[' && line[1] >= '0' && line[1] <= '9') {
            char * text = strchr(line, ']');
            if(text && strlen(text) > 1) {
                text++;
                char * nl = strchr(text, '\n');
                if(nl) *nl = '\0';
                if(strlen(text) > 0) {
                    strncpy(lyrics, text, max_len - 1);
                    fclose(fp);
                    return;
                }
            }
        }
    }
    fclose(fp);
    snprintf(lyrics, max_len, "No lyrics available");
}

static void music_init_info(void)
{
    snprintf(music_names[0], 64, "Synthwave");
    snprintf(music_artists[0], 64, "Audio test");
    snprintf(music_lyrics_text[0], 128, "Ready");
}

static lv_style_t chinese_font_style;

#define COLOR_BG            0x000000
/* Neutral palette: launcher buttons share one gray, accents are white. */
#define COLOR_TILE          0x3C3C3C
#define COLOR_ACCENT        0xFFFFFF

static void switch_to_dashboard(void);
static void switch_to_launcher(void);
static void switch_to_music(void);
static void switch_to_settings(void);
static void clock_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    struct tm calendar;bool valid;
    if(hz_ui_vehicle_mode())valid=preview_vehicle_time(&calendar);
    else {time_t now=time(NULL);valid=localtime_r(&now,&calendar)!=NULL;}
    char text[16]="--:--";
    if(valid)strftime(text,sizeof(text),"%H:%M",&calendar);
    if(status_time_label)hz_label_text(status_time_label,text);
}
static void create_status_bar(lv_obj_t *parent)
{
    struct hz_page_header header=hz_page_header_create(parent,"Home","--:--",NULL,false);
    status_time_label=header.context;
}
static void on_dashboard_click(lv_event_t *event) {(void)event;switch_to_dashboard();}
static void on_music_click(lv_event_t *event) {(void)event;switch_to_music();}
static void on_video_click(lv_event_t *event) {(void)event;hz_bt_music_release();hz_video_open();}
static void on_bt_music_click(lv_event_t *event) {(void)event;hz_bt_music_open();}
static void on_android_auto_click(lv_event_t *event) {(void)event;hz_android_auto_open(false);}
static void on_android_auto_usb_click(lv_event_t *event) {(void)event;hz_android_auto_open(true);}
static void on_settings_click(lv_event_t *event) {(void)event;switch_to_settings();}
#ifdef HZ_DOOM
#include <errno.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
/* fbDOOM draws straight into /dev/fb0 and reads the USB keyboard. The UI loop
 * blocks until it exits, so HMI drawing, touch and vehicle polling pause.
 * Leaving DOOM (Backspace, menu quit, top-left corner hold) repaints the HMI. */
static void on_doom_click(lv_event_t *event)
{
    (void)event;
    extern char **environ;
    char *argv[]={"fbdoom",NULL};
    pid_t pid;
    int error=posix_spawn(&pid,"/usr/bin/fbdoom",NULL,NULL,argv,environ);
    if(error) {fprintf(stderr,"fbdoom: %s\n",strerror(error));return;}
    while(waitpid(pid,NULL,0)<0 && errno==EINTR);
    lv_obj_invalidate(lv_scr_act());
}
#endif
/* 96px PNG tiles from assets/launcher, converted to launcher-<icon>.hzp. */
static lv_obj_t *create_icon_btn(lv_obj_t *parent,int x,int y,lv_img_dsc_t *image,
    const char *icon,const char *name,lv_event_cb_t callback)
{
    char art[48];snprintf(art,sizeof(art),"launcher-%s",icon);
    lv_obj_t *button=lv_btn_create(parent);lv_obj_set_size(button,96,96);lv_obj_set_pos(button,x,y);
    lv_obj_set_style_radius(button,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_grad_dir(button,LV_GRAD_DIR_NONE,0);
    lv_obj_set_style_shadow_width(button,0,0);lv_obj_set_style_border_width(button,0,0);
    if(hz_art_image(art,image)) {
        lv_obj_set_style_bg_opa(button,LV_OPA_TRANSP,0);lv_obj_set_style_bg_img_src(button,image,0);
        lv_obj_set_style_bg_img_recolor(button,lv_color_black(),LV_STATE_PRESSED);
        lv_obj_set_style_bg_img_recolor_opa(button,LV_OPA_30,LV_STATE_PRESSED);
    } else {
        lv_obj_set_style_bg_color(button,lv_color_hex(COLOR_TILE),0); /* artwork missing */
    }
    lv_obj_add_event_cb(button,callback,LV_EVENT_CLICKED,NULL);
    lv_obj_t *caption=lv_label_create(parent);hz_label_text(caption,name);
    lv_obj_set_style_text_font(caption,dashboard_font(14),0);
    lv_obj_set_style_text_color(caption,lv_color_hex(0xaaaaaa),0);
    lv_obj_set_pos(caption,x+48-strlen(name)*4,y+100);
    lv_obj_add_flag(caption,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(caption,callback,LV_EVENT_CLICKED,NULL);
    return button;
}
static void create_app_grid(lv_obj_t *parent)
{
    static const struct {const char *icon,*name;lv_event_cb_t callback;} apps[]={
        {"dashboard","Dashboard",on_dashboard_click},{"music","Music",on_music_click},
        {"media","Video",on_video_click},{"settings","Settings",on_settings_click},
        {"bt-music","BT Music",on_bt_music_click},{"android-auto","Wireless AA",on_android_auto_click},
        {"android-auto-usb","USB AA",on_android_auto_usb_click},
#ifdef HZ_DOOM
        {"gamepad","DOOM",on_doom_click},
#endif
    };
    static lv_img_dsc_t images[16];
    /* 4x4 grid right of the vehicle panel: 168x134 cells over x 352..1023, y 64..599. */
    for(unsigned i=0;i<sizeof(apps)/sizeof(apps[0]);i++)
        create_icon_btn(parent,352+i%4*168+36,72+i/4*134,&images[i],apps[i].icon,apps[i].name,apps[i].callback);
}

static void dashboard_back_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    switch_to_launcher();
}

static void switch_to_dashboard(void)
{
#ifdef HZ_COMBINED_UI
    extern void hz_dashboard_open(void);
    hz_dashboard_open();
    return;
#endif
    if(dashboard_scr == NULL) {
        dashboard_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(dashboard_scr, lv_color_hex(COLOR_BG), 0);

        extern void Car_Dashboard_Build(lv_obj_t * parent);
        Car_Dashboard_Build(dashboard_scr);
        hz_page_header_create(dashboard_scr,"Dashboard",NULL,dashboard_back_cb,true);

    }

    hz_screen_load_anim(dashboard_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

static void switch_to_launcher(void)
{
    if(launcher_scr) {
        hz_screen_load_anim(launcher_scr, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
}


// Settings uses the native split-pane settings module.
static void switch_to_settings(void) { hz_settings_open(); }


static void format_time(char * buf, size_t size, int seconds)
{
    int min = seconds / 60;
    int sec = seconds % 60;
    snprintf(buf, size, "%02d:%02d", min, sec);
}

static void music_update_info(int index)
{
    if(index < 0) index = MUSIC_MAX_TRACKS - 1;
    if(index >= MUSIC_MAX_TRACKS) index = 0;
    music_current_index = index;

    if(music_title_label) {
        hz_label_text(music_title_label, music_names[index]);
    }
    if(music_artist_label) {
        hz_label_text(music_artist_label, music_artists[index]);
    }
    if(music_lyrics_label) {
        hz_label_text(music_lyrics_label, music_lyrics_text[index]);
    }
    if(music_time_total) {
        char buf[16];
        format_time(buf, sizeof(buf), music_durations[index]);
        hz_label_text(music_time_total, buf);
    }
    music_elapsed_sec = 0;
    if(music_progress_slider) {
        lv_slider_set_range(music_progress_slider, 0, music_durations[index]);
        lv_slider_set_value(music_progress_slider, 0, LV_ANIM_OFF);
    }
    if(music_time_current) {
        hz_label_text(music_time_current, "00:00");
    }
}

static void music_stop_playback(void)
{
    hz_music_stop();
    music_playing = false;
    if(music_progress_timer) {
        lv_timer_pause(music_progress_timer);
    }
}

static void music_start_playback(void)
{
    hz_bt_music_release();
    hz_music_play();

    if(music_progress_timer) {
        lv_timer_resume(music_progress_timer);
    }

}

static void music_progress_timer_cb(lv_timer_t * timer)
{
    LV_UNUSED(timer);
    const struct hz_music_state *playback=hz_music_poll();
    music_playing=playback->status==HZ_MUSIC_PLAYING || playback->status==HZ_MUSIC_OPENING;
    if(music_play_icon) hz_label_text(music_play_icon,music_playing?LV_SYMBOL_PAUSE:LV_SYMBOL_PLAY);
    if(music_lyrics_label) {
        const char *status=playback->status==HZ_MUSIC_ERROR?playback->message:
            playback->status==HZ_MUSIC_PAUSED?"Paused":music_playing?"Playing":"Ready";
        hz_label_text(music_lyrics_label,status);
    }
    if(music_slider_dragging) return;
    music_elapsed_sec=playback->position;
    int duration = music_durations[music_current_index];

    if(music_progress_slider && music_elapsed_sec <= (uint32_t)duration) {
        lv_slider_set_value(music_progress_slider, music_elapsed_sec, LV_ANIM_OFF);
    }

    if(music_time_current) {
        char buf[16];
        format_time(buf, sizeof(buf), music_elapsed_sec);
        hz_label_text(music_time_current, buf);
    }

}

static void music_slider_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if(code == LV_EVENT_PRESSING) {
        music_slider_dragging = true;
    }
    else if(code == LV_EVENT_RELEASED) {
        music_slider_dragging = false;
        int new_pos = lv_slider_get_value(music_progress_slider);
        music_elapsed_sec = new_pos;

        if(music_time_current) {
            char buf[16];
            format_time(buf, sizeof(buf), new_pos);
            hz_label_text(music_time_current, buf);
        }

        hz_music_seek(new_pos);
    }
}

static void music_back_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    music_stop_playback();
    if(music_play_icon) {
        hz_label_text(music_play_icon, LV_SYMBOL_PLAY);
    }
    switch_to_launcher();
}

static void music_play_pause_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    if(music_playing) {
        hz_music_pause();
        if(music_play_icon) {
            hz_label_text(music_play_icon, LV_SYMBOL_PLAY);
        }
    } else {
        music_start_playback();
    }
}

static void music_prev_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    music_stop_playback();
    music_update_info(music_current_index - 1);
    if(music_play_icon) {
        hz_label_text(music_play_icon, LV_SYMBOL_PLAY);
    }
}

static void music_next_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    music_stop_playback();
    music_update_info(music_current_index + 1);
    if(music_play_icon) {
        hz_label_text(music_play_icon, LV_SYMBOL_PLAY);
    }
}

static void switch_to_music(void)
{
    uint32_t started=lv_tick_get();bool first=music_scr==NULL;
    if(music_scr == NULL) {
        music_init_info();

        music_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(music_scr, lv_color_hex(COLOR_BG), 0);
        lv_obj_set_style_bg_grad_dir(music_scr, LV_GRAD_DIR_NONE, 0);
        lv_obj_set_style_bg_opa(music_scr, LV_OPA_COVER, 0);

        lv_style_init(&chinese_font_style);
        Font_Init("/Mydata/simfang.ttf", 22, &chinese_font_style);

        hz_page_header_create(music_scr,"Music Player",NULL,music_back_cb,true);

        lv_obj_t * album_card = lv_obj_create(music_scr);
        lv_obj_set_size(album_card, 280, 280);
        lv_obj_align(album_card, LV_ALIGN_CENTER, 0, -60);
        lv_obj_set_style_bg_color(album_card, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(album_card, LV_OPA_80, 0);
        lv_obj_set_style_border_width(album_card, 3, 0);
        lv_obj_set_style_border_color(album_card, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_border_opa(album_card, LV_OPA_60, 0);
        lv_obj_set_style_radius(album_card, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(album_card, 0, 0);
        lv_obj_set_style_shadow_color(album_card, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_shadow_opa(album_card, LV_OPA_40, 0);
        lv_obj_clear_flag(album_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t * music_icon = lv_label_create(album_card);
        hz_label_text(music_icon, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_font(music_icon, dashboard_font(32), 0);
        lv_obj_set_style_text_color(music_icon, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_align(music_icon, LV_ALIGN_CENTER, 0, -30);

        music_title_label = lv_label_create(album_card);
        hz_label_text(music_title_label, music_names[0]);
        lv_obj_add_style(music_title_label, &chinese_font_style, 0);
        lv_obj_set_style_text_color(music_title_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(music_title_label, LV_ALIGN_CENTER, 0, 20);

        music_artist_label = lv_label_create(album_card);
        hz_label_text(music_artist_label, music_artists[0]);
        lv_obj_add_style(music_artist_label, &chinese_font_style, 0);
        lv_obj_set_style_text_color(music_artist_label, lv_color_hex(0x727272), 0);
        lv_obj_align(music_artist_label, LV_ALIGN_CENTER, 0, 50);

        music_lyrics_label = lv_label_create(music_scr);
        hz_label_text(music_lyrics_label, music_lyrics_text[0]);
        lv_obj_add_style(music_lyrics_label, &chinese_font_style, 0);
        lv_obj_set_style_text_color(music_lyrics_label, lv_color_hex(0xA1A1A1), 0);
        lv_obj_align(music_lyrics_label, LV_ALIGN_CENTER, 0, 95);

        music_progress_slider = lv_slider_create(music_scr);
        lv_obj_set_size(music_progress_slider, 400, 10);
        lv_obj_align(music_progress_slider, LV_ALIGN_CENTER, 0, 130);
        lv_slider_set_range(music_progress_slider, 0, music_durations[0]);
        lv_slider_set_value(music_progress_slider, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(music_progress_slider, lv_color_hex(0x282828), 0);
        lv_obj_set_style_bg_opa(music_progress_slider, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(music_progress_slider, 5, 0);
        lv_obj_set_style_bg_color(music_progress_slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
        lv_obj_set_style_radius(music_progress_slider, 5, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(music_progress_slider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
        lv_obj_set_style_pad_all(music_progress_slider, 3, LV_PART_KNOB);
        lv_obj_add_event_cb(music_progress_slider, music_slider_event_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(music_progress_slider, music_slider_event_cb, LV_EVENT_RELEASED, NULL);

        music_time_current = lv_label_create(music_scr);
        hz_label_text(music_time_current, "00:00");
        lv_obj_set_style_text_font(music_time_current, dashboard_font(14), 0);
        lv_obj_set_style_text_color(music_time_current, lv_color_hex(0x727272), 0);
        lv_obj_align(music_time_current, LV_ALIGN_CENTER, -220, 130);

        music_time_total = lv_label_create(music_scr);
        char time_buf[16];
        format_time(time_buf, sizeof(time_buf), music_durations[0]);
        hz_label_text(music_time_total, time_buf);
        lv_obj_set_style_text_font(music_time_total, dashboard_font(14), 0);
        lv_obj_set_style_text_color(music_time_total, lv_color_hex(0x727272), 0);
        lv_obj_align(music_time_total, LV_ALIGN_CENTER, 220, 130);

        music_progress_timer = lv_timer_create(music_progress_timer_cb, 100, NULL);

        lv_obj_t * ctrl_container = lv_obj_create(music_scr);
        lv_obj_set_size(ctrl_container, 400, 100);
        lv_obj_align(ctrl_container, LV_ALIGN_CENTER, 0, 200);
        lv_obj_set_style_bg_opa(ctrl_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ctrl_container, 0, 0);
        lv_obj_set_style_pad_all(ctrl_container, 0, 0);
        lv_obj_clear_flag(ctrl_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(ctrl_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ctrl_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t * prev_btn = lv_btn_create(ctrl_container);
        lv_obj_set_size(prev_btn, 70, 70);
        lv_obj_set_style_bg_color(prev_btn, lv_color_black(), 0);
        lv_obj_set_style_border_width(prev_btn, 2, 0);
        lv_obj_set_style_border_color(prev_btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_radius(prev_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(prev_btn, 0, 0);
        lv_obj_set_style_shadow_color(prev_btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_shadow_opa(prev_btn, LV_OPA_30, 0);

        lv_obj_t * prev_icon = lv_label_create(prev_btn);
        hz_label_text(prev_icon, LV_SYMBOL_PREV);
        lv_obj_set_style_text_font(prev_icon, dashboard_font(22), 0);
        lv_obj_set_style_text_color(prev_icon, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_center(prev_icon);
        lv_obj_add_event_cb(prev_btn, music_prev_cb, LV_EVENT_CLICKED, NULL);

        music_play_btn = lv_btn_create(ctrl_container);
        lv_obj_set_size(music_play_btn, 90, 90);
        lv_obj_set_style_bg_color(music_play_btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_radius(music_play_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(music_play_btn, 0, 0);
        lv_obj_set_style_shadow_color(music_play_btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_shadow_opa(music_play_btn, LV_OPA_50, 0);

        music_play_icon = lv_label_create(music_play_btn);
        hz_label_text(music_play_icon, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_font(music_play_icon, dashboard_font(32), 0);
        lv_obj_set_style_text_color(music_play_icon, lv_color_hex(0x000000), 0);
        lv_obj_center(music_play_icon);
        lv_obj_add_event_cb(music_play_btn, music_play_pause_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t * next_btn = lv_btn_create(ctrl_container);
        lv_obj_set_size(next_btn, 70, 70);
        lv_obj_set_style_bg_color(next_btn, lv_color_black(), 0);
        lv_obj_set_style_border_width(next_btn, 2, 0);
        lv_obj_set_style_border_color(next_btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_radius(next_btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(next_btn, 0, 0);
        lv_obj_set_style_shadow_color(next_btn, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_shadow_opa(next_btn, LV_OPA_30, 0);

        lv_obj_t * next_icon = lv_label_create(next_btn);
        hz_label_text(next_icon, LV_SYMBOL_NEXT);
        lv_obj_set_style_text_font(next_icon, dashboard_font(22), 0);
        lv_obj_set_style_text_color(next_icon, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_center(next_icon);
        lv_obj_add_event_cb(next_btn, music_next_cb, LV_EVENT_CLICKED, NULL);

    }

     {lv_obj_t *live[16];unsigned n=hz_launcher_live("music",live,16);hz_page_art_bind(music_scr,"music",live,n);}
    hz_page_profile("music",started,first);
    hz_screen_load_anim(music_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

lv_obj_t * Car_Launcher_Get_Screen(void)
{
    return launcher_scr;
}

void Car_Launcher_Gui(void)
{

    launcher_scr = lv_obj_create(NULL);
    /* Fixed native geometry: dragging between icons must not scroll Home. */
    lv_obj_clear_flag(launcher_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(launcher_scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_style_bg_color(launcher_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_dir(launcher_scr, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(launcher_scr, LV_OPA_COVER, 0);

    bool prepared_art=hz_page_art_load(launcher_scr,"home");

    create_status_bar(launcher_scr);
    car_panel=hz_vehicle_body_create(launcher_scr);
    create_app_grid(launcher_scr);
    if(prepared_art) {lv_obj_t *live[16];unsigned n=hz_launcher_live("home",live,16);hz_page_art_bind(launcher_scr,"home",live,n);}

    clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);

    clock_timer_cb(NULL);

    lv_scr_load(launcher_scr);
}

unsigned hz_launcher_live(const char *page,lv_obj_t **objects,unsigned capacity) {
    lv_obj_t *home[]={status_time_label,car_panel};
    lv_obj_t *music[]={music_title_label,music_artist_label,music_lyrics_label,music_progress_slider,music_time_current,music_time_total,music_play_btn};
    lv_obj_t **list=NULL;unsigned count=0;
    if(!strcmp(page,"home")) {list=home;count=2;}
    if(!strcmp(page,"music")) {list=music;count=7;}
    unsigned copied=0;for(unsigned i=0;i<count && copied<capacity;i++)if(list[i])objects[copied++]=list[i];
    return copied;
}

/* Desktop preview page selection; original navigation remains available. */
int preview_rk_page(const char *page)
{
    if (!strcmp(page, "main")) return 0;
    if (!strcmp(page, "dashboard")) switch_to_dashboard();
    else if (!strcmp(page, "music")) switch_to_music();
    else if (!strcmp(page, "video")) hz_video_open();
    else if (!strcmp(page, "settings")) switch_to_settings();
    else if (!strcmp(page, "gear-calibration")) hz_gear_calibration_open();
    else { fprintf(stderr, "Unknown page: %s\n", page); return -1; }
    return 0;
}
