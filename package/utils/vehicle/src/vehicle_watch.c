/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "vehicle_client.h"
#include "vehicle_blob.h"
#include <libubox/blobmsg_json.h>
#include <linux/uinput.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
static volatile sig_atomic_t stopping;
static int input_fd=-1;
static bool media;
static void stop_signal(int sig) {(void)sig;stopping=1;}
static bool stop(void *arg) {(void)arg;return stopping;}
static void print_blob(struct blob_buf *b,const char *kind) {
    char *json=blobmsg_format_json(b->head,true);
    if(json) {printf("%s %s\n",kind,json);fflush(stdout);free(json);}
}
static void state(const struct vehicle_state *s,void *arg) {
    (void)arg;if(media) return;
    struct blob_buf b={0};vehicle_state_blob(&b,s);print_blob(&b,"state");blob_buf_free(&b);
}
static int uinput_open(void) {
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0) return -1;
    int keys[]={KEY_NEXTSONG,KEY_PREVIOUSSONG,KEY_PLAY,KEY_STOP};
    if(ioctl(fd,UI_SET_EVBIT,EV_KEY)||ioctl(fd,UI_SET_EVBIT,EV_SYN)) goto fail;
    for(unsigned i=0;i<4;i++) if(ioctl(fd,UI_SET_KEYBIT,keys[i])) goto fail;
    struct uinput_user_dev u={0};snprintf(u.name,sizeof(u.name),"HZ-B500 vehicle media");u.id.bustype=BUS_VIRTUAL;
    if(write(fd,&u,sizeof(u))!=(ssize_t)sizeof(u)||ioctl(fd,UI_DEV_CREATE)) goto fail;
    return fd;
fail:close(fd);return -1;
}
static void event(const struct vehicle_event *e,void *arg) {
    (void)arg;
    if(!media) {printf("media %llu %s\n",(unsigned long long)e->sequence,vehicle_action_name(e->action));fflush(stdout);return;}
    static const int keys[]={0,KEY_NEXTSONG,KEY_PREVIOUSSONG,KEY_PLAY,KEY_STOP};
    if(e->action<V_NEXT||e->action>V_STOP) return;
    if(input_fd<0) input_fd=uinput_open();
    if(input_fd<0) {perror("vehicle-media: uinput");return;}
    struct input_event events[4]={
        {.type=EV_KEY,.code=keys[e->action],.value=1},{.type=EV_SYN,.code=SYN_REPORT},
        {.type=EV_KEY,.code=keys[e->action],.value=0},{.type=EV_SYN,.code=SYN_REPORT}
    };
    if(write(input_fd,events,sizeof(events))!=(ssize_t)sizeof(events)) {
        /* Destroying the device releases any partially written key. No replay. */
        perror("vehicle-media: write");ioctl(input_fd,UI_DEV_DESTROY);close(input_fd);input_fd=-1;
    }
}
int main(int argc,char **argv) {
    const char *socket_path=NULL,*lock_path="/var/run/vehicle-media.lock";
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--media-uinput")) media=true;
        else if(!strcmp(argv[i],"--ubus-socket")&&i+1<argc) socket_path=argv[++i];
        else if(!strcmp(argv[i],"--lock")&&i+1<argc) lock_path=argv[++i];
        else {fprintf(stderr,"Usage: vehicle-watch [--ubus-socket PATH] [--media-uinput [--lock PATH]]\n");return 2;}
    }
    int lock=-1;
    if(media) {
        lock=open(lock_path,O_CREAT|O_RDWR|O_CLOEXEC,0600);
        if(lock<0||flock(lock,LOCK_EX|LOCK_NB)) {fprintf(stderr,"vehicle-media: cannot acquire sole action-owner lock\n");return 1;}
        input_fd=uinput_open();if(input_fd<0) perror("vehicle-media: waiting for uinput");
    }
    signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);signal(SIGPIPE,SIG_IGN);
    const struct vehicle_callbacks callbacks={state,event,stop};
    vehicle_client_run(socket_path,&callbacks,NULL);
    if(input_fd>=0) {ioctl(input_fd,UI_DEV_DESTROY);close(input_fd);}
    if(lock>=0) close(lock);
    return 0;
}
