/* SPDX-License-Identifier: GPL-2.0-only */
#include "vehicle.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
const char *const vehicle_signal_names[VEHICLE_SIGNAL_COUNT] = {
    "speed_kph", "engine_rpm", "turn_left", "turn_right", "pdc_active",
    "pdc_front_left_cm", "pdc_front_middle_left_cm", "pdc_front_middle_right_cm", "pdc_front_right_cm",
    "pdc_rear_right_cm", "pdc_rear_middle_right_cm", "pdc_rear_middle_left_cm", "pdc_rear_left_cm",
    "lights", "doors_open", "cruise_speed_kph", "cruise_active", "coolant_c", "fuel_percent",
    "time_hour", "time_minute", "time_second", "date_day", "date_month", "date_year",
    "fuel_left_ml", "fuel_right_ml", "manual_gear"
};
const char *const vehicle_signal_units[VEHICLE_SIGNAL_COUNT] = {
    "km/h", "rpm", "bool", "bool", "bool", "cm", "cm", "cm", "cm", "cm", "cm", "cm", "cm",
    "enum", "bitmask", "km/h", "bool", "degC", "%", "h", "min", "s", "day", "month", "year", "ml", "ml", "enum"
};
const unsigned vehicle_signal_ttl_ms[VEHICLE_SIGNAL_COUNT] = {
    1500, 1500, 2000, 2000, 0, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
    0, 0, 2000, 2000, 5000, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1000
};
bool vehicle_signal_fresh(const struct vehicle_state *s,unsigned id,uint64_t now) {
    if(id>=VEHICLE_SIGNAL_COUNT || !s->connected) return false;
    const struct vehicle_signal *v=&s->signals[id];
    return v->seen && v->valid && now>=v->updated_ms &&
        (!vehicle_signal_ttl_ms[id] || now-v->updated_ms<=vehicle_signal_ttl_ms[id]);
}
int vehicle_night(const struct vehicle_state *s,uint64_t now) {
    if(!vehicle_signal_fresh(s,V_LIGHTS,now)) return -1;
    return s->signals[V_LIGHTS].value!=0;
}
static int month_days(int year, int month) {
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1 || month>12) return 0;
    return days[month-1]+(month==2 && year%4==0 && (year%100!=0 || year%400==0));
}
static bool calendar_valid(int hour,int minute,int second,int day,int month,int year) {
    return hour>=0 && hour<24 && minute>=0 && minute<60 && second>=0 && second<60 &&
        year>=1 && year<=2500 && day>=1 && day<=month_days(year,month);
}
bool vehicle_clock_time(const struct vehicle_state *s,uint64_t now,struct tm *out) {
    if(!s->connected) return false;
    uint64_t updated=s->signals[V_TIME_HOUR].updated_ms;
    for(unsigned i=V_TIME_HOUR;i<=V_DATE_YEAR;i++) {
        const struct vehicle_signal *v=&s->signals[i];
        if(!vehicle_signal_fresh(s,i,now) || v->updated_ms!=updated) return false;
    }
    int h=s->signals[V_TIME_HOUR].value,m=s->signals[V_TIME_MINUTE].value;
    int sec=s->signals[V_TIME_SECOND].value,d=s->signals[V_DATE_DAY].value;
    int mon=s->signals[V_DATE_MONTH].value,y=s->signals[V_DATE_YEAR].value;
    if(!calendar_valid(h,m,sec,d,mon,y)) return false;
    uint64_t seconds=h*3600+m*60+sec+(now-updated)/1000;
    while(seconds>=86400) {
        seconds-=86400;
        if(++d>month_days(y,mon)) {d=1;if(++mon>12) {mon=1;y++;}}
        if(y>2500) return false;
    }
    /* Gregorian weekday without host timezone, DST or time_t width assumptions. */
    static const int offsets[]={0,3,2,5,0,3,5,1,4,6,2,4};
    int wy=y-(mon<3);
    *out=(struct tm){.tm_year=y-1900,.tm_mon=mon-1,.tm_mday=d,
        .tm_hour=seconds/3600,.tm_min=seconds/60%60,.tm_sec=seconds%60,
        .tm_wday=(wy+wy/4-wy/100+wy/400+offsets[mon-1]+d)%7,.tm_isdst=-1};
    return true;
}
const char *vehicle_action_name(enum vehicle_action a) {
    static const char *const names[] = {"none", "next", "previous", "play", "stop"};
    return a >= V_ACTION_NONE && a <= V_STOP ? names[a] : "none";
}
uint64_t vehicle_now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (unsigned long)t.tv_nsec / 1000000;
}
bool vehicle_frame_timestamp(uint64_t now,const struct timespec *wall,const struct timespec *received,uint64_t *frame_time) {
    int64_t seconds=(int64_t)wall->tv_sec-received->tv_sec;
    if(seconds < -1 || seconds > 2) return false;
    int64_t age=seconds*1000000000+wall->tv_nsec-received->tv_nsec;
    if(age<0 || age>1000000000 || (uint64_t)(age/1000000)>now) return false;
    *frame_time=now-age/1000000;
    return true;
}
void vehicle_init(struct vehicle_decoder *d, const char *generation, bool simulated) {
    memset(d, 0, sizeof(*d));
    snprintf(d->state.generation, sizeof(d->state.generation), "%s", generation);
    d->state.simulated = simulated;
    d->fuel_capacity_ml=63000; /* User's 63-litre tank; configurable by vehicled. */
    d->speed_clock_hz=4000; /* Supplied E90 recording; 2000 is available for the Loopybunny variant. */
}
void vehicle_set(struct vehicle_decoder *d, enum vehicle_signal_id id, int32_t value, uint64_t now) {
    if ((unsigned)id >= VEHICLE_SIGNAL_COUNT) return;
    d->state.signals[id] = (struct vehicle_signal){value, true, true, now};
    d->state.sequence++;
}
void vehicle_disconnect(struct vehicle_decoder *d) {
    if (d->state.connected) d->state.sequence++;
    d->state.connected = false;
    for (unsigned i = 0; i < VEHICLE_SIGNAL_COUNT; ++i) {
        if (d->state.signals[i].valid) d->state.sequence++;
        d->state.signals[i].valid = false;
    }
    d->held = V_ACTION_NONE; d->long_sent = false;
    d->speed_baseline=false;
}
static bool emit(struct vehicle_decoder *d, enum vehicle_action a, uint64_t now, struct vehicle_event *e) {
    if (a == V_ACTION_NONE) return false;
    *e = (struct vehicle_event){a, ++d->state.event_sequence, now};
    return true;
}
static enum vehicle_action finish(struct vehicle_decoder *d) {
    enum vehicle_action a = d->long_sent ? V_ACTION_NONE : d->held;
    d->held = V_ACTION_NONE; d->long_sent = false;
    return a;
}
bool vehicle_tick(struct vehicle_decoder *d, uint64_t now, struct vehicle_event *e) {
    if (!d->state.simulated && d->state.connected &&
        (now < d->last_frame_ms || now - d->last_frame_ms > VEHICLE_CONNECTION_TTL_MS))
        vehicle_disconnect(d);
    for (unsigned i = 0; i < VEHICLE_SIGNAL_COUNT; ++i) {
        struct vehicle_signal *s = &d->state.signals[i];
        if (s->valid && !vehicle_signal_fresh(&d->state,i,now)) {
            s->valid = false; d->state.sequence++;
        }
    }
    /* Match the Melis release-gap fallback. A lost bus must not replay a key. */
    if (d->held && now - d->last_press_ms > 500)
        return emit(d, finish(d), now, e);
    return false;
}
static unsigned bitfield(const uint8_t *p, unsigned start, unsigned len) {
    unsigned v = 0;
    for (unsigned i = 0; i < len; ++i) v |= ((p[(start+i)/8] >> ((start+i)%8)) & 1u) << i;
    return v;
}
bool vehicle_frame(struct vehicle_decoder *d, const struct can_frame *f, uint64_t now, struct vehicle_event *e) {
    if (f->can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG) || f->can_dlc > 8) return false;
    d->state.received_frames++;
    bool adapter_gear=f->can_id==0x1d2 && f->can_dlc==1;
    if(adapter_gear) {
        /* UART-generated gear heartbeats are not evidence of live vehicle CAN. */
        if(!d->state.connected) return false;
    } else {
        d->last_frame_ms = now;
        if (!d->state.connected) {
            d->state.connected = true;
            d->state.sequence++;
        }
    }
    const uint8_t *p = f->data;
    switch (f->can_id) {
    case 0x1d2: {
        if(!adapter_gear) break;
        /* Private adapter protocol: DLC 1, ASCII R/N/1..6/?; not EGS data. */
        int gear=p[0]=='R'?V_GEAR_REVERSE:p[0]=='N'?V_GEAR_NEUTRAL:
            p[0]>='1' && p[0]<='6'?p[0]-'0':-1;
        vehicle_set(d,V_MANUAL_GEAR,gear<0?0:gear,now);
        if(gear<0) d->state.signals[V_MANUAL_GEAR].valid=false;
        break;
    }
    case 0x1d0:
        if(f->can_dlc!=8) break;
        /* Loopybunny 0x1D0 and bmw_e9x_e8x.dbc TEMP_ENG: byte 0 - 48 C.
         * Treat the all-ones temperature as unavailable, not a hot reading. */
        if(p[0]==0xff) {
            if(d->state.signals[V_COOLANT].valid) d->state.sequence++;
            d->state.signals[V_COOLANT].valid=false;
        } else vehicle_set(d,V_COOLANT,(int)p[0]-48,now);
        break;
    case 0x349: {
        if(f->can_dlc!=5 && f->can_dlc!=8) break;
        /* Loopybunny 0x349: two little-endian senders, 160 units/litre. */
        unsigned left=p[0]|(p[1]<<8),right=p[2]|(p[3]<<8);
        if(left==0xffff || right==0xffff || !d->fuel_capacity_ml) {
            const unsigned ids[]={V_FUEL,V_FUEL_LEFT_ML,V_FUEL_RIGHT_ML};
            for(unsigned i=0;i<3;i++) {
                if(d->state.signals[ids[i]].valid) d->state.sequence++;
                d->state.signals[ids[i]].valid=false;
            }
            break;
        }
        vehicle_set(d,V_FUEL_LEFT_ML,(left*1000u+80)/160,now);
        vehicle_set(d,V_FUEL_RIGHT_ML,(right*1000u+80)/160,now);
        unsigned percent=((uint64_t)(left+right)*100000u+d->fuel_capacity_ml*80u)/(d->fuel_capacity_ml*160u);
        vehicle_set(d,V_FUEL,percent>100?100:percent,now);
        break;
    }
    case 0x0aa:
        if (f->can_dlc != 8) break;
        /* Loopybunny BMW 0x0AA: bytes 4/5 little endian, quarter RPM. With the
         * engine off the car sends FF FF (16383 RPM): unavailable, not a reading. */
        if (p[4] == 0xff && p[5] == 0xff) {
            if (d->state.signals[V_RPM].valid) d->state.sequence++;
            d->state.signals[V_RPM].valid = false;
        } else vehicle_set(d, V_RPM, (p[4] | (p[5] << 8)) / 4, now);
        break;
    case 0x2f8: {
        if(f->can_dlc!=8) break;
        /* nberlette/canbus bmw-e90.dbc: month is byte 4's upper nibble;
         * year is the full little-endian value, not an offset from 2000. */
        int values[]={p[0],p[1],p[2],p[3],p[4]>>4,p[5]|(p[6]<<8)};
        if(!calendar_valid(values[0],values[1],values[2],values[3],values[4],values[5])) {
            for(unsigned i=V_TIME_HOUR;i<=V_DATE_YEAR;i++) {
                if(d->state.signals[i].valid) d->state.sequence++;
                d->state.signals[i].valid=false;
            }
            break;
        }
        for(unsigned i=0;i<6;i++) vehicle_set(d,V_TIME_HOUR+i,values[i],now);
        break;
    }
    case 0x1a6: {
        if(f->can_dlc!=8) break;
        /* 0x1A6: distance counter / 12-bit clock counter.
         * Distance advances mph/50ms; clock rate is configurable (2 or 4 kHz).
         * Use embedded time, so USB/UART batching cannot change the speed. */
        unsigned counter=p[0]|(p[1]<<8),clock=p[6]|((p[7]&15)<<8);
        if(!d->speed_baseline || !d->speed_clock_hz || now<d->speed_sample_ms ||
           now-d->speed_sample_ms>3000000u/d->speed_clock_hz) {
            if(d->state.signals[V_SPEED].valid)d->state.sequence++;
            d->state.signals[V_SPEED].valid=false;
        } else {
            unsigned elapsed=(clock-d->speed_clock)&4095;
            unsigned distance=(counter-d->speed_counter)&65535;
            /* Average at least 100ms to avoid quantization at fast simulator
             * rates. The real source normally sends one frame per 100–300ms. */
            if(elapsed<d->speed_clock_hz/10)break;
            uint64_t divisor=20ULL*elapsed*1000000;
            uint64_t speed=((uint64_t)distance*d->speed_clock_hz*1609344+divisor/2)/divisor;
            if(elapsed<=3000 && speed<=450)vehicle_set(d,V_SPEED,speed,now);
            else {
                if(d->state.signals[V_SPEED].valid)d->state.sequence++;
                d->state.signals[V_SPEED].valid=false;
            }
        }
        d->speed_counter=counter;d->speed_clock=clock;d->speed_sample_ms=now;d->speed_baseline=true;
        break;
    }
    case 0x1f6:
        if (f->can_dlc < 1) break;
        vehicle_set(d, V_LEFT, !!(p[0] & 0x10), now);
        vehicle_set(d, V_RIGHT, !!(p[0] & 0x20), now);
        break;
    case 0x21a:
        if (f->can_dlc < 1) break;
        /* BMW_E90_DASH uses 7 for high; the bench-cluster encoder emits 6. */
        vehicle_set(d, V_LIGHTS, ((p[0] & 7) == 7 || (p[0] & 7) == 6) ? 2 :
                    ((p[0] & 7) == 5 || (p[0] & 7) == 4), now);
        break;
    case 0x2fc:
        if (f->can_dlc < 3) break;
        vehicle_set(d, V_DOORS, (!!(p[1]&1)) | (!!(p[1]&4)<<1) | (!!(p[1]&16)<<2) |
                    (!!(p[1]&64)<<3) | (!!(p[2]&1)<<4), now);
        break;
    case 0x193:
        if (f->can_dlc < 6) break;
        vehicle_set(d, V_CRUISE_ACTIVE, bitfield(p, 43, 1), now);
        { int speed = (int)bitfield(p, 15, 8) - 2;
          vehicle_set(d, V_CRUISE_SPEED, speed > 0 ? speed : 0, now); }
        break;
    case 0x1c2:
        if (f->can_dlc != 8) break;
        for (unsigned i=0; i<8; ++i) {
            static const unsigned order[] = {4,5,6,7,3,2,1,0};
            vehicle_set(d, V_PDC_FL+i, p[order[i]], now);
        }
        break;
    case 0x24a:
        if (f->can_dlc && (p[0] == 5 || p[0] == 6)) vehicle_set(d, V_PDC_ACTIVE, p[0] == 5, now);
        break;
    case 0x1d6: {
        if (f->can_dlc != 2) break;
        enum vehicle_action a = V_ACTION_NONE;
        if (p[1] == 0 || p[1] == 12) {
            if (p[0] == 0xe0) a = V_NEXT;
            if (p[0] == 0xd0) a = V_PREVIOUS;
        }
        if (!a) {
            if (d->held && !d->long_sent && now - d->first_press_ms >= 1000) {
                a = d->held == V_NEXT ? V_PLAY : V_STOP;
                finish(d); return emit(d, a, now, e);
            }
            return emit(d, finish(d), now, e);
        }
        if (d->held && now - d->last_press_ms > 500) {
            /* A new press after a gap also starts a fresh hold. */
            enum vehicle_action previous = finish(d);
            d->held = a; d->first_press_ms = d->last_press_ms = now;
            return emit(d, previous, now, e);
        }
        if (a != d->held) {
            enum vehicle_action previous = finish(d);
            d->held = a; d->first_press_ms = d->last_press_ms = now;
            return emit(d, previous, now, e);
        }
        d->last_press_ms = now;
        if (!d->long_sent && now - d->first_press_ms >= 1000) {
            d->long_sent = true;
            return emit(d, a == V_NEXT ? V_PLAY : V_STOP, now, e);
        }
        break;
    }
    }
    return false;
}
