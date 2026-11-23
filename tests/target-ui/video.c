/* SPDX-License-Identifier: GPL-2.0-only */
/* Mock the Cedar device boundary while compiling against its actual public API. */
#include "video_player.h"
#include <tplayer.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef HZ_VIDEO_DYNAMIC
#include <dlfcn.h>
#include "cedar_symbols.h"
static atomic_int loads,resolves;
static atomic_bool fail_load,slow_load,loading;
void *__wrap_dlopen(const char *path,int flags) {
    assert(!strcmp(path,"libtplayer.so") && flags==(RTLD_NOW|RTLD_GLOBAL));loads++;
    if(atomic_exchange(&fail_load,false))return NULL;
    loading=true;while(slow_load)usleep(1000);loading=false;return (void *)1;
}
void *__wrap_dlsym(void *library,const char *name) {
    assert(library==(void *)1);resolves++;
#define HZ_SYMBOL(field,symbol) if(!strcmp(name,#symbol))return (void *)symbol;
    HZ_CEDAR_API(HZ_SYMBOL)
#undef HZ_SYMBOL
    abort();
}
#endif
static atomic_int creates,destroys,starts,pauses,seeks,music_stops,visible,preparing;
static atomic_bool music_idle=true,fail_create,fail_prepare,slow_prepare;
static pthread_mutex_t callbacks=PTHREAD_MUTEX_INITIALIZER;
static TPlayerNotifyCallback notify;
static void *notify_arg;
static int position_ms;
static TPlayer *active;
void hz_music_stop(void) {music_stops++;}
bool hz_music_quiescent(void) {return music_idle;}
TPlayer *TPlayerCreate(TplayerType type) {
    assert(type==CEDARX_PLAYER && music_idle);creates++;
    if(atomic_exchange(&fail_create,false))return NULL;
    pthread_mutex_lock(&callbacks);assert(!active);active=calloc(1,sizeof(*active));
    pthread_mutex_unlock(&callbacks);return active;
}
void TPlayerDestroy(TPlayer *player) {
    /* Exercise noninstantaneous teardown: idle must stay false until it finishes. */
    usleep(60000);pthread_mutex_lock(&callbacks);assert(active==player);
    free(player);active=NULL;notify=NULL;destroys++;pthread_mutex_unlock(&callbacks);
}
int TPlayerSetNotifyCallback(TPlayer *player,TPlayerNotifyCallback callback,void *arg) {
    assert(player==active);pthread_mutex_lock(&callbacks);notify=callback;notify_arg=arg;
    pthread_mutex_unlock(&callbacks);return 0;
}
int TPlayerSetHoldLastPicture(TPlayer *player,int hold) {(void)player;assert(!hold);return 0;}
int TPlayerSetDataSource(TPlayer *player,const char *path,const CdxKeyedVectorT *headers) {
    (void)player;assert(!strcmp(path,HZ_VIDEO_TRACK) && !headers);return 0;
}
int TPlayerPrepare(TPlayer *player) {
    (void)player;preparing++;
    while(slow_prepare)usleep(1000);
    position_ms=0;return atomic_exchange(&fail_prepare,false)?-1:0;
}
void TPlayerSetDisplayRect(TPlayer *player,int x,int y,unsigned width,unsigned height) {
    (void)player;assert(x==112 && y==78 && width==800 && height==450);
}
void TPlayerSetVideoDisplay(TPlayer *player,bool enabled) {(void)player;visible=enabled;}
int TPlayerGetDuration(TPlayer *player,int *duration) {(void)player;*duration=596461;return 0;}
int TPlayerStart(TPlayer *player) {(void)player;starts++;return 0;}
int TPlayerPause(TPlayer *player) {(void)player;pauses++;return 0;}
int TPlayerStop(TPlayer *player) {(void)player;return 0;}
int TPlayerSeekTo(TPlayer *player,int milliseconds) {(void)player;position_ms=milliseconds;seeks++;return 0;}
int TPlayerGetCurrentPosition(TPlayer *player,int *milliseconds) {(void)player;*milliseconds=position_ms;return 0;}
static void message(int code) {
    pthread_mutex_lock(&callbacks);assert(notify);notify(notify_arg,code,0,NULL);pthread_mutex_unlock(&callbacks);
}
static void state(enum hz_video_status expected,bool idle) {
    for(unsigned i=0;i<2000;i++) {
        const struct hz_video_state *s=hz_video_poll();
        if(s->status==expected && s->idle==idle)return;
        usleep(1000);
    }
    fprintf(stderr,"expected state %d idle %d, got %d idle %d\n",expected,idle,hz_video_poll()->status,hz_video_poll()->idle);abort();
}
static void counter(atomic_int *value,int expected) {
    for(unsigned i=0;i<2000 && atomic_load(value)<expected;i++)usleep(1000);
    assert(atomic_load(value)>=expected);
}
int main(void) {
    assert(hz_video_poll()->idle);
#ifdef HZ_VIDEO_DYNAMIC
    hz_video_obscured(false);hz_video_pause();hz_video_stop();assert(!loads && !creates);
    fail_load=true;hz_video_play();state(HZ_VIDEO_ERROR,true);assert(loads==1 && !creates);
    slow_load=true;hz_video_play();
    while(!loading)usleep(1000);
    hz_video_stop();usleep(50000);assert(!hz_video_poll()->idle && !creates);
    slow_load=false;state(HZ_VIDEO_STOPPED,true);assert(!creates && loads==2 && resolves==14);
    music_stops=0;
#endif
    music_idle=false;hz_video_play();usleep(80000);assert(!creates);
    hz_video_stop();state(HZ_VIDEO_STOPPED,true);assert(!creates);
    music_idle=true;hz_video_play();state(HZ_VIDEO_PLAYING,false);
    assert(visible && hz_video_poll()->duration==596 && music_stops==2);
    hz_video_pause();state(HZ_VIDEO_PAUSED,false);assert(pauses==1);
    hz_video_seek(90);counter(&seeks,1);usleep(60000);assert(hz_video_poll()->position==90);
    hz_video_obscured(true);usleep(80000);assert(!visible);
    hz_video_obscured(false);usleep(80000);assert(visible);
    hz_video_play();state(HZ_VIDEO_PLAYING,false);assert(creates==1 && starts==2);
    hz_video_stop();assert(!hz_video_poll()->idle);state(HZ_VIDEO_STOPPED,true);assert(destroys==1 && !visible);
    fail_prepare=true;hz_video_play();state(HZ_VIDEO_ERROR,true);assert(destroys==2);
    fail_create=true;hz_video_play();state(HZ_VIDEO_ERROR,true);
    hz_video_play();state(HZ_VIDEO_PLAYING,false);
    message(TPLAYER_NOTIFY_MEDIA_ERROR);state(HZ_VIDEO_ERROR,true);assert(destroys==3);
    hz_video_play();state(HZ_VIDEO_PLAYING,false);
    message(TPLAYER_NOTIFY_PLAYBACK_COMPLETE);state(HZ_VIDEO_STOPPED,true);assert(destroys==4);
    slow_prepare=true;int preparations=preparing;int start_count=starts;
    hz_video_play();counter(&preparing,preparations+1);
    hz_video_stop();usleep(50000);assert(!hz_video_poll()->idle);
    slow_prepare=false;state(HZ_VIDEO_STOPPED,true);assert(starts==start_count);
    /* Rapid stop/restart replaces the old instance without exposing idle. */
    hz_video_play();state(HZ_VIDEO_PLAYING,false);int opened=creates;
    hz_video_stop();hz_video_play();state(HZ_VIDEO_PLAYING,false);assert(creates==opened+1);
    hz_video_stop();state(HZ_VIDEO_STOPPED,true);
    assert(creates==destroys+1); /* one deliberate Create failure */
#ifdef HZ_VIDEO_DYNAMIC
    assert(loads==2 && resolves==14);
    puts("PASS: Cedar dlopen occurs only on Play; failed loads recover and stop during plugin loading creates no player");
#endif
    puts("PASS: native Cedar worker API, music handoff, play/pause/resume/seek, overlay visibility, EOF, errors, preparation cancellation and teardown before idle");
    return 0;
}
