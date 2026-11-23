/* SPDX-License-Identifier: GPL-2.0-only */
/* Exercise production IPC and real UCI parsing/commits against isolated files. */
#include "settings_device.h"
#include <uci.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static const char *config_directory,*delta_directory;
static int volume=18,mute,speaker,bluetooth,backlight=128,mode,fail_save,fail_display,radio_deaf;
static char radio_name[32]="D-B500"; /* Name the fake radio reports, as stock left it. */
static char request[64],reply[512];
static size_t sent,received;
struct uci_context *__real_uci_alloc_context(void);
struct uci_context *__wrap_uci_alloc_context(void)
{
    struct uci_context *context=__real_uci_alloc_context();assert(context);
    assert(!uci_set_confdir(context,config_directory));assert(!uci_set_savedir(context,delta_directory));return context;
}
int __real_uci_commit(struct uci_context *,struct uci_package **,bool);
int __wrap_uci_commit(struct uci_context *context,struct uci_package **package,bool overwrite)
{
    return fail_save?UCI_ERR_IO:__real_uci_commit(context,package,overwrite);
}
int hz_lcd_brightness(unsigned value)
{
    assert(value<=255);if(fail_display)return -1;backlight=value;return 0;
}
static unsigned picture[4]={50,50,50,50};
int hz_lcd_picture(unsigned index,unsigned value)
{
    assert(index<4 && value<=100);if(fail_display)return -1;picture[index]=value;return 0;
}
int __wrap_socket(int family,int type,int protocol)
{
    assert(family==AF_UNIX && (type&SOCK_NONBLOCK) && !protocol);sent=received=0;request[0]=0;return 123;
}
int __wrap_connect(int fd,const struct sockaddr *address,socklen_t size)
{
    assert(fd==123 && size==sizeof(struct sockaddr_un));
    assert(!strcmp(((const struct sockaddr_un *)address)->sun_path,"/var/run/ab5301b.sock"));
    if(mode==1) {errno=ENOENT;return -1;}
    errno=EINPROGRESS;return -1;
}
int __wrap_getsockopt(int fd,int level,int option,void *value,socklen_t *size)
{
    assert(fd==123 && level==SOL_SOCKET && option==SO_ERROR && *size==sizeof(int));*(int *)value=0;return 0;
}
int __wrap_poll(struct pollfd *poller,nfds_t count,int timeout)
{
    assert(count==1 && poller->fd==123 && timeout>0 && timeout<=15000);
    if(mode==2)return 0;
    poller->revents=poller->events;return 1;
}
ssize_t __wrap_send(int fd,const void *buffer,size_t size,int flags)
{
    assert(fd==123 && (flags&MSG_NOSIGNAL));if(size>2)size=2;
    assert(sent+size<sizeof(request));memcpy(request+sent,buffer,size);sent+=size;request[sent]=0;
    if(strchr(request,'\n')) {
        if(!strcmp(request,"status\n"))snprintf(reply,sizeof(reply),
            "OK online=1 settings_acknowledged=1 requested_speaker=%s requested_mute=%s requested_volume=%d requested_source=sd version=test physical_output=unverified bluetooth=%s bt_name=%s\n",
            speaker?"on":"off",mute?"on":"off",volume,bluetooth?"on":"off",radio_name);
        else {
            if(sscanf(request,"volume %d",&volume)!=1) {
                if(!strncmp(request,"mute ",5))mute=!strcmp(request,"mute on\n");
                else if(!strncmp(request,"speaker ",8))speaker=!strcmp(request,"speaker on\n");
                else if(!strncmp(request,"bluetooth ",10))bluetooth=!strcmp(request,"bluetooth on\n");
                else if(!strncmp(request,"name ",5)) {
                    if(!radio_deaf)snprintf(radio_name,sizeof(radio_name),"%.*s",(int)strcspn(request+5,"\n"),request+5);
                } else assert(0);
            }
            snprintf(reply,sizeof(reply),mode==3?"ERR no acknowledgement\n":"OK acknowledged\n");
        }
    }
    return size;
}
ssize_t __wrap_read(int fd,void *buffer,size_t size)
{
    assert(fd==123);size_t remaining=strlen(reply)-received;if(size>3)size=3;if(size>remaining)size=remaining;
    memcpy(buffer,reply+received,size);received+=size;return size;
}
int __wrap_close(int fd) {assert(fd==123);return 0;}
static void option(const char *name,const char *expected)
{
    struct uci_context *context=__wrap_uci_alloc_context();struct uci_ptr pointer={0};char path[128];
    snprintf(path,sizeof(path),"%s",name);assert(!uci_lookup_ptr(context,&pointer,path,true));
    assert(pointer.o && pointer.o->type==UCI_TYPE_STRING && !strcmp(pointer.o->v.string,expected));uci_free_context(context);
}
int main(int argc,char **argv)
{
    assert(argc==3);config_directory=argv[1];delta_directory=argv[2];hz_settings_device_init(128,64);
    struct hz_settings_values values={.brightness=128};char message[160];
    assert(!hz_settings_device.load(&values));assert(values.audio_available && values.audio_acknowledged);
    assert(values.volume==18 && !values.mute && !values.speaker && values.fuel_litres==63 && values.speed_clock_hz==4000);
    /* UCI requests SWL-BT, but Settings shows what the radio reports. */
    assert(!strcmp(values.timezone,"Europe/Berlin") && !strcmp(values.bluetooth_name,"D-B500"));
    assert(!hz_settings_device.rename("HZ-B500",message,sizeof(message)) && strstr(message,"reports the new"));
    option("ab5301b.bluetooth.local_name","HZ-B500");
    assert(!hz_settings_device.load(&values) && !strcmp(values.bluetooth_name,"HZ-B500"));
    radio_deaf=1;assert(hz_settings_device.rename("OTHER",message,sizeof(message))==1 && strstr(message,"not confirmed"));
    option("ab5301b.bluetooth.local_name","OTHER");radio_deaf=0;
    assert(!hz_settings_device.load(&values) && !strcmp(values.bluetooth_name,"HZ-B500"));
    assert(!hz_settings_device.apply(HZ_BRIGHTNESS,64,message,sizeof(message)) && backlight==64);
    option("hz-ui.main.brightness","64");option("hz-ui.main.touch_device","auto");
    assert(!hz_settings_device.apply(HZ_VOLUME,9,message,sizeof(message)));option("ab5301b.audio.volume","9");
    assert(!hz_settings_device.apply(HZ_MUTE,1,message,sizeof(message)));option("ab5301b.audio.mute","1");
    assert(!hz_settings_device.apply(HZ_SPEAKER,1,message,sizeof(message)));option("ab5301b.audio.speaker","1");
    option("ab5301b.audio.aa_template","0000070700000a011200");option("ab5301b.audio.source","sd");
    assert(!hz_settings_device.load(&values) && values.volume==9 && values.mute && values.speaker);
    /* Bluetooth power defaults off from the packaged config, then persists the UI choice. */
    assert(!values.bluetooth);option("ab5301b.bluetooth.power","0");
    assert(!hz_settings_device.apply(HZ_BLUETOOTH,1,message,sizeof(message)) && bluetooth);
    option("ab5301b.bluetooth.power","1");option("ab5301b.bluetooth.enabled","1"); /* packaged default; the toggle leaves it alone */
    assert(!hz_settings_device.load(&values) && values.bluetooth);
    assert(values.dimmed_brightness==64);
    assert(!hz_settings_device.apply(HZ_DIMMED_BRIGHTNESS,32,message,sizeof(message)) && backlight==64);
    option("hz-ui.main.dimmed_brightness","32");
    assert(!hz_settings_device.lights(true) && backlight==32);
    assert(!hz_settings_device.apply(HZ_BRIGHTNESS,90,message,sizeof(message)) && backlight==32);
    assert(!hz_settings_device.apply(HZ_DIMMED_BRIGHTNESS,48,message,sizeof(message)) && backlight==48);
    assert(!hz_settings_device.lights(false) && backlight==90);
    assert(!hz_settings_device.apply(HZ_BRIGHTNESS,64,message,sizeof(message)) && backlight==64);
    /* Picture: packaged defaults are neutral; each change reaches the display and UCI. */
    assert(!hz_settings_device.load(&values));
    for(int i=0;i<4;i++)assert(values.picture[i]==50);
    assert(!hz_settings_device.apply(HZ_LCD_BRIGHT,40,message,sizeof(message)) && picture[0]==40);
    assert(!hz_settings_device.apply(HZ_LCD_HUE,55,message,sizeof(message)) && picture[2]==55);
    option("hz-ui.main.lcd_bright","40");option("hz-ui.main.lcd_hue","55");
    assert(!hz_settings_device.load(&values) && values.picture[0]==40 && values.picture[1]==50 && values.picture[2]==55);
    /* Preview reaches the display but is not saved. */
    assert(!hz_settings_device.preview(HZ_LCD_SATURATION,30) && picture[3]==30);
    option("hz-ui.main.lcd_saturation","50");
    /* A dragged dimmed preset is shown by day, unsaved; the lights hook restores the active level. */
    assert(!hz_settings_device.preview(HZ_DIMMED_BRIGHTNESS,200) && backlight==200);
    option("hz-ui.main.dimmed_brightness","48");
    assert(!hz_settings_device.lights(false) && backlight==64);
    fail_display=1;assert(hz_settings_device.apply(HZ_LCD_CONTRAST,70,message,sizeof(message))<0 && picture[1]==50);
    option("hz-ui.main.lcd_contrast","50");fail_display=0;
    puts("PASS: picture brightness/contrast/hue/saturation apply to the display and persist; rejection is not saved");
    puts("PASS: vehicle lights select the dimmed preset; editing an inactive preset preserves active brightness and both presets persist");
    puts("PASS: native settings handle fragmented IPC, preserve companion ownership and unrelated UCI fields, and persist brightness/volume/mute/speaker");
    fail_display=1;assert(hz_settings_device.apply(HZ_BRIGHTNESS,90,message,sizeof(message))<0 && backlight==64);
    option("hz-ui.main.brightness","64");fail_display=0;
    fail_save=1;assert(hz_settings_device.apply(HZ_BRIGHTNESS,90,message,sizeof(message))==1 && backlight==90);
    assert(strstr(message,"could not save"));option("hz-ui.main.brightness","64");fail_save=0;
    mode=3;assert(hz_settings_device.apply(HZ_VOLUME,12,message,sizeof(message))<0);
    option("ab5301b.audio.volume","9");assert(!hz_settings_device.load(&values) && values.volume==12);
    mode=1;assert(!hz_settings_device.load(&values) && !values.audio_available);
    mode=2;assert(!hz_settings_device.load(&values) && !values.audio_available);
    puts("PASS: display rejection, failed persistence, retained unacknowledged audio requests, missing service and transport timeout remain distinct");
    return 0;
}
