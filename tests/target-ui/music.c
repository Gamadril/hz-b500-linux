/* SPDX-License-Identifier: GPL-2.0-only */
#include "music_player.h"
#include <out123.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static atomic_int fail_output;
static atomic_uint output_calls;
int __real_out123_open(out123_handle *,const char *,const char *);
size_t __real_out123_play(out123_handle *,void *,size_t);
int __wrap_out123_open(out123_handle *output,const char *driver,const char *device) {
    assert(!strcmp(driver,"alsa") && !strcmp(device,"hw:audiocodec,0"));
    if(atomic_exchange(&fail_output,0)) return __real_out123_open(output,driver,"hz-ui-test-missing-device");
    return __real_out123_open(output,driver,"null");
}
size_t __wrap_out123_play(out123_handle *output,void *data,size_t bytes) {
    assert(bytes>0 && bytes<=4096);
    atomic_fetch_add(&output_calls,1);
    usleep(10000); /* Pace ALSA's null sink so pause and seek can be exercised. */
    return __real_out123_play(output,data,bytes);
}
static const struct hz_music_state *wait_status(enum hz_music_status status) {
    for(unsigned i=0;i<100;i++) {
        const struct hz_music_state *state=hz_music_poll();
        if(state->status==status) return state;
        usleep(10000);
    }
    fprintf(stderr,"Expected music status %d, got %d: %s\n",status,hz_music_poll()->status,hz_music_poll()->message);
    assert(0);return NULL;
}
int main(void) {
    assert(hz_music_poll()->status==HZ_MUSIC_STOPPED);
    hz_music_play();wait_status(HZ_MUSIC_PLAYING);
    hz_music_pause();
    unsigned position=wait_status(HZ_MUSIC_PAUSED)->position;
    unsigned played=atomic_load(&output_calls);
    usleep(100000);
    assert(hz_music_poll()->position==position && atomic_load(&output_calls)==played);
    hz_music_play();wait_status(HZ_MUSIC_PLAYING);
    hz_music_seek(42);
    hz_music_pause();
    assert(wait_status(HZ_MUSIC_PAUSED)->position>=42 && hz_music_poll()->position<45);
    hz_music_seek(64);usleep(100000);
    assert(hz_music_poll()->status==HZ_MUSIC_PAUSED && hz_music_poll()->position==64);
    hz_music_play();
    assert(wait_status(HZ_MUSIC_PLAYING)->position>=64 && hz_music_poll()->position<67);
    hz_music_stop();assert(wait_status(HZ_MUSIC_STOPPED)->position==0);
    atomic_store(&fail_output,1);
    hz_music_play();assert(wait_status(HZ_MUSIC_ERROR)->message[0]);
    hz_music_play();wait_status(HZ_MUSIC_PLAYING);
    hz_music_seek(129);wait_status(HZ_MUSIC_STOPPED);
    assert(hz_music_poll()->position==0);
    hz_music_play();wait_status(HZ_MUSIC_PLAYING);
    hz_music_stop();
    assert(atomic_load(&output_calls)>0);
    puts("PASS: HMI libmpg123/libout123 decode original MP3, play/pause/resume/seek/stop/EOF and output-error recovery (ALSA null sink)");
}
