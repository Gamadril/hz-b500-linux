/* SPDX-License-Identifier: GPL-2.0-only */
#include "vehicle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct can_frame frame(unsigned id,unsigned len,const uint8_t *p) {
    struct can_frame f={.can_id=id,.can_dlc=len};if(p)memcpy(f.data,p,len);return f;
}
static struct can_frame speed_frame(unsigned distance,unsigned clock) {
    uint8_t bytes[]={distance&255,distance>>8,0x55,0xaa,0x33,0xcc,clock&255,0xf0|((clock>>8)&15)};
    return frame(0x1a6,8,bytes);
}
static void speed_checks(void) {
    struct vehicle_decoder d;struct vehicle_event event;
    vehicle_init(&d,"speed",false);
    assert(d.speed_clock_hz==4000);
    d.speed_clock_hz=2000; /* Loopybunny's explicit timing variant. */
    struct can_frame f=speed_frame(15,160);vehicle_frame(&d,&f,1134,&event);
    assert(!d.state.signals[V_SPEED].valid);
    /* Reference: 15->31 distance and 160->560 clock gives 4 MPH = 6 km/h. */
    f=speed_frame(31,560);vehicle_frame(&d,&f,1334,&event);
    assert(d.state.signals[V_SPEED].valid && d.state.signals[V_SPEED].value==6);
    /* Duplicate packets do not renew speed validity. */
    vehicle_frame(&d,&f,1400,&event);assert(d.state.signals[V_SPEED].updated_ms==1334);
    for(unsigned length=0;length<8;length++) {
        f.can_dlc=length;vehicle_frame(&d,&f,1400,&event);assert(d.state.signals[V_SPEED].updated_ms==1334);
    }
    vehicle_init(&d,"wrap",false);
    f=speed_frame(65500,3900);vehicle_frame(&d,&f,1000,&event);
    f=speed_frame(92,204);vehicle_frame(&d,&f,1000,&event); /* Both counters wrap; packets arrive in one UART batch. */
    assert(d.state.signals[V_SPEED].valid && d.state.signals[V_SPEED].value==103);
    f=speed_frame(92,604);vehicle_frame(&d,&f,1100,&event);
    assert(d.state.signals[V_SPEED].valid && !d.state.signals[V_SPEED].value);
    f=speed_frame(220,1004);vehicle_frame(&d,&f,3000,&event);assert(!d.state.signals[V_SPEED].valid);
    f=speed_frame(348,1404);vehicle_frame(&d,&f,3100,&event);assert(d.state.signals[V_SPEED].value==103);
    f=speed_frame(0,1804);vehicle_frame(&d,&f,3200,&event);assert(!d.state.signals[V_SPEED].valid);
    vehicle_disconnect(&d);assert(!d.speed_baseline);
    f=speed_frame(128,2204);vehicle_frame(&d,&f,3300,&event);assert(!d.state.signals[V_SPEED].valid);
    /* Captured E90 vector: +11 distance, +400 ticks over 100ms = 9 km/h. */
    vehicle_init(&d,"recording",false);
    f=speed_frame(1000,1000);vehicle_frame(&d,&f,687436,&event);
    f=speed_frame(1011,1400);vehicle_frame(&d,&f,687536,&event);
    assert(d.state.signals[V_SPEED].valid && d.state.signals[V_SPEED].value==9);
    puts("PASS: 0x1A6 published counter example, MPH conversion, wraparound, UART batching, zero, gaps and reset rejection");
}
static void clock_checks(void) {
    struct vehicle_decoder d;struct vehicle_event e;struct tm t;
    vehicle_init(&d,"clock",false);d.state.connected=true;
    assert(!vehicle_clock_time(&d.state,1000,&t));
    /* 2026-10-22 14:37:56; month low nibble and byte 7 are unused. */
    uint8_t date[]={14,37,56,22,0xaf,0xea,0x07,0xff};
    struct can_frame f=frame(0x2f8,8,date);
    vehicle_frame(&d,&f,1000,&e);
    assert(vehicle_clock_time(&d.state,1000,&t));
    assert(t.tm_year==126 && t.tm_mon==9 && t.tm_mday==22 && t.tm_wday==4);
    assert(t.tm_hour==14 && t.tm_min==37 && t.tm_sec==56);
    assert(vehicle_clock_time(&d.state,5000,&t) && t.tm_min==38 && t.tm_sec==0);
    assert(!vehicle_clock_time(&d.state,999,&t));
    assert(vehicle_clock_time(&d.state,6001,&t)); /* Calendar is retained between sparse updates. */
    assert(vehicle_clock_time(&d.state,1000+3*86400000ULL,&t) && t.tm_mday==25 && t.tm_hour==14);
    for(unsigned len=0;len<8;len++) {
        f.can_dlc=len;vehicle_frame(&d,&f,2000,&e);
        assert(d.state.signals[V_DATE_YEAR].updated_ms==1000);
    }
    f.can_dlc=8;
    const canid_t flags[]={CAN_EFF_FLAG,CAN_RTR_FLAG,CAN_ERR_FLAG};
    for(unsigned i=0;i<3;i++) {
        f.can_id=0x2f8|flags[i];vehicle_frame(&d,&f,2000,&e);
        assert(d.state.signals[V_DATE_YEAR].updated_ms==1000);
    }
    f.can_id=0x2f8;
    /* February 29, leap year, and midnight rollover. */
    uint8_t leap[]={23,59,59,28,0x2f,0xe8,0x07,0};memcpy(f.data,leap,8);
    vehicle_frame(&d,&f,2000,&e);
    assert(vehicle_clock_time(&d.state,3000,&t) && t.tm_mday==29 && t.tm_hour==0);
    f.data[3]=29;vehicle_frame(&d,&f,3000,&e);
    assert(vehicle_clock_time(&d.state,4000,&t) && t.tm_mon==2 && t.tm_mday==1);
    /* Full Gregorian leap rule and malformed fields invalidate the sample. */
    uint8_t invalid[][8]={
        {24,0,0,1,0x1f,0xea,7,0},{0,60,0,1,0x1f,0xea,7,0},
        {0,0,60,1,0x1f,0xea,7,0},{0,0,0,0,0x1f,0xea,7,0},
        {0,0,0,31,0x4f,0xea,7,0},{0,0,0,1,0x0f,0xea,7,0},
        {0,0,0,1,0xdf,0xea,7,0},{0,0,0,29,0x2f,0x34,8,0},
        {0,0,0,29,0x2f,0xea,7,0},{0,0,0,1,0x1f,0,0,0},
        {0,0,0,1,0x1f,0xc5,9,0},{255,255,255,255,255,255,255,255}
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        memcpy(f.data,date,8);vehicle_frame(&d,&f,4000,&e);
        memcpy(f.data,invalid[i],8);vehicle_frame(&d,&f,4100,&e);
        assert(!vehicle_clock_time(&d.state,4100,&t));
        for(unsigned j=V_TIME_HOUR;j<=V_DATE_YEAR;j++) assert(!d.state.signals[j].valid);
    }
    uint8_t year_end[]={23,59,59,31,0xcf,0xea,7,0};memcpy(f.data,year_end,8);
    vehicle_frame(&d,&f,5000,&e);
    assert(vehicle_clock_time(&d.state,6000,&t) && t.tm_year==127 && t.tm_mon==0 && t.tm_mday==1 && t.tm_wday==5);
    vehicle_tick(&d,10001,&e);assert(!d.state.signals[V_TIME_HOUR].valid);
    vehicle_frame(&d,&f,11000,&e);vehicle_disconnect(&d);
    assert(!vehicle_clock_time(&d.state,11000,&t));
    puts("PASS: CAN calendar layout, validation, leap dates, midnight/year rollover, freshness and disconnect");
}
static void coolant_checks(void) {
    struct vehicle_decoder d;struct vehicle_event e;
    vehicle_init(&d,"coolant",false);
    assert(!d.state.signals[V_COOLANT].seen);
    /* Published Loopybunny sample; DBC TEMP_ENG is 0|8@1+ (1,-48). */
    struct can_frame f=frame(0x1d0,8,(uint8_t[]){0x8b,0xff,0x63,0xcd,0x5d,0x37,0xcd,0xa8});
    vehicle_frame(&d,&f,1000,&e);
    assert(d.state.signals[V_COOLANT].valid && d.state.signals[V_COOLANT].value==91);
    assert(d.state.signals[V_COOLANT].updated_ms==1000);
    for(unsigned len=0;len<8;len++) {
        f.can_dlc=len;f.data[0]=0;vehicle_frame(&d,&f,1100,&e);
        assert(d.state.signals[V_COOLANT].value==91 && d.state.signals[V_COOLANT].updated_ms==1000);
    }
    f.can_dlc=8;
    const canid_t flags[]={CAN_EFF_FLAG,CAN_RTR_FLAG,CAN_ERR_FLAG};
    for(unsigned i=0;i<3;i++) {
        f.can_id=0x1d0|flags[i];vehicle_frame(&d,&f,1100,&e);
        assert(d.state.signals[V_COOLANT].updated_ms==1000);
    }
    f.can_id=0x1d0;
    const uint8_t raw[]={0,8,48,88,128,168,198,254};
    const int expected[]={-48,-40,0,40,80,120,150,206};
    for(unsigned i=0;i<sizeof(raw)/sizeof(raw[0]);i++) {
        f.data[0]=raw[i];vehicle_frame(&d,&f,2000+i,&e);
        assert(d.state.signals[V_COOLANT].valid && d.state.signals[V_COOLANT].value==expected[i]);
    }
    f.data[0]=255;uint64_t sequence=d.state.sequence;
    vehicle_frame(&d,&f,3000,&e);
    assert(!d.state.signals[V_COOLANT].valid && d.state.sequence>sequence);
    f.data[0]=139;vehicle_frame(&d,&f,4000,&e);
    /* Keep the bus alive with other traffic, so signal expiry is independent. */
    struct can_frame other=frame(0x21a,1,(uint8_t[]){0});
    vehicle_frame(&d,&other,9000,&e);vehicle_tick(&d,9000,&e);
    assert(d.state.signals[V_COOLANT].valid);
    vehicle_tick(&d,9001,&e);
    assert(d.state.connected && !d.state.signals[V_COOLANT].valid);
    vehicle_frame(&d,&f,10000,&e);vehicle_disconnect(&d);
    assert(!d.state.signals[V_COOLANT].valid);
    puts("PASS: 0x1D0 coolant example/DBC offset, signed temperatures, DLC/flag rejection, unavailable value, expiry and disconnect");
}
static void rpm_checks(void) {
    struct vehicle_decoder d;struct vehicle_event e;
    vehicle_init(&d,"rpm",false);
    struct can_frame running=frame(0x0aa,8,(uint8_t[]){0,0,0,0,0x10,0x27,0,0}); /* 10000/4 = 2500 */
    vehicle_frame(&d,&running,1000,&e);vehicle_frame(&d,&running,1010,&e);
    assert(d.state.signals[V_RPM].valid && d.state.signals[V_RPM].value==2500);
    /* Engine off: FF FF must not show 16383 RPM. */
    struct can_frame off=frame(0x0aa,8,(uint8_t[]){0,0,0,0,0xff,0xff,0,0});
    vehicle_frame(&d,&off,1020,&e);
    assert(!d.state.signals[V_RPM].valid);
    vehicle_frame(&d,&running,1030,&e);
    assert(d.state.signals[V_RPM].valid && d.state.signals[V_RPM].value==2500);
}
static void night_checks(void) {
    struct vehicle_decoder d;struct vehicle_event e;
    vehicle_init(&d,"night",false);
    assert(vehicle_night(&d.state,1000)==-1); /* no light frame yet */
    const uint8_t lights[]={0,4,7};const int night[]={0,1,1}; /* off, low, high beam */
    for(unsigned i=0;i<3;i++) {
        struct can_frame f=frame(0x21a,1,(uint8_t[]){lights[i]});
        vehicle_frame(&d,&f,1000+i*100,&e);vehicle_frame(&d,&f,1010+i*100,&e);
        assert(vehicle_night(&d.state,1020+i*100)==night[i]);
    }
}
static void manual_gear_checks(void) {
    struct vehicle_decoder d;struct vehicle_event e;
    vehicle_init(&d,"manual-gear",false);
    struct can_frame f=frame(0x1d2,1,(uint8_t[]){'R'});
    vehicle_frame(&d,&f,1000,&e);
    assert(!d.state.connected && !d.state.signals[V_MANUAL_GEAR].seen && !d.last_frame_ms);
    struct can_frame traffic=frame(0x21a,1,(uint8_t[]){0});
    vehicle_frame(&d,&traffic,1000,&e);
    const char gears[]="RN123456";
    const int values[]={V_GEAR_REVERSE,V_GEAR_NEUTRAL,1,2,3,4,5,6};
    for(unsigned i=0;i<sizeof(gears)-1;i++) {
        f.data[0]=gears[i];vehicle_frame(&d,&f,1100+i,&e);
        assert(d.state.signals[V_MANUAL_GEAR].valid && d.state.signals[V_MANUAL_GEAR].seen);
        assert(d.state.signals[V_MANUAL_GEAR].value==values[i]);
        assert(d.state.signals[V_MANUAL_GEAR].updated_ms==1100+i && d.last_frame_ms==1000);
    }
    f.data[0]='6';vehicle_frame(&d,&f,1200,&e);
    for(unsigned len=0;len<=8;len++)if(len!=1) {
        f.can_dlc=len;f.data[0]='R';vehicle_frame(&d,&f,1300,&e);
        assert(d.state.signals[V_MANUAL_GEAR].value==6 && d.state.signals[V_MANUAL_GEAR].updated_ms==1200);
    }
    /* Genuine automatic-format frames must not overwrite the manual gear. */
    f=frame(0x1d2,8,(uint8_t[]){0xd2,0x0f,0xff,0x0c,0xf0,0xff,0xff,0xff});
    vehicle_frame(&d,&f,1350,&e);
    assert(d.state.signals[V_MANUAL_GEAR].value==6 && d.state.signals[V_MANUAL_GEAR].updated_ms==1200);
    f.can_dlc=1;f.data[0]='R';
    const canid_t flags[]={CAN_EFF_FLAG,CAN_RTR_FLAG,CAN_ERR_FLAG};
    for(unsigned i=0;i<3;i++) {
        f.can_id=0x1d2|flags[i];vehicle_frame(&d,&f,1400,&e);
        assert(d.state.signals[V_MANUAL_GEAR].updated_ms==1200);
    }
    f.can_id=0x1d2;
    const uint8_t invalid[]={'?','0','7','n','r',0,255};
    for(unsigned i=0;i<sizeof(invalid);i++) {
        f.data[0]='R';vehicle_frame(&d,&f,2000+i*20,&e);
        f.data[0]=invalid[i];vehicle_frame(&d,&f,2001+i*20,&e);
        assert(!d.state.signals[V_MANUAL_GEAR].valid && d.state.signals[V_MANUAL_GEAR].seen);
        assert(d.state.signals[V_MANUAL_GEAR].updated_ms==2001+i*20);
    }
    f.data[0]='2';vehicle_frame(&d,&f,3000,&e);
    vehicle_frame(&d,&f,3900,&e); /* An identical heartbeat renews freshness. */
    vehicle_frame(&d,&traffic,4900,&e);vehicle_tick(&d,4900,&e);
    assert(d.state.signals[V_MANUAL_GEAR].valid && d.state.signals[V_MANUAL_GEAR].updated_ms==3900);
    vehicle_tick(&d,4901,&e);
    assert(d.state.connected && !d.state.signals[V_MANUAL_GEAR].valid);
    f.data[0]='3';vehicle_frame(&d,&f,5000,&e);
    /* Synthetic UART traffic alone must not retain a physical CAN connection. */
    for(unsigned now=5100;now<=10000;now+=100) {
        vehicle_frame(&d,&f,now,&e);vehicle_tick(&d,now,&e);
    }
    assert(!d.state.connected && !d.state.signals[V_MANUAL_GEAR].valid && d.last_frame_ms==4900);
    vehicle_frame(&d,&f,10100,&e);assert(!d.state.connected && !d.state.signals[V_MANUAL_GEAR].valid);
    vehicle_frame(&d,&traffic,10200,&e);f.data[0]='6';vehicle_frame(&d,&f,10200,&e);
    assert(d.state.signals[V_MANUAL_GEAR].valid && d.state.signals[V_MANUAL_GEAR].value==6);
    vehicle_disconnect(&d);assert(!d.state.signals[V_MANUAL_GEAR].valid);
    puts("PASS: private manual gear R/N/1..6, native frame/flag rejection, unknown, heartbeat, expiry, connection isolation and recovery");
}
int main(void) {
    rpm_checks();
    night_checks();
    manual_gear_checks();
    coolant_checks();
    clock_checks();
    speed_checks();
    {
        struct vehicle_decoder fuel;struct vehicle_event event;
        vehicle_init(&fuel,"fuel",false);
        uint8_t senders[]={0x80,0x0c,0x40,0x06,0}; /* 20L + 10L in a 63L tank. */
        struct can_frame f=frame(0x349,5,senders);
        vehicle_frame(&fuel,&f,1000,&event);
        assert(fuel.state.signals[V_FUEL].valid && fuel.state.signals[V_FUEL].value==48);
        assert(fuel.state.signals[V_FUEL_LEFT_ML].value==20000 && fuel.state.signals[V_FUEL_RIGHT_ML].value==10000);
        for(unsigned length=0;length<=8;length++)if(length!=5 && length!=8) {
            f.can_dlc=length;vehicle_frame(&fuel,&f,1500,&event);
            assert(fuel.state.signals[V_FUEL].updated_ms==1000);
        }
        f.can_dlc=8;vehicle_frame(&fuel,&f,1800,&event);assert(fuel.state.signals[V_FUEL].value==48);
        f.can_dlc=5;fuel.fuel_capacity_ml=75000;
        vehicle_frame(&fuel,&f,2000,&event);assert(fuel.state.signals[V_FUEL].value==40);
        memcpy(f.data,(uint8_t[]){0x76,0x0f,0xbe,0x1a,0},5);fuel.fuel_capacity_ml=63000;
        vehicle_frame(&fuel,&f,3000,&event);assert(fuel.state.signals[V_FUEL].value==100);
        assert(fuel.state.signals[V_FUEL_LEFT_ML].value==24738 && fuel.state.signals[V_FUEL_RIGHT_ML].value==42788);
        f.data[0]=f.data[1]=255;vehicle_frame(&fuel,&f,4000,&event);
        assert(!fuel.state.signals[V_FUEL].valid && !fuel.state.signals[V_FUEL_LEFT_ML].valid);
        memset(f.data,0,5);vehicle_frame(&fuel,&f,5000,&event);
        assert(fuel.state.signals[V_FUEL].valid && fuel.state.signals[V_FUEL].value==0);
        /* PDC activation, lights and fuel survive their old TTLs while CAN stays connected. */
        f=frame(0x24a,1,(uint8_t[]){5});vehicle_frame(&fuel,&f,5000,&event);
        f=frame(0x21a,1,(uint8_t[]){7});vehicle_frame(&fuel,&f,5000,&event);
        f=frame(0x2fc,3,(uint8_t[]){0,0x11,1});vehicle_frame(&fuel,&f,5000,&event);
        f=frame(0x0aa,8,(uint8_t[8]){0});vehicle_frame(&fuel,&f,20000,&event);
        vehicle_tick(&fuel,20000,&event);
        assert(fuel.state.signals[V_PDC_ACTIVE].valid && fuel.state.signals[V_PDC_ACTIVE].value==1);
        assert(fuel.state.signals[V_FUEL].valid && fuel.state.signals[V_LIGHTS].valid);
        assert(fuel.state.signals[V_DOORS].valid && fuel.state.signals[V_DOORS].value==21);
        f=frame(0x21a,1,(uint8_t[]){6});vehicle_frame(&fuel,&f,20000,&event);
        assert(fuel.state.signals[V_LIGHTS].value==2);
        f=frame(0x24a,1,(uint8_t[]){6});vehicle_frame(&fuel,&f,20100,&event);
        assert(!fuel.state.signals[V_PDC_ACTIVE].value);
        vehicle_tick(&fuel,25101,&event);
        assert(!fuel.state.connected && !fuel.state.signals[V_FUEL].valid && !fuel.state.signals[V_LIGHTS].valid);
        assert(!fuel.state.signals[V_DOORS].valid);
        puts("PASS: 0x349 sender litres/63L percentage, configurable capacity, bounds, sparse retained states and disconnect");
    }
    struct vehicle_decoder rpm_decoder;struct vehicle_event rpm_event;
    vehicle_init(&rpm_decoder,"rpm",false);
    /* Published Loopybunny 0x0AA example: 0x0D34 / 4 = 845 RPM. */
    const uint8_t rpm_data[]={0x5f,0x59,0xff,0,0x34,0x0d,0x80,0x99};
    struct can_frame rpm_frame=frame(0x0aa,8,rpm_data);
    vehicle_frame(&rpm_decoder,&rpm_frame,1000,&rpm_event);
    assert(rpm_decoder.state.signals[V_RPM].valid && rpm_decoder.state.signals[V_RPM].value==845);
    for(unsigned len=0;len<8;len++) {
        rpm_frame.can_dlc=len;vehicle_frame(&rpm_decoder,&rpm_frame,1100,&rpm_event);
        assert(rpm_decoder.state.signals[V_RPM].updated_ms==1000);
    }
    rpm_frame.can_dlc=8;rpm_frame.data[4]=0x10;rpm_frame.data[5]=0x27;
    vehicle_frame(&rpm_decoder,&rpm_frame,1200,&rpm_event);
    assert(rpm_decoder.state.signals[V_RPM].value==2500);
    vehicle_tick(&rpm_decoder,2701,&rpm_event);assert(!rpm_decoder.state.signals[V_RPM].valid);
    puts("PASS: published BMW RPM vector, byte offsets, short frames and expiry");
    struct timespec wall={10,500000000},received={10,0};uint64_t frame_time;
    assert(vehicle_frame_timestamp(2000,&wall,&received,&frame_time)&&frame_time==1500);
    received=(struct timespec){9,500000000};
    assert(vehicle_frame_timestamp(2000,&wall,&received,&frame_time)&&frame_time==1000);
    received.tv_nsec--;
    assert(!vehicle_frame_timestamp(2000,&wall,&received,&frame_time));
    received=(struct timespec){10,600000000};
    assert(!vehicle_frame_timestamp(2000,&wall,&received,&frame_time));
    received=(struct timespec){1,0};
    assert(!vehicle_frame_timestamp(2000,&wall,&received,&frame_time));
    received=(struct timespec){10,0};
    assert(!vehicle_frame_timestamp(100,&wall,&received,&frame_time));
    struct vehicle_decoder d;struct vehicle_event e;
    vehicle_init(&d,"test",false);
    for(unsigned i=0;i<VEHICLE_SIGNAL_COUNT;i++) assert(!d.state.signals[i].valid);
    uint8_t speed[]={128,0,128,0,128,0,0x90,0xf1};
    struct can_frame f=frame(0x1a6,8,(uint8_t[8]){0});
    vehicle_frame(&d,&f,900,&e);assert(!d.state.signals[V_SPEED].valid);
    f=frame(0x1a6,8,speed);
    assert(!vehicle_frame(&d,&f,1000,&e));assert(d.state.signals[V_SPEED].value==103);
    assert(d.state.signals[V_SPEED].valid);assert(!d.state.signals[V_RPM].valid);
    f.can_dlc=1;vehicle_frame(&d,&f,1100,&e);assert(d.state.signals[V_SPEED].updated_ms==1000);
    f=frame(0x1a6|CAN_EFF_FLAG,8,speed);vehicle_frame(&d,&f,1100,&e);assert(d.state.signals[V_SPEED].updated_ms==1000);
    f.can_id=0x1a6|CAN_RTR_FLAG;vehicle_frame(&d,&f,1100,&e);assert(d.state.signals[V_SPEED].updated_ms==1000);
    f.can_id=0x1a6|CAN_ERR_FLAG;vehicle_frame(&d,&f,1100,&e);assert(d.state.signals[V_SPEED].updated_ms==1000);
    uint8_t distances[]={0,1,2,3,4,5,6,7};f=frame(0x1c2,8,distances);vehicle_frame(&d,&f,1200,&e);
    unsigned order[]={4,5,6,7,3,2,1,0};for(unsigned i=0;i<8;i++)assert(d.state.signals[V_PDC_FL+i].value==(int)order[i]);
    uint8_t enabled[]={5};f=frame(0x24a,1,enabled);vehicle_frame(&d,&f,1200,&e);assert(d.state.signals[V_PDC_ACTIVE].value==1);
    uint8_t invalid[]={7};f=frame(0x24a,1,invalid);vehicle_frame(&d,&f,1300,&e);assert(d.state.signals[V_PDC_ACTIVE].updated_ms==1200);
    uint8_t turns[]={0x30};f=frame(0x1f6,1,turns);vehicle_frame(&d,&f,1300,&e);
    assert(d.state.signals[V_LEFT].value&&d.state.signals[V_RIGHT].value);
    uint8_t doors[]={0,0x45,1};f=frame(0x2fc,3,doors);vehicle_frame(&d,&f,1300,&e);assert(d.state.signals[V_DOORS].value==27);
    uint8_t cruise[]={0,0,0,0,0,8}; /* active bit 43 */
    cruise[1]=0;cruise[2]=51; /* bits 15..22 = 102 => setpoint 100 */
    f=frame(0x193,6,cruise);vehicle_frame(&d,&f,1400,&e);
    assert(d.state.signals[V_CRUISE_SPEED].value==100&&d.state.signals[V_CRUISE_ACTIVE].value==1);
    assert(!vehicle_tick(&d,2501,&e));assert(!d.state.signals[V_SPEED].valid);assert(!d.state.signals[V_PDC_FL].valid);
    assert(d.state.signals[V_SPEED].value==103); /* Expiry does not fabricate zero. */
    uint8_t up[]={0xe0,12},release[]={0xc0,12},down[]={0xd0,0};
    f=frame(0x1d6,2,up);assert(!vehicle_frame(&d,&f,3000,&e));assert(!vehicle_frame(&d,&f,3100,&e));
    f=frame(0x1d6,2,release);assert(vehicle_frame(&d,&f,3200,&e));assert(e.action==V_NEXT&&e.sequence==1);
    assert(!vehicle_frame(&d,&f,3300,&e));
    f=frame(0x1d6,2,up);assert(!vehicle_frame(&d,&f,4000,&e));
    for(unsigned t=4200;t<5000;t+=200)assert(!vehicle_frame(&d,&f,t,&e));
    assert(vehicle_frame(&d,&f,5000,&e));assert(e.action==V_PLAY&&e.sequence==2);
    assert(!vehicle_frame(&d,&f,5200,&e));f=frame(0x1d6,2,release);assert(!vehicle_frame(&d,&f,5300,&e));
    f=frame(0x1d6,2,down);assert(!vehicle_frame(&d,&f,6000,&e));
    assert(vehicle_tick(&d,6501,&e));assert(e.action==V_PREVIOUS&&e.sequence==3);
    f=frame(0x1d6,2,up);assert(!vehicle_frame(&d,&f,7000,&e));vehicle_disconnect(&d);
    assert(!vehicle_tick(&d,8000,&e));assert(d.state.event_sequence==3);
    puts("PASS: BMW decoding, DLC/flag validation, stale/unknown values, short/long presses and disconnect cancellation");
}
