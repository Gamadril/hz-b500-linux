/* SPDX-License-Identifier: GPL-2.0-only */
#include "vehicle_blob.h"
#include <string.h>
#include <stdio.h>
void vehicle_state_blob(struct blob_buf *b, const struct vehicle_state *s) {
    blob_buf_init(b, 0);
    blobmsg_add_u32(b, "schema", VEHICLE_SCHEMA);
    blobmsg_add_string(b, "generation", s->generation);
    blobmsg_add_u64(b, "sequence", s->sequence);
    blobmsg_add_u64(b, "event_sequence", s->event_sequence);
    blobmsg_add_u64(b, "received_frames", s->received_frames);
    blobmsg_add_u64(b, "dropped_frames", s->dropped_frames);
    blobmsg_add_u8(b, "connected", s->connected);
    blobmsg_add_u8(b, "simulated", s->simulated);
    void *signals = blobmsg_open_table(b, "signals");
    for (unsigned i=0; i<VEHICLE_SIGNAL_COUNT; ++i) {
        const struct vehicle_signal *v=&s->signals[i];
        void *entry=blobmsg_open_table(b, vehicle_signal_names[i]);
        blobmsg_add_u32(b, "value", v->value);
        blobmsg_add_u8(b, "valid", v->valid);
        blobmsg_add_u8(b, "seen", v->seen);
        blobmsg_add_u64(b, "updated_ms", v->updated_ms);
        blobmsg_add_u32(b, "ttl_ms", vehicle_signal_ttl_ms[i]);
        blobmsg_add_string(b, "unit", vehicle_signal_units[i]);
        blobmsg_close_table(b, entry);
    }
    blobmsg_close_table(b, signals);
}
bool vehicle_state_parse(struct blob_attr *msg, struct vehicle_state *s) {
    enum {SCHEMA,GEN,SEQ,ESEQ,FRAMES,DROPS,CONNECTED,SIM,SIGNALS,MAX};
    static const struct blobmsg_policy policy[MAX] = {
        [SCHEMA]={"schema",BLOBMSG_TYPE_INT32}, [GEN]={"generation",BLOBMSG_TYPE_STRING},
        [SEQ]={"sequence",BLOBMSG_TYPE_INT64}, [ESEQ]={"event_sequence",BLOBMSG_TYPE_INT64},
        [FRAMES]={"received_frames",BLOBMSG_TYPE_INT64}, [DROPS]={"dropped_frames",BLOBMSG_TYPE_INT64},
        [CONNECTED]={"connected",BLOBMSG_TYPE_INT8}, [SIM]={"simulated",BLOBMSG_TYPE_INT8},
        [SIGNALS]={"signals",BLOBMSG_TYPE_TABLE}
    };
    struct blob_attr *tb[MAX];
    if (!msg) return false;
    blobmsg_parse(policy, MAX, tb, blob_data(msg), blob_len(msg));
    for (unsigned i=0; i<MAX; ++i) if (!tb[i]) return false;
    if (blobmsg_get_u32(tb[SCHEMA]) != VEHICLE_SCHEMA || strlen(blobmsg_get_string(tb[GEN])) > 32) return false;
    struct vehicle_state result={0};
    snprintf(result.generation,sizeof(result.generation),"%s",blobmsg_get_string(tb[GEN]));
    result.sequence=blobmsg_get_u64(tb[SEQ]); result.event_sequence=blobmsg_get_u64(tb[ESEQ]);
    result.received_frames=blobmsg_get_u64(tb[FRAMES]); result.dropped_frames=blobmsg_get_u64(tb[DROPS]);
    result.connected=blobmsg_get_bool(tb[CONNECTED]); result.simulated=blobmsg_get_bool(tb[SIM]);
    unsigned seen=0; struct blob_attr *entry; unsigned rem;
    blobmsg_for_each_attr(entry,tb[SIGNALS],rem) {
        unsigned id;
        for(id=0;id<VEHICLE_SIGNAL_COUNT;id++) if(!strcmp(blobmsg_name(entry),vehicle_signal_names[id])) break;
        if(id==VEHICLE_SIGNAL_COUNT || blobmsg_type(entry)!=BLOBMSG_TYPE_TABLE) continue;
        if(seen & (1u<<id)) return false;
        seen |= 1u<<id;
        static const struct blobmsg_policy p[]={
            {"value",BLOBMSG_TYPE_INT32},{"valid",BLOBMSG_TYPE_INT8},
            {"seen",BLOBMSG_TYPE_INT8},{"updated_ms",BLOBMSG_TYPE_INT64}
        };
        struct blob_attr *v[4]; blobmsg_parse(p,4,v,blobmsg_data(entry),blobmsg_data_len(entry));
        if(!v[0]||!v[1]||!v[2]||!v[3]) return false;
        result.signals[id]=(struct vehicle_signal){(int32_t)blobmsg_get_u32(v[0]),blobmsg_get_bool(v[1]),blobmsg_get_bool(v[2]),blobmsg_get_u64(v[3])};
    }
    /* Calendar and later fields extend schema 1; older services leave them unknown. */
    for(unsigned i=0;i<V_TIME_HOUR;i++) if(!(seen & (1u<<i))) return false;
    *s=result; return true;
}
void vehicle_event_blob(struct blob_buf *b, const struct vehicle_state *s, const struct vehicle_event *e) {
    blob_buf_init(b,0); blobmsg_add_u32(b,"schema",VEHICLE_SCHEMA);
    blobmsg_add_string(b,"generation",s->generation); blobmsg_add_u64(b,"sequence",e->sequence);
    blobmsg_add_u64(b,"timestamp_ms",e->timestamp_ms); blobmsg_add_string(b,"action",vehicle_action_name(e->action));
}
bool vehicle_event_parse(struct blob_attr *msg, char generation[33], struct vehicle_event *e) {
    static const struct blobmsg_policy p[]={
        {"schema",BLOBMSG_TYPE_INT32},{"generation",BLOBMSG_TYPE_STRING},
        {"sequence",BLOBMSG_TYPE_INT64},{"timestamp_ms",BLOBMSG_TYPE_INT64},{"action",BLOBMSG_TYPE_STRING}
    };
    struct blob_attr *v[5]; if(!msg) return false;
    blobmsg_parse(p,5,v,blob_data(msg),blob_len(msg));
    for(unsigned i=0;i<5;i++) if(!v[i]) return false;
    if(blobmsg_get_u32(v[0])!=VEHICLE_SCHEMA || strlen(blobmsg_get_string(v[1]))>32) return false;
    for(enum vehicle_action a=V_NEXT;a<=V_STOP;a++) {
        if(strcmp(blobmsg_get_string(v[4]),vehicle_action_name(a))) continue;
        snprintf(generation,33,"%s",blobmsg_get_string(v[1]));
        *e=(struct vehicle_event){a,blobmsg_get_u64(v[2]),blobmsg_get_u64(v[3])}; return true;
    }
    return false;
}
