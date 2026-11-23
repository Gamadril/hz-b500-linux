/* SPDX-License-Identifier: GPL-2.0-only */
/* Test the actual vendor sink functions extracted by video-sinks.sh. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "tsound_ctrl.h"
#include "auGaincom.h"
#define TLOGD(...) ((void)0)
#define TLOGW(...) ((void)0)
#define TLOGE(...) ((void)0)
#define TP_CHECK(value) assert(value)
#define AUDIO_SURROUND 0
static unsigned gain_calls,post_calls,pcm_calls,callbacks;
static void *expected_pcm;
static unsigned expected_frames;
int tina_do_AudioGain(AudioGain *gain) {(void)gain;gain_calls++;return 1;}
int do_auPostProc(PostProcessSt *process) {
    post_calls++;process->OutputPcmLen=process->InputPcmLen;return 1;
}
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm,const void *data,snd_pcm_uframes_t frames) {
    assert(pcm && data==expected_pcm && frames==expected_frames);pcm_calls++;return frames;
}
int snd_pcm_resume(snd_pcm_t *pcm) {(void)pcm;abort();}
int snd_pcm_prepare(snd_pcm_t *pcm) {(void)pcm;abort();}
static int audio_callback(void *context,void *arg) {
    SoundCtrlContext *sound=context;SoundPcmData *pcm=arg;
    assert(pcm->channels==sound->nChannelNum && pcm->samplerate==48000);
    callbacks++;return 0;
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "sound-write.inc"
#pragma GCC diagnostic pop
/* LayerDisplayOnoff uses only these private fields. */
typedef struct {
    dispOutPort *mDispOutPort;
    int bDisplayEnabled,bFrameSubmitted;
} LayerContext;
static pthread_mutex_t configMtx=PTHREAD_MUTEX_INITIALIZER;
static unsigned enable_calls;
static bool visible;
static int enable(void *port,int on) {(void)port;enable_calls++;visible=on;return 0;}
#include "layer-visible.inc"
int main(void) {
    unsigned char input[1200],output[AUDIO_POST_PROC_PCM_BUFSIZE];
    databuf input_pcm={0},output_pcm={0};
    PostProcessSt processing={.InputPcmBuf=&input_pcm,.OutputPcmBuf=&output_pcm};
    SoundCtrlContext sound={.alsa_handler=(void *)1,.bytes_per_sample=2,.nChannelNum=6,
        .nSampleRate=48000,.sound_status=STATUS_START,.mAudioframeCallback=audio_callback,
        .PostProcPar=&processing,.outputPcmBuf=(char *)output};
    sound.pUserData=&sound;memset(input,0x55,sizeof(input));
    expected_pcm=input;expected_frames=100;
    assert(TSoundDeviceWrite((SoundCtrl *)&sound,input,sizeof(input))==sizeof(input));
    assert(pcm_calls==1 && callbacks==1 && !gain_calls && !post_calls);
    for(unsigned i=0;i<sizeof(input);i++)assert(input[i]==0x55);
    sound.nChannelNum=2;expected_pcm=output;expected_frames=300;
    assert(TSoundDeviceWrite((SoundCtrl *)&sound,input,sizeof(input))==sizeof(input));
    assert(gain_calls==1 && post_calls==1 && pcm_calls==2);
    dispOutPort port={.setEnable=enable};LayerContext layer={.mDispOutPort=&port};
    /* No frame means enabling would expose uninitialized display memory. */
    LayerDisplayOnoff((LayerCtrl *)&layer,1);assert(!enable_calls && layer.bDisplayEnabled);
    layer.bFrameSubmitted=1;
    LayerDisplayOnoff((LayerCtrl *)&layer,0);assert(enable_calls==1 && !visible && !layer.bDisplayEnabled);
    /* Restore immediately even with a paused video and no newly queued frames. */
    LayerDisplayOnoff((LayerCtrl *)&layer,1);assert(enable_calls==2 && visible && layer.bDisplayEnabled);
    puts("PASS: production Cedar PCM sink preserves six-channel samples for ALSA downmix, retains stereo processing; layer hides and restores paused frames");
    return 0;
}
