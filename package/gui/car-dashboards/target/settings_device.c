/* SPDX-License-Identifier: GPL-2.0-only */
#include "settings_device.h"
#include "video_plane.h"
#include <uci.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static pthread_mutex_t brightness_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned normal_brightness=128,dimmed_brightness=64;
static bool lights_on;
void hz_settings_device_init(unsigned normal,unsigned dimmed)
{
    normal_brightness=normal;dimmed_brightness=dimmed;
}
static int set_lights(bool on)
{
    pthread_mutex_lock(&brightness_lock);lights_on=on;
    int result=hz_lcd_brightness(lights_on?dimmed_brightness:normal_brightness);
    pthread_mutex_unlock(&brightness_lock);return result;
}

static void read_option(struct uci_context *context,const char *name,char *out,size_t size)
{
    char path[128];snprintf(path,sizeof(path),"%s",name);struct uci_ptr pointer={0};
    if(!uci_lookup_ptr(context,&pointer,path,true) && (pointer.flags&UCI_LOOKUP_COMPLETE) &&
       pointer.o && pointer.o->type==UCI_TYPE_STRING)
        snprintf(out,size,"%s",pointer.o->v.string);
}
static int read_integer(struct uci_context *context,const char *name,int fallback,int min,int max)
{
    char text[32]="";read_option(context,name,text,sizeof(text));
    char *end;long value=strtol(text,&end,10);
    return !*text || *end || value<min || value>max?fallback:(int)value;
}
static int save_option(const char *name,const char *value)
{
    struct uci_context *context=uci_alloc_context();if(!context)return -1;
    char path[128];snprintf(path,sizeof(path),"%s=%s",name,value);struct uci_ptr pointer={0};
    int result=uci_lookup_ptr(context,&pointer,path,true);
    if(!result && pointer.s)result=uci_set(context,&pointer);else result=-1;
    if(!result)result=uci_commit(context,&pointer.p,false);
    uci_free_context(context);return result;
}
static const char *const names[HZ_SETTING_COUNT]={"hz-ui.main.brightness","ab5301b.audio.volume","ab5301b.audio.mute","ab5301b.audio.speaker","hz-ui.main.dimmed_brightness","ab5301b.bluetooth.power",
        "hz-ui.main.lcd_bright","hz-ui.main.lcd_contrast","hz-ui.main.lcd_hue","hz-ui.main.lcd_saturation"};
static int save(enum hz_setting key,int value)
{
    char text[16];snprintf(text,sizeof(text),"%d",value);return save_option(names[key],text);
}
/* bt_name is the status line's last field: the name the radio itself reported. */
static void reported_name(const char *reply,char *out,size_t size)
{
    const char *name=strstr(reply," bt_name=");*out=0;if(!name)return;
    name+=9;size_t length=strcspn(name,"\n");if(length>=size)length=size-1;
    memcpy(out,name,length);out[length]=0;
}
static int64_t milliseconds(void)
{
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (int64_t)now.tv_sec*1000+now.tv_nsec/1000000;
}
/* Single request through the companion's existing owner. A deadline bounds
 * connect, short writes and fragmented replies together, including EINTR. */
static int wait_socket(int fd,short events,int64_t deadline)
{
    for(;;) {
        int64_t remaining=deadline-milliseconds();if(remaining<=0)return -1;
        struct pollfd poller={.fd=fd,.events=events};int result=poll(&poller,1,(int)remaining);
        if(result<0 && errno==EINTR)continue;
        return result>0 && (poller.revents&events)?0:-1;
    }
}
int hz_companion(const char *request,char *reply,size_t size)
{
    struct sockaddr_un address={.sun_family=AF_UNIX};
    snprintf(address.sun_path,sizeof(address.sun_path),"/var/run/ab5301b.sock");
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0)return -1;
    int result=-1;int64_t deadline=milliseconds()+15000;
    if(connect(fd,(struct sockaddr *)&address,sizeof(address))) {
        if(errno!=EINPROGRESS || wait_socket(fd,POLLOUT,deadline))goto done;
        int error=0;socklen_t length=sizeof(error);
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length) || error)goto done;
    }
    size_t sent=0,length=strlen(request),used=0;
    while(sent<length) {
        if(wait_socket(fd,POLLOUT,deadline))goto done;
        ssize_t count=send(fd,request+sent,length-sent,MSG_NOSIGNAL);
        if(count<0 && (errno==EINTR || errno==EAGAIN))continue;
        if(count<=0)goto done;
        sent+=count;
    }
    while(used<size-1) {
        if(wait_socket(fd,POLLIN,deadline))goto done;
        ssize_t count=read(fd,reply+used,size-1-used);
        if(count<0 && (errno==EINTR || errno==EAGAIN))continue;
        if(count<=0)goto done;
        used+=count;reply[used]=0;
        if(strchr(reply,'\n')) {result=strncmp(reply,"OK ",3)?-1:0;break;}
    }
