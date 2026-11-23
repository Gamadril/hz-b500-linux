/* SPDX-License-Identifier: GPL-2.0-only */
#include "settings_store.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static const struct hz_settings_driver *driver;
static bool started, pending;
static bool light_retry;
static uint64_t light_retry_after;
static int job=-1, requested;
#define RENAME_JOB (-2)
static char requested_name[HZ_BLUETOOTH_NAME_MAX+1];
static struct hz_settings_snapshot state={
    .values={.brightness=128,.dimmed_brightness=64,.picture={50,50,50,50},.volume=18,.fuel_litres=63,.speed_clock_hz=4000,
        .audio_available=true,.timezone="Europe/Berlin",.bluetooth_name="SWL-BT",.source="sd"},
    .ready=true,.preview=true,.message="Desktop preview: changes last for this session only."
};
static int *field(struct hz_settings_values *values,enum hz_setting key)
{
    switch(key) {
    case HZ_BRIGHTNESS:return &values->brightness;
    case HZ_DIMMED_BRIGHTNESS:return &values->dimmed_brightness;
    case HZ_VOLUME:return &values->volume;
    case HZ_MUTE:return &values->mute;
    case HZ_SPEAKER:return &values->speaker;
    case HZ_BLUETOOTH:return &values->bluetooth;
    case HZ_LCD_BRIGHT: case HZ_LCD_CONTRAST: case HZ_LCD_HUE: case HZ_LCD_SATURATION:
        return &values->picture[key-HZ_LCD_BRIGHT];
    default:return NULL;
    }
}
static void *worker(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&mutex);
    for(;;) {
        while(!pending)pthread_cond_wait(&wake,&mutex);
        int key=job,value=requested;
        char name[sizeof(requested_name)];snprintf(name,sizeof(name),"%s",requested_name);
        struct hz_settings_values values=state.values;
        pending=false;
        pthread_mutex_unlock(&mutex);
        char message[160]="Settings loaded.";
        if(key>=0 && driver->apply(key,value,message,sizeof(message))>=0)*field(&values,key)=value;
        if(key==RENAME_JOB)driver->rename(name,message,sizeof(message));
        int result=driver->load(&values);
        if(result)snprintf(message,sizeof(message),"Cannot read device settings. Tap Refresh to retry.");
        pthread_mutex_lock(&mutex);
        state.values=values;state.ready=!result;state.busy=false;
        snprintf(state.message,sizeof(state.message),"%s",message);
    }
    return NULL;
}
/* Called once before the UI is constructed. Preview builds install no driver. */
void hz_settings_init(const struct hz_settings_driver *backend,unsigned brightness,unsigned dimmed_brightness)
{
    driver=backend;state.preview=backend==NULL;state.ready=backend==NULL;
    state.values.brightness=brightness;
    state.values.dimmed_brightness=dimmed_brightness;
}
void hz_settings_lights(int on)
{
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    uint64_t milliseconds=(uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
    pthread_mutex_lock(&mutex);
    bool selected=on<0?state.lights_on:on!=0;
    bool apply=state.lights_on!=selected || (light_retry && milliseconds>=light_retry_after);
    state.lights_on=selected;
    pthread_mutex_unlock(&mutex);
    if(apply && driver && driver->lights) {
        int result=driver->lights(selected);
        pthread_mutex_lock(&mutex);
        light_retry=result!=0;light_retry_after=milliseconds+1000;
        const char *failure="Could not apply automatic backlight.";
        if(result)snprintf(state.message,sizeof(state.message),"%s",failure);
        else if(!strcmp(state.message,failure))snprintf(state.message,sizeof(state.message),"Automatic backlight applied.");
        pthread_mutex_unlock(&mutex);
    }
}
void hz_settings_preview(enum hz_setting key,int value)
{
    bool valid=HZ_SETTING_PICTURE(key)?value>=0 && value<=100:
        (key==HZ_BRIGHTNESS || key==HZ_DIMMED_BRIGHTNESS) && value>=16 && value<=255;
    if(valid && driver && driver->preview)(void)driver->preview(key,value);
}
void hz_settings_preview_restore(enum hz_setting key)
{
    if(!driver)return;
    pthread_mutex_lock(&mutex);
    int saved=HZ_SETTING_PICTURE(key)?state.values.picture[key-HZ_LCD_BRIGHT]:-1;bool lights_on=state.lights_on;
    pthread_mutex_unlock(&mutex);
    if(saved>=0 && driver->preview)(void)driver->preview(key,saved);
    else if(saved<0 && (key==HZ_BRIGHTNESS || key==HZ_DIMMED_BRIGHTNESS) && driver->lights)(void)driver->lights(lights_on);
}
static bool queue(int key,int value)
{
    if(state.busy)return false;
    if(!started) {
        pthread_t thread;
        if(pthread_create(&thread,NULL,worker,NULL)) {
            snprintf(state.message,sizeof(state.message),"Settings worker unavailable. Tap Refresh to retry.");
            return false;
        }
        pthread_detach(thread);started=true;
    }
    job=key;requested=value;pending=true;state.busy=true;
    snprintf(state.message,sizeof(state.message),"%s",key==-1?"Loading settings...":"Applying settings...");
    pthread_cond_signal(&wake);return true;
}
void hz_settings_refresh(void)
{
    pthread_mutex_lock(&mutex);
    if(driver)queue(-1,0);
    pthread_mutex_unlock(&mutex);
}
bool hz_settings_change(enum hz_setting key,int value)
{
    if(key<0 || key>=HZ_SETTING_COUNT || value<0 ||
       ((key==HZ_BRIGHTNESS || key==HZ_DIMMED_BRIGHTNESS) && (value<16 || value>255)) ||
       (key==HZ_VOLUME && value>20) ||
       (HZ_SETTING_PICTURE(key) && value>100) ||
       ((key==HZ_MUTE || key==HZ_SPEAKER || key==HZ_BLUETOOTH) && value>1))return false;
    pthread_mutex_lock(&mutex);
    bool accepted=false;
    if(state.ready && !state.busy && (HZ_SETTING_DISPLAY(key) || state.values.audio_available)) {
        if(driver)accepted=queue(key,value);
        else {*field(&state.values,key)=value;accepted=true;}
    }
    pthread_mutex_unlock(&mutex);return accepted;
}
bool hz_settings_valid_name(const char *name)
{
    size_t length=strlen(name);
    if(!length || length>HZ_BLUETOOTH_NAME_MAX)return false;
    for(size_t i=0;i<length;i++)if((unsigned char)name[i]<33 || (unsigned char)name[i]>126)return false;
    return true;
}
bool hz_settings_rename(const char *name)
{
    if(!hz_settings_valid_name(name))return false;
    pthread_mutex_lock(&mutex);
    bool accepted=false;
    if(state.ready && !state.busy && state.values.audio_available) {
        snprintf(requested_name,sizeof(requested_name),"%s",name);
        if(driver)accepted=queue(RENAME_JOB,0);
        else {snprintf(state.values.bluetooth_name,sizeof(state.values.bluetooth_name),"%s",name);accepted=true;}
    }
    pthread_mutex_unlock(&mutex);return accepted;
}
void hz_settings_snapshot(struct hz_settings_snapshot *out)
{
    pthread_mutex_lock(&mutex);*out=state;pthread_mutex_unlock(&mutex);
}
