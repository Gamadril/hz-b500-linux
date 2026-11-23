/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#define main vehicled_main
#include "vehicled.c"
#undef main
#include <assert.h>
#include <stdarg.h>

/* Exercise the live socket path without requiring a host CAN interface. */
static uint64_t test_now=10000;
static bool link_up=true, queued, expired;
static struct can_frame incoming;
static struct vehicle_state published;
static unsigned registered, notifications;

int __wrap_clock_gettime(clockid_t id,struct timespec *t) {
    *t=(struct timespec){.tv_sec=test_now/1000+(id==CLOCK_REALTIME?1000:0),
                        .tv_nsec=(test_now%1000)*1000000};
    return 0;
}
int __wrap_socket(int domain,int type,int protocol) {
    assert(domain==PF_CAN && (type&SOCK_RAW) && protocol==CAN_RAW);
    return 123;
}
int __wrap_close(int fd) {assert(fd==123);return 0;}
int __wrap_ioctl(int fd,unsigned long request,...) {
    assert(fd==123);
    va_list args;va_start(args,request);
    struct ifreq *ifr=va_arg(args,struct ifreq *);va_end(args);
    assert(!strcmp(ifr->ifr_name,"can0"));
    if(request==SIOCGIFINDEX) ifr->ifr_ifindex=7;
    else {assert(request==SIOCGIFFLAGS);ifr->ifr_flags=link_up?IFF_UP:0;}
    return 0;
}
int __wrap_setsockopt(int fd,int level,int option,const void *value,socklen_t len) {
    assert(fd==123 && value && len);
    if(level==SOL_CAN_RAW) {
        assert(option==CAN_RAW_FILTER);
        const struct can_filter *filters=value;bool rpm=false,fuel=false,speed=false,coolant=false,gear=false;
        for(unsigned i=0;i<len/sizeof(*filters);i++) {
            /* af_can.c routes a mask containing CAN_ERR_FLAG to RX_ERR;
             * it is not a way to exclude error frames from data filters. */
            assert(!(filters[i].can_mask&CAN_ERR_FLAG));
            assert((filters[i].can_mask&(CAN_EFF_FLAG|CAN_RTR_FLAG))==(CAN_EFF_FLAG|CAN_RTR_FLAG));
            if(filters[i].can_id==0x0aa) rpm=true;
            if(filters[i].can_id==0x349) fuel=true;
            if(filters[i].can_id==0x1a6) speed=true;
            if(filters[i].can_id==0x1d0) coolant=true;
            if(filters[i].can_id==0x1d2) gear=true;
            assert(filters[i].can_id!=0x1a0);
        }
        assert(rpm && fuel && speed && coolant && gear); /* The old speed ID must not override 0x1A6. */
    }
    else assert(level==SOL_SOCKET && (option==SO_RXQ_OVFL || option==SO_TIMESTAMPNS || option==SO_RCVBUF));
    return 0;
}
int __wrap_bind(int fd,const struct sockaddr *address,socklen_t len) {
    const struct sockaddr_can *can=(const struct sockaddr_can *)address;
    assert(fd==123 && len==sizeof(*can) && can->can_family==AF_CAN && can->can_ifindex==7);
    return 0;
}
int __wrap_uloop_fd_add(struct uloop_fd *fd,unsigned flags) {
    assert(fd->fd==123 && flags==ULOOP_READ);registered++;return 0;
}
int __wrap_uloop_fd_delete(struct uloop_fd *fd) {
    assert(fd->fd==123 && registered);registered--;return 0;
}
int __wrap_uloop_timeout_set(struct uloop_timeout *t,int msecs) {
    assert(t==&timer && msecs==20);return 0;
}
int __wrap_ubus_notify(struct ubus_context *ctx,struct ubus_object *obj,
                     const char *method,struct blob_attr *msg,int timeout) {
    (void)ctx;(void)timeout;assert(obj==&object && !strcmp(method,"state"));
    assert(vehicle_state_parse(msg,&published));notifications++;return 0;
}
ssize_t __wrap_recvmsg(int fd,struct msghdr *msg,int flags) {
    assert(fd==123 && flags==MSG_DONTWAIT);
    if(!queued) {errno=EAGAIN;return -1;}
    queued=false;
    memcpy(msg->msg_iov[0].iov_base,&incoming,sizeof(incoming));
    msg->msg_controllen=CMSG_SPACE(sizeof(struct timespec));
    struct cmsghdr *c=CMSG_FIRSTHDR(msg);
    c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_TIMESTAMPNS;
    c->cmsg_len=CMSG_LEN(sizeof(struct timespec));
    struct timespec stamp;clock_gettime(CLOCK_REALTIME,&stamp);
    if(expired) stamp.tv_sec-=2;
    memcpy(CMSG_DATA(c),&stamp,sizeof(stamp));
    return sizeof(incoming);
}
static void receive(struct can_frame frame) {
    incoming=frame;queued=true;receive_can(&can_input,ULOOP_READ);
}
int main(void) {
    vehicle_init(&decoder,"live-socket",false);
    tick(&timer);
    assert(registered==1 && can_input.fd==123);
    /* An unplugged UART bridge still permits successful SocketCAN binding. */
    assert(notifications && !published.connected && !published.received_frames);
    test_now+=6000;tick(&timer);
    assert(!published.connected && !published.signals[V_SPEED].valid);

    struct can_frame invalid={.can_id=0x1a6|CAN_RTR_FLAG,.can_dlc=8};
    receive(invalid);
    assert(!decoder.state.connected && !decoder.state.received_frames);

    struct can_frame speed={.can_id=0x1a6,.can_dlc=8,.data={0}};
    expired=true;receive(speed);test_now+=1000;tick(&timer);
    assert(!published.connected && published.dropped_frames==1);
    expired=false;receive(speed);tick(&timer);
    assert(published.connected && published.received_frames==1 && !published.signals[V_SPEED].valid);
    speed.data[0]=128;speed.data[6]=0x90;speed.data[7]=1;receive(speed);tick(&timer);
    assert(published.signals[V_SPEED].valid && published.signals[V_SPEED].value==103);

    struct can_frame coolant={.can_id=0x1d0,.can_dlc=8,
        .data={0x8b,0xff,0x63,0xcd,0x5d,0x37,0xcd,0xa8}};
    receive(coolant);tick(&timer);
    assert(published.signals[V_COOLANT].valid && published.signals[V_COOLANT].value==91);
    struct can_frame gear={.can_id=0x1d2,.can_dlc=1,.data={'R'}};
    receive(gear);tick(&timer);
    assert(published.signals[V_MANUAL_GEAR].valid && published.signals[V_MANUAL_GEAR].value==V_GEAR_REVERSE);
    gear.data[0]='?';receive(gear);tick(&timer);
    assert(!published.signals[V_MANUAL_GEAR].valid);
    gear.data[0]='6';receive(gear);tick(&timer);
    assert(published.signals[V_MANUAL_GEAR].valid && published.signals[V_MANUAL_GEAR].value==6);

    test_now+=VEHICLE_CONNECTION_TTL_MS;tick(&timer);
    assert(published.connected && !published.signals[V_SPEED].valid);
    assert(!published.signals[V_MANUAL_GEAR].valid);
    test_now+=1000;tick(&timer);
    assert(!published.connected && !published.signals[V_SPEED].valid);
    assert(!published.signals[V_COOLANT].valid);
    assert(can_input.fd==123 && registered==1); /* Keep listening for recovery. */
    receive(speed);tick(&timer);assert(!published.signals[V_SPEED].valid);
    speed.data[0]=0;speed.data[1]=1;speed.data[6]=0x20;speed.data[7]=3;receive(speed);tick(&timer);
    assert(published.connected && published.signals[V_SPEED].valid);

    link_up=false;test_now+=1000;tick(&timer);
    assert(!published.connected && !published.signals[V_SPEED].valid && !registered);
    link_up=true;test_now+=1000;tick(&timer);
    assert(registered==1 && !published.connected);
    receive(speed);tick(&timer);assert(published.connected);
    close_can();blob_buf_free(&buffer);
    puts("PASS: live CAN socket waits for traffic, rejects queued frames, expires connection and recovers");
    return 0;
}
