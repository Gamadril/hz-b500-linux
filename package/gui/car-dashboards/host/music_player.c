/* SPDX-License-Identifier: GPL-2.0-only */
#include "music_player.h"
#include <stdio.h>
#include <string.h>

#ifdef HZ_AUDIO_PLAYBACK
#include <mpg123.h>
#include <out123.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#ifndef HZ_MUSIC_INPUT
#define HZ_MUSIC_INPUT HZ_MUSIC_TRACK
#endif
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static bool started,quitting,seek_pending,audio_active;
static unsigned seek_seconds,generation;
static enum hz_music_status desired=HZ_MUSIC_STOPPED;
static struct hz_music_state latest;

static void release_audio(mpg123_handle **decoder,out123_handle **output) {
    if(*output) {out123_drop(*output);out123_del(*output);*output=NULL;}
    if(*decoder) {mpg123_delete(*decoder);*decoder=NULL;}
    pthread_mutex_lock(&lock);audio_active=false;pthread_mutex_unlock(&lock);
}
static void failed(unsigned current,const char *message) {
    pthread_mutex_lock(&lock);
    if(generation==current) {
        latest.status=HZ_MUSIC_ERROR;desired=HZ_MUSIC_STOPPED;
        snprintf(latest.message,sizeof(latest.message),"%s",message);
        fprintf(stderr,"Music: %s\n",latest.message);
    }
    pthread_mutex_unlock(&lock);
}
static void *playback(void *unused) {
    (void)unused;
    mpg123_handle *decoder=NULL;out123_handle *output=NULL;
    unsigned current=0;
    long rate=0;
    bool paused=false;
    unsigned char pcm[4096];
    for(;;) {
        pthread_mutex_lock(&lock);
        bool quit=quitting;
        enum hz_music_status want=desired;
        unsigned next=generation;
        bool seek=seek_pending;
        unsigned position=seek_seconds;
        pthread_mutex_unlock(&lock);
        if(quit) break;
        if(want==HZ_MUSIC_STOPPED || (decoder && next!=current)) {
            release_audio(&decoder,&output);paused=false;
            pthread_mutex_lock(&lock);
            while(!quitting && desired==HZ_MUSIC_STOPPED) pthread_cond_wait(&changed,&lock);
            pthread_mutex_unlock(&lock);
            continue;
        }
        pthread_mutex_lock(&lock);
        if(generation==next && seek_seconds==position) seek_pending=false;
        pthread_mutex_unlock(&lock);
        if(!decoder) {
            pthread_mutex_lock(&lock);
            if(generation!=next || desired==HZ_MUSIC_STOPPED) {pthread_mutex_unlock(&lock);continue;}
            audio_active=true;
            pthread_mutex_unlock(&lock);
            current=next;
            int error,channels,encoding;
            decoder=mpg123_new(NULL,&error);output=out123_new();
            if(!decoder || !output) {
                failed(current,"Cannot initialize MP3 playback");
                release_audio(&decoder,&output);continue;
            }
            if(mpg123_open_fixed(decoder,HZ_MUSIC_INPUT,MPG123_STEREO,MPG123_ENC_SIGNED_16)!=MPG123_OK ||
               mpg123_getformat(decoder,&rate,&channels,&encoding)!=MPG123_OK) {
                failed(current,mpg123_strerror(decoder));release_audio(&decoder,&output);continue;
            }
            out123_param_float(output,OUT123_DEVICEBUFFER,0.1);
            if(out123_open(output,"alsa","hw:audiocodec,0")!=OUT123_OK ||
               out123_start(output,rate,channels,encoding)!=OUT123_OK) {
                failed(current,out123_strerror(output));release_audio(&decoder,&output);continue;
            }
        }
        if(seek) {
            out123_drop(output);
            if(mpg123_seek(decoder,(off_t)position*rate,SEEK_SET)<0) {
                failed(current,mpg123_strerror(decoder));release_audio(&decoder,&output);continue;
            }
            pthread_mutex_lock(&lock);
            if(generation==current) latest.position=position;
            pthread_mutex_unlock(&lock);
        }
        if(want==HZ_MUSIC_PAUSED) {
            if(!paused) {out123_pause(output);paused=true;}
            pthread_mutex_lock(&lock);
            if(generation==current) latest.status=HZ_MUSIC_PAUSED;
            while(!quitting && generation==current && desired==HZ_MUSIC_PAUSED && !seek_pending)
                pthread_cond_wait(&changed,&lock);
            pthread_mutex_unlock(&lock);
            continue;
        }
        if(paused) {out123_continue(output);paused=false;}
        size_t bytes=0;
        int result=mpg123_read(decoder,pcm,sizeof(pcm),&bytes);
        if(bytes && out123_play(output,pcm,bytes)!=bytes) {
            failed(current,out123_strerror(output));release_audio(&decoder,&output);continue;
        }
        if(result!=MPG123_OK && result!=MPG123_NEW_FORMAT && result!=MPG123_DONE) {
            failed(current,mpg123_strerror(decoder));release_audio(&decoder,&output);continue;
        }
        off_t samples=mpg123_tell(decoder);
        pthread_mutex_lock(&lock);
        if(generation==current && desired==HZ_MUSIC_PLAYING) {
            latest.status=HZ_MUSIC_PLAYING;
            if(samples>=0) latest.position=(unsigned)(samples/rate);
        }
        pthread_mutex_unlock(&lock);
        if(result==MPG123_DONE) {
            out123_drain(output);
            pthread_mutex_lock(&lock);
            if(generation==current && !seek_pending) {desired=HZ_MUSIC_STOPPED;latest.status=HZ_MUSIC_STOPPED;latest.position=0;}
            pthread_mutex_unlock(&lock);
            release_audio(&decoder,&output);paused=false;
        }
    }
    release_audio(&decoder,&output);
    return NULL;
}
static void shutdown_player(void) {
    pthread_mutex_lock(&lock);quitting=true;pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
    pthread_join(thread,NULL);
}
const struct hz_music_state *hz_music_poll(void) {
    static struct hz_music_state copy;
    pthread_mutex_lock(&lock);copy=latest;pthread_mutex_unlock(&lock);
    return &copy;
}
bool hz_music_quiescent(void) {
    pthread_mutex_lock(&lock);
    bool idle=desired==HZ_MUSIC_STOPPED && !audio_active;
    pthread_mutex_unlock(&lock);return idle;
}
void hz_music_play(void) {
    pthread_mutex_lock(&lock);
    if(!started) {
        if(pthread_create(&thread,NULL,playback,NULL)) {
            latest.status=HZ_MUSIC_ERROR;
            snprintf(latest.message,sizeof(latest.message),"Cannot start audio worker");
            pthread_mutex_unlock(&lock);return;
        }
        started=true;atexit(shutdown_player);
    }
    if(desired==HZ_MUSIC_STOPPED) {generation++;seek_pending=true;seek_seconds=latest.position;}
    desired=HZ_MUSIC_PLAYING;latest.status=HZ_MUSIC_OPENING;latest.message[0]=0;
    pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
}
void hz_music_pause(void) {
    pthread_mutex_lock(&lock);
    if(desired==HZ_MUSIC_PLAYING) desired=HZ_MUSIC_PAUSED;
    pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
}
void hz_music_stop(void) {
    pthread_mutex_lock(&lock);
    generation++;desired=HZ_MUSIC_STOPPED;seek_pending=false;
    latest=(struct hz_music_state){0};
    pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
}
void hz_music_seek(unsigned seconds) {
    if(seconds>HZ_MUSIC_DURATION) seconds=HZ_MUSIC_DURATION;
    pthread_mutex_lock(&lock);
    seek_seconds=seconds;seek_pending=true;latest.position=seconds;
    pthread_cond_signal(&changed);pthread_mutex_unlock(&lock);
}
#else
static struct hz_music_state state;
bool hz_music_quiescent(void) {return true;}
const struct hz_music_state *hz_music_poll(void) {return &state;}
void hz_music_play(void) {
    state.status=HZ_MUSIC_ERROR;
    snprintf(state.message,sizeof(state.message),"Playback is available on the target");
}
void hz_music_pause(void) {}
void hz_music_stop(void) {state=(struct hz_music_state){0};}
void hz_music_seek(unsigned seconds) {state.position=seconds;}
#endif
