/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include "vehicle_blob.h"
#include "vehicle_clock.h"
#include <libubus.h>
#include <libubox/uloop.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
static struct vehicle_decoder decoder;
static struct ubus_context *bus;
static struct blob_buf buffer;
static struct uloop_fd can_input={.fd=-1};
static struct uloop_timeout timer;
static struct ubus_object object;
static const char *interface="can0";
static bool simulated, demo=true;
static uint64_t last_publish, published_sequence, next_demo, demo_step, last_retry;
static uint32_t previous_socket_drops;
static struct vehicle_clock_sync clock_sync={.marker_path="/var/run/vehicle-time-synced"};
static void publish_event(const struct vehicle_event *e) {
    vehicle_event_blob(&buffer,&decoder.state,e);
    if(ubus_notify(bus,&object,"media",buffer.head,-1)) fprintf(stderr,"vehicled: media notification failed, sequence %llu\n",(unsigned long long)e->sequence);
}
static int snapshot(struct ubus_context *ctx, struct ubus_object *obj,
                    struct ubus_request_data *req,const char *method,struct blob_attr *msg) {
    (void)obj;(void)method;(void)msg;
    vehicle_state_blob(&buffer,&decoder.state);
    return ubus_send_reply(ctx,req,buffer.head);
}
static int hex(char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
static const struct blobmsg_policy inject_policy[]={{"id",BLOBMSG_TYPE_INT32},{"data",BLOBMSG_TYPE_STRING}};
static int inject(struct ubus_context *ctx,struct ubus_object *obj,struct ubus_request_data *req,const char *method,struct blob_attr *msg) {
    (void)ctx;(void)obj;(void)req;(void)method;
    if(!simulated) return UBUS_STATUS_PERMISSION_DENIED;
    struct blob_attr *v[2]; if(!msg) return UBUS_STATUS_INVALID_ARGUMENT;
    blobmsg_parse(inject_policy,2,v,blob_data(msg),blob_len(msg));
    if(!v[0]||!v[1]) return UBUS_STATUS_INVALID_ARGUMENT;
    const char *data=blobmsg_get_string(v[1]); size_t n=strlen(data);
    struct can_frame f={.can_id=blobmsg_get_u32(v[0])};
    if(f.can_id>CAN_SFF_MASK || n>16 || n%2) return UBUS_STATUS_INVALID_ARGUMENT;
    f.can_dlc=n/2;
    for(unsigned i=0;i<n;i+=2) {
        int a=hex(data[i]),b=hex(data[i+1]);
        if(a<0||b<0) return UBUS_STATUS_INVALID_ARGUMENT;
        f.data[i/2]=(a<<4)|b;
    }
    struct vehicle_event e;
    if(vehicle_frame(&decoder,&f,vehicle_now_ms(),&e)) publish_event(&e);
    return 0;
}
static const struct blobmsg_policy set_policy[]={{"signal",BLOBMSG_TYPE_STRING},{"value",BLOBMSG_TYPE_INT32}};
static int set_signal(struct ubus_context *ctx,struct ubus_object *obj,struct ubus_request_data *req,const char *method,struct blob_attr *msg) {
    (void)ctx;(void)obj;(void)req;(void)method;
    if(!simulated) return UBUS_STATUS_PERMISSION_DENIED;
    struct blob_attr *v[2]; if(!msg) return UBUS_STATUS_INVALID_ARGUMENT;
    blobmsg_parse(set_policy,2,v,blob_data(msg),blob_len(msg));
    if(!v[0]||!v[1]) return UBUS_STATUS_INVALID_ARGUMENT;
    int32_t value=blobmsg_get_u32(v[1]);
    for(unsigned i=0;i<VEHICLE_SIGNAL_COUNT;i++) {
        if(strcmp(blobmsg_get_string(v[0]),vehicle_signal_names[i])) continue;
        /* Calendar and later frame-derived fields (including gear) use inject. */
        if(i>=V_TIME_HOUR) return UBUS_STATUS_INVALID_ARGUMENT;
        int minimum=i==V_COOLANT?-40:0;
        int maximum=i==V_RPM?8000:i==V_SPEED?450:i==V_CRUISE_SPEED?300:
            i==V_COOLANT?150:i==V_FUEL?100:i>=V_PDC_FL&&i<=V_PDC_RL?255:
            i==V_LIGHTS?2:i==V_DOORS?31:1;
        if(value<minimum||value>maximum) return UBUS_STATUS_INVALID_ARGUMENT;
        vehicle_set(&decoder,i,value,vehicle_now_ms()); return 0;
    }
    return UBUS_STATUS_INVALID_ARGUMENT;
}
static const struct ubus_method methods[]={
    UBUS_METHOD_NOARG("snapshot",snapshot),
    UBUS_METHOD("inject",inject,inject_policy), UBUS_METHOD("set",set_signal,set_policy)
};
static struct ubus_object_type type=UBUS_OBJECT_TYPE("vehicle",methods);
static struct ubus_object object={.name=VEHICLE_OBJECT,.type=&type,.methods=methods,.n_methods=3};
static void close_can(void) {
    if(can_input.fd>=0) { uloop_fd_delete(&can_input); close(can_input.fd); can_input.fd=-1; }
    vehicle_disconnect(&decoder);
}
static void receive_can(struct uloop_fd *fd,unsigned events) {
    if(events & ULOOP_ERROR_CB) { close_can(); return; }
    for(unsigned count=0;count<64;count++) {
        struct can_frame frame;
        union {
            struct cmsghdr align;
            char data[CMSG_SPACE(sizeof(uint32_t))+CMSG_SPACE(sizeof(struct timespec))];
        } control;
        struct iovec io={&frame,sizeof(frame)};
        struct msghdr msg={.msg_iov=&io,.msg_iovlen=1,.msg_control=control.data,.msg_controllen=sizeof(control.data)};
        ssize_t n=recvmsg(fd->fd,&msg,MSG_DONTWAIT);
        if(n<0 && errno==EINTR) continue;
        if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK)) break;
        if(n!=(ssize_t)sizeof(frame)) { close_can(); break; }
        struct timespec received={0};bool timestamp=false;
        for(struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)) {
            if(c->cmsg_level==SOL_SOCKET && c->cmsg_type==SO_RXQ_OVFL && c->cmsg_len>=CMSG_LEN(sizeof(uint32_t))) {
                uint32_t drops; memcpy(&drops,CMSG_DATA(c),sizeof(drops));
                if(drops!=previous_socket_drops) {decoder.held=V_ACTION_NONE;decoder.long_sent=false;}
                decoder.state.dropped_frames+=(uint32_t)(drops-previous_socket_drops);
                previous_socket_drops=drops;
            }
            if(c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_TIMESTAMPNS && c->cmsg_len>=CMSG_LEN(sizeof(received))) {
                memcpy(&received,CMSG_DATA(c),sizeof(received));timestamp=true;
            }
        }
        uint64_t now=vehicle_now_ms(),frame_time=now;
        if(timestamp) {
            struct timespec wall;clock_gettime(CLOCK_REALTIME,&wall);
            if(!vehicle_frame_timestamp(now,&wall,&received,&frame_time)) {
                decoder.state.dropped_frames++;
                /* Do not turn a queued, expired press into a new action on recovery. */
                decoder.held=V_ACTION_NONE;decoder.long_sent=false;continue;
            }
        }
        struct vehicle_event e;
        if(vehicle_frame(&decoder,&frame,frame_time,&e)) publish_event(&e);
        if(frame.can_id==0x2f8 && frame.can_dlc==8) {
            int synchronized=vehicle_sync_system_time(&clock_sync,&decoder.state,vehicle_now_ms());
            if(synchronized<0)
                fprintf(stderr,"vehicled: system time synchronization failed: %s\n",strerror(errno));
            else if(synchronized>0)
                fprintf(stderr,"vehicled: system time synchronized from CAN (once per boot)\n");
        }
    }
}
static void open_can(void) {
    int fd=socket(PF_CAN,SOCK_RAW|SOCK_NONBLOCK|SOCK_CLOEXEC,CAN_RAW);
    if(fd<0) return;
    struct ifreq ifr={0}; snprintf(ifr.ifr_name,sizeof(ifr.ifr_name),"%s",interface);
    if(ioctl(fd,SIOCGIFINDEX,&ifr)<0) { close(fd); return; }
    struct sockaddr_can address={.can_family=AF_CAN,.can_ifindex=ifr.ifr_ifindex};
    static const canid_t ids[]={0x0aa,0x1a6,0x1d0,0x1d2,0x1f6,0x21a,0x2fc,0x193,0x1c2,0x24a,0x1d6,0x2f8,0x349};
    struct can_filter filters[sizeof(ids)/sizeof(ids[0])];
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)
        /* CAN_ERR_FLAG in a mask selects the kernel's error-only RX list. */
        filters[i]=(struct can_filter){ids[i],CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG};
    int one=1,size=65536;
    if(setsockopt(fd,SOL_CAN_RAW,CAN_RAW_FILTER,filters,sizeof(filters)) ||
       setsockopt(fd,SOL_SOCKET,SO_RXQ_OVFL,&one,sizeof(one)) ||
       setsockopt(fd,SOL_SOCKET,SO_TIMESTAMPNS,&one,sizeof(one)) ||
       setsockopt(fd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size)) ||
       bind(fd,(struct sockaddr*)&address,sizeof(address))) {close(fd);return;}
    if(ioctl(fd,SIOCGIFFLAGS,&ifr)<0 || !(ifr.ifr_flags&IFF_UP)) {close(fd);return;}
    can_input.fd=fd;can_input.cb=receive_can;previous_socket_drops=0;
    if(uloop_fd_add(&can_input,ULOOP_READ)) {close(fd);can_input.fd=-1;return;}
    fprintf(stderr,"vehicled: %s ready; waiting for CAN frames\n",interface);
}
/* Night state for the Android Auto receiver (it cannot use ubus): written only on a
 * change, atomically. Unknown lights leave the last value in place. */
