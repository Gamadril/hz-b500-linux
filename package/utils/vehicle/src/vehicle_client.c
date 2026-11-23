/* SPDX-License-Identifier: GPL-2.0-only */
#include "vehicle_client.h"
#include "vehicle_blob.h"
#include <libubus.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
struct connection {
    struct ubus_subscriber sub;
    struct vehicle_state state;
    const struct vehicle_callbacks *cb;
    void *arg;
    bool active, initialized;
    uint64_t last_snapshot, event_sequence;
};
static void update(struct connection *c,struct blob_attr *msg,bool snapshot) {
    struct vehicle_state s;
    if(!vehicle_state_parse(msg,&s)) return;
    bool restart=!c->initialized || strcmp(c->state.generation,s.generation);
    if(!restart && s.sequence<c->state.sequence) return;
    if(restart) {
        c->initialized=true;c->event_sequence=s.event_sequence;
    } else if(snapshot && s.event_sequence>c->event_sequence) {
        fprintf(stderr,"vehicle-client: missed %llu media events; not replaying\n",(unsigned long long)(s.event_sequence-c->event_sequence));
        c->event_sequence=s.event_sequence;
    }
    c->state=s;
    if(c->cb->state) c->cb->state(&c->state,c->arg);
}
static void snapshot_reply(struct ubus_request *req,int type,struct blob_attr *msg) {
    (void)type;update(req->priv,msg,true);
}
static int notification(struct ubus_context *ctx,struct ubus_object *obj,
                        struct ubus_request_data *req,const char *method,struct blob_attr *msg) {
    (void)ctx;(void)req;
    struct ubus_subscriber *sub=container_of(obj,struct ubus_subscriber,obj);
    struct connection *c=container_of(sub,struct connection,sub);
    if(!c->initialized) return 0;
    if(!strcmp(method,"state")) update(c,msg,false);
    if(!strcmp(method,"media")) {
        struct vehicle_event e; char generation[33];
        if(!vehicle_event_parse(msg,generation,&e) || strcmp(generation,c->state.generation)) return 0;
        if(e.sequence<=c->event_sequence) return 0;
        if(e.sequence!=c->event_sequence+1)
            fprintf(stderr,"vehicle-client: media sequence gap before %llu\n",(unsigned long long)e.sequence);
        c->event_sequence=e.sequence;
        /* Expired actions never run after a blocked or suspended consumer. */
        uint64_t now=vehicle_now_ms();
        if(e.timestamp_ms>now || now-e.timestamp_ms>1000) return 0;
        if(c->cb->event) c->cb->event(&e,c->arg);
    }
    return 0;
}
static void removed(struct ubus_context *ctx,struct ubus_subscriber *sub,uint32_t id) {
    (void)ctx;(void)id;
    struct connection *c=container_of(sub,struct connection,sub);c->active=false;
}
static void lost(struct ubus_context *ctx) { ctx->sock.eof=true; }
static bool stopped(struct connection *c) {return c->cb->stop && c->cb->stop(c->arg);}
static void invalidate(struct connection *c) {
    c->state.connected=false;
    for(unsigned i=0;i<VEHICLE_SIGNAL_COUNT;i++) c->state.signals[i].valid=false;
    if(c->cb->state) c->cb->state(&c->state,c->arg);
}
void vehicle_client_run(const char *socket_path,const struct vehicle_callbacks *cb,void *arg) {
    struct connection c={.cb=cb,.arg=arg};
    while(!stopped(&c)) {
        struct ubus_context *ctx=ubus_connect(socket_path);
        if(!ctx) {invalidate(&c);poll(NULL,0,200);continue;}
        memset(&c.sub,0,sizeof(c.sub));c.sub.cb=notification;c.sub.remove_cb=removed;
        c.active=false;c.initialized=false;ctx->connection_lost=lost;
        if(ubus_register_subscriber(ctx,&c.sub)) {ubus_free(ctx);poll(NULL,0,200);continue;}
        uint32_t id=0;uint64_t retry=0;
        while(!stopped(&c) && !ctx->sock.eof) {
            uint64_t now=vehicle_now_ms();
            if(!c.active && now>=retry) {
                retry=now+500;
                if(!ubus_lookup_id(ctx,VEHICLE_OBJECT,&id) && !ubus_subscribe(ctx,&c.sub,id)) {
                    c.active=true;c.initialized=false;c.last_snapshot=0;
                }
            }
            if(c.active && now-c.last_snapshot>=1000) {
                /* Subscribe first, then snapshot. Sequence/generation reject older updates. */
                c.last_snapshot=now;
                if(ubus_invoke(ctx,id,"snapshot",NULL,snapshot_reply,&c,1000)) {c.active=false;invalidate(&c);}
            }
            bool changed=false;
            for(unsigned i=0;i<VEHICLE_SIGNAL_COUNT;i++) {
                struct vehicle_signal *v=&c.state.signals[i];
                if(v->valid && !vehicle_signal_fresh(&c.state,i,now)) {v->valid=false;changed=true;}
            }
            if(changed && cb->state) cb->state(&c.state,arg);
            struct pollfd p={.fd=ctx->sock.fd,.events=POLLIN};
            int r=poll(&p,1,100);
            if(r<0 && errno!=EINTR) break;
            if(r>0 && (p.revents&(POLLERR|POLLHUP|POLLNVAL))) break;
            if(r>0 && (p.revents&POLLIN)) ubus_handle_event(ctx);
        }
        invalidate(&c);ubus_free(ctx);poll(NULL,0,100);
    }
}