done:
    close(fd);return result;
}
static int load(struct hz_settings_values *values)
{
    struct uci_context *context=uci_alloc_context();if(!context)return -1;
    struct uci_package *configuration=NULL;
    if(uci_load(context,"hz-ui",&configuration)) {uci_free_context(context);return -1;}
    /* The running backlight follows the startup CLI and successful ioctls;
     * a UCI edit alone does not establish what the panel is displaying. */
    pthread_mutex_lock(&brightness_lock);
    values->brightness=normal_brightness;values->dimmed_brightness=dimmed_brightness;
    pthread_mutex_unlock(&brightness_lock);
    /* Picture: the init script applied UCI at boot, the HMI on every change. */
    for(int i=0;i<4;i++)values->picture[i]=read_integer(context,names[HZ_LCD_BRIGHT+i],50,0,100);
    values->volume=read_integer(context,"ab5301b.audio.volume",18,0,20);
    values->speaker=read_integer(context,"ab5301b.audio.speaker",0,0,1);
    values->mute=read_integer(context,"ab5301b.audio.mute",0,0,1);
    values->bluetooth=read_integer(context,"ab5301b.bluetooth.power",0,0,1);
    values->fuel_litres=read_integer(context,"vehicle.main.fuel_capacity_litres",63,1,200);
    values->speed_clock_hz=read_integer(context,"vehicle.main.speed_clock_hz",4000,2000,4000);
    read_option(context,"system.@system[0].zonename",values->timezone,sizeof(values->timezone));
    values->bluetooth_name[0]=0; /* Only the radio's report is shown, never the UCI request. */
    read_option(context,"ab5301b.audio.source",values->source,sizeof(values->source));
    uci_free_context(context);
    char reply[512],speaker[8],mute[8],source[32];int online,acknowledged,volume;
    values->audio_available=false;values->audio_acknowledged=false;
    if(!hz_companion("status\n",reply,sizeof(reply)) &&
       sscanf(reply,"OK online=%d settings_acknowledged=%d requested_speaker=%7s requested_mute=%7s requested_volume=%d requested_source=%31s",
           &online,&acknowledged,speaker,mute,&volume,source)==6 && volume>=0 && volume<=20) {
        values->audio_available=online;values->audio_acknowledged=acknowledged;
        values->volume=volume;values->speaker=!strcmp(speaker,"on");values->mute=!strcmp(mute,"on");
        snprintf(values->source,sizeof(values->source),"%s",source);
        const char *bluetooth=strstr(reply," bluetooth=");
        if(bluetooth)values->bluetooth=!strncmp(bluetooth+11,"on",2);
        reported_name(reply,values->bluetooth_name,sizeof(values->bluetooth_name));
    }
    return 0;
}
static int apply(enum hz_setting key,int value,char *message,size_t size)
{
    bool brightness=key==HZ_BRIGHTNESS || key==HZ_DIMMED_BRIGHTNESS,picture=HZ_SETTING_PICTURE(key);
    if(picture) {
        if(hz_lcd_picture(key-HZ_LCD_BRIGHT,value)) {snprintf(message,size,"Display rejected the picture change.");return -1;}
    } else if(brightness) {
        pthread_mutex_lock(&brightness_lock);
        unsigned *preset=key==HZ_BRIGHTNESS?&normal_brightness:&dimmed_brightness,old=*preset;
        *preset=value;
        /* Always rewrite the active level: a drag may have previewed the other preset. */
        bool active=(key==HZ_DIMMED_BRIGHTNESS)==lights_on;
        int result=hz_lcd_brightness(lights_on?dimmed_brightness:normal_brightness);
        if(!active)result=0; /* the saved inactive preset is not what failed */
        if(result)*preset=old;
        pthread_mutex_unlock(&brightness_lock);
        if(result) {snprintf(message,size,"Display rejected the backlight change.");return -1;}
    } else {
        char request[48],reply[512];
        if(key==HZ_VOLUME)snprintf(request,sizeof(request),"volume %d\n",value);
        else snprintf(request,sizeof(request),"%s %s\n",key==HZ_MUTE?"mute":key==HZ_BLUETOOTH?"bluetooth":"speaker",value?"on":"off");
        if(hz_companion(request,reply,sizeof(reply))) {
            snprintf(message,size,"Companion did not acknowledge; its requested state may retry.");return -1;
        }
    }
    if(save(key,value)) {snprintf(message,size,"Applied, but could not save. This change may be lost on restart.");return 1;}
    snprintf(message,size,picture?"Picture setting saved.":brightness?"Backlight preset saved; vehicle lights select the active level.":
        key==HZ_BLUETOOTH?"Connection setting sent and saved.":"Audio request acknowledged and saved.");return 0;
}
static int rename_radio(const char *name,char *message,size_t size)
{
    char request[48],reply[512],reported[64];
    snprintf(request,sizeof(request),"name %s\n",name);
    if(hz_companion(request,reply,sizeof(reply))) {snprintf(message,size,"Companion rejected the Bluetooth name.");return -1;}
    int saved=save_option("ab5301b.bluetooth.local_name",name);
    /* The radio echoes MM<name>; give the service ~1 s to receive it. */
    for(int attempt=0;attempt<10;attempt++) {
        if(!hz_companion("status\n",reply,sizeof(reply))) {
            reported_name(reply,reported,sizeof(reported));
            if(!strcmp(reported,name)) {
                snprintf(message,size,saved?"Radio reports the new name, but it could not be saved.":"Radio reports the new Bluetooth name.");
                return saved?1:0;
            }
        }
        nanosleep(&(struct timespec){.tv_nsec=100000000},NULL);
    }
    snprintf(message,size,"Name sent; the radio has not confirmed it yet.");return 1;
}
static int preview(enum hz_setting key,int value)
{
    if(HZ_SETTING_PICTURE(key))return hz_lcd_picture(key-HZ_LCD_BRIGHT,value);
    pthread_mutex_lock(&brightness_lock);int result=hz_lcd_brightness(value);pthread_mutex_unlock(&brightness_lock);
    return result;
}
const struct hz_settings_driver hz_settings_device={.load=load,.apply=apply,.lights=set_lights,.preview=preview,.rename=rename_radio};
/* Android Auto (HMI tiles): run the receiver for one session. It exits on
 * session end or a held top-left corner; the HMI is blocked meanwhile. */
