/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise the actual native monitor callback, including its first-frame gate. */
#define main hz_native_main
#include "main.c"
#undef main
#include <assert.h>
static const char *ready_path;
static unsigned ready_writes;
static unsigned cleanup_step;
void hz_display_close(void) {assert(!cleanup_step && instance_lock==42);cleanup_step=1;}
int __wrap_unlink(const char *path) {
    assert(!strcmp(path,"/var/run/hz-ui.ready") && cleanup_step==1);cleanup_step=2;return 0;
}
int __wrap_close(int fd) {assert(fd==42 && cleanup_step==2);cleanup_step=3;return 0;}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *path,const char *mode) {
    assert(!strcmp(path,"/var/run/hz-ui.ready") && !strcmp(mode,"w"));
    ready_writes++;return __real_fopen(ready_path,mode);
}
lv_disp_t *_lv_refr_get_disp_refreshing(void) {return NULL;}
int main(int argc,char **argv) {
    assert(argc==2);ready_path=argv[1];assert(!unsetenv("HZ_UI_PROFILE"));
    assert(!ready_writes);frame_profile(NULL,40,1024*600);assert(ready_writes==1);
    FILE *file=__real_fopen(ready_path,"r");assert(file);
    unsigned long long uptime=0;assert(fscanf(file,"%llu",&uptime)==1 && uptime>0);fclose(file);
    frame_profile(NULL,5,1000);assert(ready_writes==1);
    assert(!setenv("HZ_UI_PROFILE","1",1));frame_profile(NULL,7,12000);assert(ready_writes==1);
    stop(SIGTERM);assert(quit);instance_lock=42;close_ui();
    assert(cleanup_step==3 && instance_lock==-1);
    puts("PASS: native first-frame readiness is timestamped once after rendering, independent of profiling");
    puts("PASS: SIGTERM requests graceful exit; display releases before readiness and instance lock");
    return 0;
}
