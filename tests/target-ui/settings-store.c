/* SPDX-License-Identifier: GPL-2.0-only */
#include "settings_store.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static int entered,released,result;
static int light_changes,fail_lights;
static unsigned milliseconds=1000;
int __wrap_clock_gettime(clockid_t id,struct timespec *now)
{
    assert(id==CLOCK_MONOTONIC);*now=(struct timespec){milliseconds/1000,(milliseconds%1000)*1000000};return 0;
}
static int lights(bool on) {(void)on;light_changes++;return fail_lights?-1:0;}
static int previews,preview_key,preview_value;
static int preview(enum hz_setting key,int value) {previews++;preview_key=key;preview_value=value;return 0;}
static int load(struct hz_settings_values *values) {values->audio_available=true;return 0;}
static int apply(enum hz_setting key,int value,char *message,size_t size)
{
    assert(key==HZ_BRIGHTNESS && value>=16 && value<=255);
    pthread_mutex_lock(&gate);entered=1;pthread_cond_signal(&wake);
    while(!released)pthread_cond_wait(&wake,&gate);
    pthread_mutex_unlock(&gate);
    snprintf(message,size,result<0?"Rejected":result?"Applied, not saved":"Saved");return result;
}
static void settled(struct hz_settings_snapshot *state)
{
    for(int i=0;i<2000;i++) {hz_settings_snapshot(state);if(!state->busy)return;usleep(1000);}assert(0);
}
int main(void)
{
    const struct hz_settings_driver driver={.load=load,.apply=apply,.lights=lights,.preview=preview};
    hz_settings_init(&driver,128,64);hz_settings_refresh();struct hz_settings_snapshot state;settled(&state);
    assert(state.ready && !state.preview);
    assert(!hz_settings_change(HZ_BRIGHTNESS,0));assert(!hz_settings_change(HZ_VOLUME,21));assert(!hz_settings_change(HZ_MUTE,2));
    assert(!hz_settings_change(HZ_BLUETOOTH,2));
    assert(!hz_settings_change(HZ_LCD_HUE,101) && !hz_settings_change(HZ_LCD_BRIGHT,-1));
    /* Dragging previews picture values only, in range, without the worker. */
    hz_settings_preview(HZ_LCD_HUE,60);assert(previews==1 && preview_key==HZ_LCD_HUE && preview_value==60);
    hz_settings_preview(HZ_LCD_HUE,101);hz_settings_preview(HZ_BRIGHTNESS,15);hz_settings_preview(HZ_VOLUME,10);assert(previews==1);
    hz_settings_preview(HZ_DIMMED_BRIGHTNESS,200);assert(previews==2 && preview_key==HZ_DIMMED_BRIGHTNESS && preview_value==200);
    /* Restore: saved picture value; backlight goes back to the level the lights select. */
    hz_settings_preview_restore(HZ_LCD_HUE);assert(previews==3 && preview_key==HZ_LCD_HUE && preview_value==50);
    int lights_before=light_changes;hz_settings_preview_restore(HZ_DIMMED_BRIGHTNESS);assert(light_changes==lights_before+1 && previews==3);light_changes=0;
    hz_settings_snapshot(&state);assert(state.values.picture[2]==50);
    assert(hz_settings_valid_name("SWL-BT") && hz_settings_valid_name("0123456789ABCDEF"));
    assert(!hz_settings_valid_name("") && !hz_settings_valid_name("has space") && !hz_settings_valid_name("0123456789ABCDEFG"));
    assert(hz_settings_change(HZ_BRIGHTNESS,64));
    pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&wake,&gate);pthread_mutex_unlock(&gate);
    hz_settings_snapshot(&state);assert(state.busy && state.values.brightness==128);
    fail_lights=1;hz_settings_lights(true);hz_settings_lights(true);hz_settings_snapshot(&state);
    assert(state.lights_on && light_changes==1); /* Never waits for the busy service worker. */
    milliseconds=1999;hz_settings_lights(-1);assert(light_changes==1);
    milliseconds=2000;fail_lights=0;hz_settings_lights(-1);assert(light_changes==2);
    hz_settings_lights(false);assert(light_changes==3);
    assert(!hz_settings_change(HZ_VOLUME,10)); /* No stale overlapping updates. */
    hz_settings_refresh(); /* A refresh cannot replace an in-flight apply. */
    pthread_mutex_lock(&gate);released=1;pthread_cond_signal(&wake);pthread_mutex_unlock(&gate);
    settled(&state);assert(state.values.brightness==64 && !strcmp(state.message,"Saved"));
    result=-1;assert(hz_settings_change(HZ_BRIGHTNESS,90));settled(&state);
    assert(state.values.brightness==64 && !strcmp(state.message,"Rejected"));
    result=1;assert(hz_settings_change(HZ_BRIGHTNESS,90));settled(&state);
    assert(state.values.brightness==90 && !strcmp(state.message,"Applied, not saved"));
    puts("PASS: settings worker stays responsive during blocked hardware I/O, serializes updates, rejects invalid ranges, and retains actual brightness when saving fails");
    return 0;
}
