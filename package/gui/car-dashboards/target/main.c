/* SPDX-License-Identifier: GPL-2.0-only */
#include "linux_display.h"
#include "navigation.h"
#include "system_fonts.h"
#include "vehicle_ui.h"
#include "page_art.h"
#include "video_plane.h"
#include "settings_device.h"
#include "bt_music.h"
#include "android_auto.h"
#include "mode/Car_Launcher.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdlib.h>

static volatile sig_atomic_t quit;
static int instance_lock=-1;
static void stop(int number) {(void)number;quit=1;}
static void close_ui(void)
{
    /* Registered before media workers' atexit handlers: they finish and release
     * their planes first. Keep the instance lock until display cleanup is done. */
    hz_display_close();
    unlink("/var/run/hz-ui.ready");
    if(instance_lock>=0) {close(instance_lock);instance_lock=-1;}
}
static void print_log(const char *message) {fputs(message,stderr);}
static void boot_stage(const char *stage) {
    struct timespec now;clock_gettime(CLOCK_BOOTTIME,&now);
    char line[192];int size=snprintf(line,sizeof(line),"[%llu.%03lu] UI %s\n",
        (unsigned long long)now.tv_sec,(unsigned long)now.tv_nsec/1000000,stage);
    fputs(line,stderr);
    if(getenv("HZ_UI_BOOT_LOG")) {
        int fd=open("/var/run/hz-ui.boot",O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0600);
        if(fd>=0) {(void)write(fd,line,size);close(fd);}
        fd=open("/dev/console",O_WRONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
        if(fd>=0) {(void)write(fd,line,size);close(fd);}
    }
}
static void frame_profile(lv_disp_drv_t *display,uint32_t elapsed,uint32_t pixels)
{
    static bool first=true;
    if(first) {
        first=false;
        struct timespec uptime;clock_gettime(CLOCK_BOOTTIME,&uptime);
        unsigned long long milliseconds=(unsigned long long)uptime.tv_sec*1000+uptime.tv_nsec/1000000;
        FILE *ready=fopen("/var/run/hz-ui.ready","w");
        if(ready) {fprintf(ready,"%llu\n",milliseconds);fclose(ready);}
        fprintf(stderr,"UI first frame: %llu ms since kernel boot\n",milliseconds);
        boot_stage("first frame presented");
    }
    if(!getenv("HZ_UI_PROFILE"))return;
    lv_disp_t *screen=_lv_refr_get_disp_refreshing();
    (void)display;
    fprintf(stderr,"UI redraw: %u ms, %u pixels (%s)\n",elapsed,pixels,
        screen && screen->prev_scr?"transition":"steady");
}
uint32_t hz_ui_milliseconds(void)
{
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (uint32_t)((uint64_t)now.tv_sec*1000+now.tv_nsec/1000000);
}
void Car_Dashboard_Vehicle_Mode(void);
/* The receiver also read the touchscreen; drop its taps before the HMI resumes. */
static int launch_android_auto(bool usb,char *message,size_t size)
{
    int result=hz_android_auto_launch(usb,message,size);
    hz_touch_discard();return result;
}
int main(int argc,char **argv)
{
    const char *fb="/dev/fb0",*touch="auto",*socket=NULL,*page="home";
    unsigned brightness=128,dimmed_brightness=64;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--framebuffer") && i+1<argc)fb=argv[++i];
        else if(!strcmp(argv[i],"--touch-device") && i+1<argc)touch=argv[++i];
        else if(!strcmp(argv[i],"--ubus-socket") && i+1<argc)socket=argv[++i];
        else if(!strcmp(argv[i],"--page") && i+1<argc)page=argv[++i];
        else if((!strcmp(argv[i],"--brightness") || !strcmp(argv[i],"--dimmed-brightness")) && i+1<argc) {
            bool dimmed=!strcmp(argv[i],"--dimmed-brightness");
            char *end;unsigned long value=strtoul(argv[++i],&end,10);
            if(!*argv[i] || *end || value>255)return 2;
            if(dimmed)dimmed_brightness=value;else brightness=value;
        }
        else {fprintf(stderr,"Usage: hz-ui [--framebuffer PATH] [--touch-device auto|PATH] [--ubus-socket PATH] [--page home|dashboard] [--brightness 0..255] [--dimmed-brightness 0..255]\n");return 2;}
    }
    if(strcmp(page,"home") && strcmp(page,"dashboard"))return 2;
    int lock=open("/var/run/hz-ui.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) {fprintf(stderr,"UI already running or lock unavailable\n");return 1;}
    instance_lock=lock;
    atexit(close_ui);
    unlink("/var/run/hz-ui.ready");
    fprintf(stderr,"UI starting: %u ms since kernel boot\n",hz_ui_milliseconds());
    boot_stage("main entered");
    signal(SIGINT,stop);signal(SIGTERM,stop);
    if(hz_display_init(fb,touch))return 1;
    (void)hz_lcd_brightness(brightness);
    hz_settings_device_init(brightness,dimmed_brightness);
    hz_settings_init(&hz_settings_device,brightness,dimmed_brightness);
    hz_bt_music_init(hz_companion);
    hz_android_auto_init(launch_android_auto);
    (void)hz_video_plane_recover();
    boot_stage("display/touch/G2D ready");
    lv_init();lv_log_register_print_cb(print_log);dashboard_fonts_init();
    boot_stage("core fonts ready");
    /* The 40-line CPU fallback is used if the cached G2D surface is unavailable. */
    static lv_color_t pixels[1024*40];
    static lv_disp_draw_buf_t buffer;
    static lv_disp_drv_t display;lv_disp_drv_init(&display);
    hz_display_configure(&display,&buffer,pixels,1024*40);
    display.monitor_cb=frame_profile;
    lv_disp_drv_register(&display);
    static lv_indev_drv_t input;lv_indev_drv_init(&input);
    input.type=LV_INDEV_TYPE_POINTER;input.read_cb=hz_touch_read;lv_indev_drv_register(&input);
    hz_ui_set_vehicle_mode(1);
    uint32_t build_start=hz_ui_milliseconds();
    Car_Dashboard_Vehicle_Mode();Car_Launcher_Gui();hz_home_open();
    boot_stage(hz_page_art_ready(Car_Launcher_Get_Screen())?"home ready (build-time artwork)":"home ready (live artwork fallback)");
    if(getenv("HZ_UI_PROFILE"))fprintf(stderr,"UI home construction: %u ms\n",hz_ui_milliseconds()-build_start);
    if(preview_vehicle_start(socket,0))return 1;
    if(!strcmp(page,"dashboard"))hz_dashboard_open();
    while(!quit) {
        preview_vehicle_poll();lv_timer_handler();usleep(5000);
    }
    return 0;
}