/* Wireless only: AB5301B bridge and connections on (saved, as the Settings
 * toggle does). The AP itself is started and stopped by android-auto-wireless. */
#define AA_BRIDGE "/var/run/ab5301b.sock.bt"
/* system() is disabled in this binary (preview_guard.c returns 0 without
 * running anything), so commands are spawned directly. */
static int run(char *const argv[])
{
    extern char **environ;
    pid_t pid;int status=0;
    if(posix_spawn(&pid,argv[0],NULL,NULL,argv,environ))return -1;
    while(waitpid(pid,&status,0)<0 && errno==EINTR) {}
    return WIFEXITED(status) && !WEXITSTATUS(status)?0:-1;
}
static int wireless_ready(char *message,size_t size)
{
    struct uci_context *context=uci_alloc_context();if(!context) {snprintf(message,size,"Cannot read settings.");return -1;}
    int power=read_integer(context,"ab5301b.bluetooth.power",0,0,1);
    uci_free_context(context);
    char reply[512];
    /* The running service decides, not UCI: the bridge socket exists only when
     * the service was started with the bridge enabled. */
    if(access(AA_BRIDGE,F_OK)) {
        char *restart[]={"/etc/init.d/ab5301b","restart",NULL};
        if(save_option("ab5301b.bluetooth.enabled","1") || save_option("ab5301b.bluetooth.power","1") ||
           run(restart)) {snprintf(message,size,"Could not enable the Bluetooth bridge.");return -1;}
        for(int i=0;i<50 && access(AA_BRIDGE,F_OK);i++)nanosleep(&(struct timespec){.tv_nsec=100000000},NULL);
        if(access(AA_BRIDGE,F_OK)) {snprintf(message,size,"The Bluetooth bridge did not start.");return -1;}
    } else if(!power && (hz_companion("bluetooth on\n",reply,sizeof(reply)) || save_option("ab5301b.bluetooth.power","1"))) {
        snprintf(message,size,"Could not allow Bluetooth connections.");return -1;
    }
    return 0;
}
int hz_android_auto_launch(bool usb,char *message,size_t size)
{
    if(access("/usr/bin/cedar-headunit",X_OK)) {snprintf(message,size,"Android Auto receiver is not installed.");return -1;}
    if(!usb && wireless_ready(message,size))return -1;
    extern char **environ;
    /* Wireless: the helper brings the AP up, runs the receiver, then takes it down. */
    char *wireless[]={"android-auto-wireless","--once",NULL};
    char *wired[]={"cedar-headunit","--once",NULL}; /* USB is the receiver default */
    pid_t pid;int status=0,error=posix_spawn(&pid,usb?"/usr/bin/cedar-headunit":"/usr/bin/android-auto-wireless",NULL,NULL,usb?wired:wireless,environ);
    if(error) {snprintf(message,size,"Android Auto did not start: %s",strerror(error));return -1;}
    while(waitpid(pid,&status,0)<0 && errno==EINTR) {}
    if(!WIFEXITED(status) || WEXITSTATUS(status)) {snprintf(message,size,"Android Auto stopped with an error (see logread).");return -1;}
    return 0;
}
