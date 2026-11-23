/* SPDX-License-Identifier: GPL-2.0-only */
#include "video_player.h"
#include "music_player.h"
#include <stdio.h>
#include <string.h>
#if defined(HZ_VIDEO_PLAYBACK) && defined(HZ_TARGET_UI)
#include "video_plane.h"
#endif
void hz_video_clear_stale_plane(void) {
#if defined(HZ_VIDEO_PLAYBACK) && defined(HZ_TARGET_UI)
    (void)hz_video_plane_recover();
#endif
}
#ifdef HZ_VIDEO_PLAYBACK
#include <tplayer.h>
#include <pthread.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#ifdef HZ_VIDEO_DYNAMIC
#include "cedar_loader.h"
#else
static bool load_cedar(void) {return true;}
#endif
#ifndef HZ_VIDEO_INPUT
#define HZ_VIDEO_INPUT HZ_VIDEO_TRACK
#endif
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed;
static pthread_t thread;
static bool started,quitting,seek_pending,hidden,ended,media_error;
static unsigned generation,seek_seconds;
static enum hz_video_status desired=HZ_VIDEO_STOPPED;
static struct hz_video_state latest={.duration=HZ_VIDEO_DURATION,.idle=true};
struct callback_context {unsigned generation;};
static int notification(void *arg,int message,int ext,void *data) {
    (void)ext;(void)data;
    struct callback_context *context=arg;
    pthread_mutex_lock(&lock);
    if(context->generation==generation &&
       (message==TPLAYER_NOTIFY_PLAYBACK_COMPLETE || message==TPLAYER_NOTIFY_MEDIA_ERROR)) {
        if(message==TPLAYER_NOTIFY_PLAYBACK_COMPLETE) ended=true;
        if(message==TPLAYER_NOTIFY_MEDIA_ERROR) media_error=true;
        pthread_cond_signal(&changed);
    }
    pthread_mutex_unlock(&lock);return 0;
}
static void failed(unsigned current,const char *message) {
    pthread_mutex_lock(&lock);
    if(generation==current) {
        desired=HZ_VIDEO_STOPPED;latest.status=HZ_VIDEO_ERROR;
        snprintf(latest.message,sizeof(latest.message),"%s",message);
        fprintf(stderr,"Video: %s\n",message);
    }
    pthread_mutex_unlock(&lock);
}
static void release_player(TPlayer **player) {
    if(*player) {
        TPlayerSetVideoDisplay(*player,false);
        TPlayerStop(*player);TPlayerDestroy(*player);*player=NULL;
        /* The player's threads leave several MB free in their glibc arenas;
         * without returning it, the next player runs the 64 MB board OOM. */
        malloc_trim(0);
    }
    pthread_mutex_lock(&lock);latest.idle=desired==HZ_VIDEO_STOPPED;
    if(latest.idle && latest.status==HZ_VIDEO_CLOSING) latest.status=HZ_VIDEO_STOPPED;
    pthread_mutex_unlock(&lock);
}
static void wait_update(void) {
    struct timespec deadline;clock_gettime(CLOCK_MONOTONIC,&deadline);
    deadline.tv_nsec+=50000000;
    if(deadline.tv_nsec>=1000000000) {deadline.tv_nsec-=1000000000;deadline.tv_sec++;}
    pthread_mutex_lock(&lock);
    pthread_cond_timedwait(&changed,&lock,&deadline);
    pthread_mutex_unlock(&lock);
}
static void *playback(void *unused) {
    (void)unused;
    TPlayer *player=NULL;
    struct callback_context context={0};
    enum hz_video_status actual=HZ_VIDEO_STOPPED;
    bool was_hidden=false;
    for(;;) {
        pthread_mutex_lock(&lock);
        bool quit=quitting,is_hidden=hidden,eof=ended,error=media_error,seek=seek_pending;
        unsigned current=generation,position=seek_seconds;
        enum hz_video_status want=desired;
        pthread_mutex_unlock(&lock);
        if(quit) break;
        if(error && player) {failed(current,"Video decoding or output failed");want=HZ_VIDEO_STOPPED;}
        if(eof && player) {
            pthread_mutex_lock(&lock);
            if(generation==current && current==context.generation) {
                desired=HZ_VIDEO_STOPPED;latest.status=HZ_VIDEO_CLOSING;latest.position=0;
            }
            pthread_mutex_unlock(&lock);want=HZ_VIDEO_STOPPED;
        }
        if(want==HZ_VIDEO_STOPPED || (player && context.generation!=current)) {
            release_player(&player);actual=HZ_VIDEO_STOPPED;
            pthread_mutex_lock(&lock);
            while(!quitting && desired==HZ_VIDEO_STOPPED) pthread_cond_wait(&changed,&lock);
            pthread_mutex_unlock(&lock);continue;
        }
        if(!player) {
            if(!hz_music_quiescent()) {wait_update();continue;}
            pthread_mutex_lock(&lock);
            if(generation!=current || desired==HZ_VIDEO_STOPPED || quitting) {
                pthread_mutex_unlock(&lock);continue;
            }
            context.generation=current;latest.idle=false;ended=media_error=false;
            pthread_mutex_unlock(&lock);
            if(!load_cedar()) {failed(current,"Cannot load Cedar video runtime");continue;}
            /* A user can leave while dlopen/plugin initialization is running. */
            pthread_mutex_lock(&lock);
            bool cancelled=generation!=current || desired==HZ_VIDEO_STOPPED || quitting;
            pthread_mutex_unlock(&lock);
            if(cancelled)continue;
            player=TPlayerCreate(CEDARX_PLAYER);
            if(!player) {failed(current,"Cannot initialize Cedar video playback");continue;}
            TPlayerSetNotifyCallback(player,notification,&context);
            TPlayerSetHoldLastPicture(player,0);
            if(TPlayerSetDataSource(player,HZ_VIDEO_INPUT,NULL) || TPlayerPrepare(player)) {
                failed(current,"Cannot open or prepare the video file");release_player(&player);continue;
            }
            TPlayerSetDisplayRect(player,112,78,800,450);
            TPlayerSetVideoDisplay(player,!is_hidden);was_hidden=is_hidden;
            int duration=0;TPlayerGetDuration(player,&duration);
            pthread_mutex_lock(&lock);
            if(generation==current && duration>0) latest.duration=duration/1000;
            pthread_mutex_unlock(&lock);
            actual=HZ_VIDEO_STOPPED;
        }
        /* Preparation can take time: cancellation must win before starting output. */
        pthread_mutex_lock(&lock);
        bool cancelled=generation!=current || desired==HZ_VIDEO_STOPPED || quitting;
        want=desired;is_hidden=hidden;
        pthread_mutex_unlock(&lock);
        if(cancelled) continue;
        if(is_hidden!=was_hidden) {TPlayerSetVideoDisplay(player,!is_hidden);was_hidden=is_hidden;}
        if(seek) {
            pthread_mutex_lock(&lock);
            if(generation==current && seek_seconds==position) seek_pending=false;
            pthread_mutex_unlock(&lock);
            if(TPlayerSeekTo(player,position*1000)) {failed(current,"Video seeking failed");continue;}
        }
        if(want!=actual) {
            if(want==HZ_VIDEO_PAUSED && actual==HZ_VIDEO_STOPPED && TPlayerStart(player)) {
                failed(current,"Cannot start video playback");continue;
            }
            int result=want==HZ_VIDEO_PAUSED?TPlayerPause(player):TPlayerStart(player);
            if(result) {failed(current,"Cannot change video playback state");continue;}
            actual=want;
        }
        int milliseconds=-1;
        if(TPlayerGetCurrentPosition(player,&milliseconds)) milliseconds=-1;
        pthread_mutex_lock(&lock);
        if(generation==current && desired==want) {
            latest.status=actual;if(milliseconds>=0) latest.position=milliseconds/1000;
        }
        pthread_mutex_unlock(&lock);
        wait_update();
    }
    release_player(&player);return NULL;
}
static void shutdown_player(void) {
    pthread_mutex_lock(&lock);quitting=true;pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
    pthread_join(thread,NULL);
}
const struct hz_video_state *hz_video_poll(void) {
    static struct hz_video_state copy;
    pthread_mutex_lock(&lock);copy=latest;pthread_mutex_unlock(&lock);return &copy;
}
void hz_video_play(void) {
    hz_music_stop();
    pthread_mutex_lock(&lock);
    if(!started) {
        pthread_condattr_t attributes;pthread_condattr_init(&attributes);
        pthread_condattr_setclock(&attributes,CLOCK_MONOTONIC);
        pthread_cond_init(&changed,&attributes);pthread_condattr_destroy(&attributes);
        if(pthread_create(&thread,NULL,playback,NULL)) {
            pthread_cond_destroy(&changed);
            latest.status=HZ_VIDEO_ERROR;
            snprintf(latest.message,sizeof(latest.message),"Cannot start video worker");
            pthread_mutex_unlock(&lock);return;
        }
        started=true;atexit(shutdown_player);
    }
    if(desired==HZ_VIDEO_STOPPED) {generation++;ended=media_error=false;}
    desired=HZ_VIDEO_PLAYING;latest.status=HZ_VIDEO_OPENING;latest.idle=false;latest.message[0]=0;
    pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
}
void hz_video_pause(void) {
    pthread_mutex_lock(&lock);if(desired==HZ_VIDEO_PLAYING) desired=HZ_VIDEO_PAUSED;
    if(started) pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
}
void hz_video_stop(void) {
    pthread_mutex_lock(&lock);generation++;desired=HZ_VIDEO_STOPPED;seek_pending=false;
    latest.position=0;latest.status=latest.idle?HZ_VIDEO_STOPPED:HZ_VIDEO_CLOSING;
    if(started) pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
}
void hz_video_seek(unsigned seconds) {
    pthread_mutex_lock(&lock);if(seconds>latest.duration) seconds=latest.duration;
    seek_seconds=seconds;seek_pending=true;latest.position=seconds;
    if(started) pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
}
void hz_video_obscured(bool obscured) {
    pthread_mutex_lock(&lock);
    if(hidden!=obscured) {
        hidden=obscured;
        if(started) pthread_cond_signal(&changed);
    }
    pthread_mutex_unlock(&lock);
}
#else
static struct hz_video_state state={.duration=HZ_VIDEO_DURATION,.idle=true};
const struct hz_video_state *hz_video_poll(void) {return &state;}
void hz_video_play(void) {state.status=HZ_VIDEO_ERROR;snprintf(state.message,sizeof(state.message),"Video playback is available on the target");}
void hz_video_pause(void) {}
void hz_video_stop(void) {state.status=HZ_VIDEO_STOPPED;state.position=0;}
void hz_video_seek(unsigned seconds) {state.position=seconds;}
void hz_video_obscured(bool hidden) {(void)hidden;}
#endif