static void write_night(int night) {
    static int written=-1;
    if(night<0 || night==written) return;
    mkdir("/var/run/vehicle",0755);
    FILE *file=fopen("/var/run/vehicle/night.tmp","w");
    if(!file) return;
    int ok=fprintf(file,"%d\n",night)>0;
    if(fclose(file) || !ok || rename("/var/run/vehicle/night.tmp","/var/run/vehicle/night")) return;
    written=night;
}
static void tick(struct uloop_timeout *t) {
    uint64_t now=vehicle_now_ms(); struct vehicle_event e;
    if(!simulated && now-last_retry>=1000) {
        last_retry=now;
        if(can_input.fd<0) open_can();
        else {
            struct ifreq ifr={0};snprintf(ifr.ifr_name,sizeof(ifr.ifr_name),"%s",interface);
            if(ioctl(can_input.fd,SIOCGIFFLAGS,&ifr)<0 || !(ifr.ifr_flags&IFF_UP)) close_can();
        }
    }
    if(simulated && demo && now>=next_demo) {
        next_demo=now+100;unsigned step=demo_step++%400;
        vehicle_set(&decoder,V_SPEED,step<200?step:400-step,now);
        vehicle_set(&decoder,V_RPM,800+(step%140)*40,now);
        vehicle_set(&decoder,V_COOLANT,85,now);vehicle_set(&decoder,V_FUEL,65,now);
        vehicle_set(&decoder,V_LEFT,(step/30)%2,now);vehicle_set(&decoder,V_RIGHT,0,now);
        vehicle_set(&decoder,V_PDC_ACTIVE,(step/70)%2,now);
        for(unsigned i=0;i<8;i++) vehicle_set(&decoder,V_PDC_FL+i,20+(step+i*17)%180,now);
    }
    if(vehicle_tick(&decoder,now,&e)) publish_event(&e);
    if(decoder.state.sequence!=published_sequence || now-last_publish>=1000) {
        vehicle_state_blob(&buffer,&decoder.state);
        if(ubus_notify(bus,&object,"state",buffer.head,-1)) fprintf(stderr,"vehicled: state notification failed\n");
        published_sequence=decoder.state.sequence;last_publish=now;
    }
    write_night(vehicle_night(&decoder.state,now));
    uloop_timeout_set(t,20);
}
static void bus_lost(struct ubus_context *ctx) { (void)ctx;uloop_end(); }
int main(int argc,char **argv) {
    const char *socket_path=NULL;
    unsigned fuel_capacity_litres=63;
    unsigned speed_clock_hz=4000;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--simulate")) simulated=true;
        else if(!strcmp(argv[i],"--no-demo")) demo=false;
        else if(!strcmp(argv[i],"--interface") && i+1<argc) interface=argv[++i];
        else if(!strcmp(argv[i],"--ubus-socket") && i+1<argc) socket_path=argv[++i];
        else if(!strcmp(argv[i],"--fuel-capacity-litres") && i+1<argc) {
            char *end;unsigned long value=strtoul(argv[++i],&end,10);
            if(!*argv[i] || *end || value<1 || value>200)return 2;
            fuel_capacity_litres=value;
        }
        else if(!strcmp(argv[i],"--speed-clock-hz") && i+1<argc) {
            char *end;unsigned long value=strtoul(argv[++i],&end,10);
            if(!*argv[i] || *end || (value!=2000 && value!=4000))return 2;
            speed_clock_hz=value;
        }
        else { fprintf(stderr,"Usage: vehicled [--interface can0] [--fuel-capacity-litres 63] [--speed-clock-hz 2000|4000] [--ubus-socket PATH] [--simulate [--no-demo]]\n");return 2; }
    }
    if(strlen(interface)>=IFNAMSIZ) return 2;
    uint64_t random[2];
    if(getrandom(random,sizeof(random),GRND_NONBLOCK)!=(ssize_t)sizeof(random)) {
        /* This identifies a process lifetime, not a security token. Do not wait for CRNG. */
        struct timespec realtime;clock_gettime(CLOCK_REALTIME,&realtime);
        random[0]=((uint64_t)realtime.tv_sec<<32) ^ realtime.tv_nsec;
        random[1]=(vehicle_now_ms()<<32) ^ (unsigned)getpid();
    }
    char generation[33];snprintf(generation,sizeof(generation),"%016llx%016llx",(unsigned long long)random[0],(unsigned long long)random[1]);
    vehicle_init(&decoder,generation,simulated);decoder.state.connected=simulated;
    decoder.fuel_capacity_ml=fuel_capacity_litres*1000;
    decoder.speed_clock_hz=speed_clock_hz;
    signal(SIGPIPE,SIG_IGN);uloop_init();
    bus=ubus_connect(socket_path);
    if(!bus) {fprintf(stderr,"vehicled: cannot connect to ubus\n");return 1;}
    bus->connection_lost=bus_lost;ubus_add_uloop(bus);
    int registration=ubus_add_object(bus,&object);
    if(registration) {fprintf(stderr,"vehicled: cannot register object: %s\n",ubus_strerror(registration));ubus_free(bus);return 1;}
    timer.cb=tick;uloop_timeout_set(&timer,1);
    fprintf(stderr,"vehicled: schema %d, %s, generation %s\n",VEHICLE_SCHEMA,simulated?"simulator":interface,generation);
    fprintf(stderr,"vehicled: speed clock %u Hz, fuel capacity %u litres\n",speed_clock_hz,fuel_capacity_litres);
    uloop_run();close_can();ubus_free(bus);blob_buf_free(&buffer);uloop_done();return 0;
}
