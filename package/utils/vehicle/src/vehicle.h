/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VEHICLE_H
#define VEHICLE_H
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <linux/can.h>
#define VEHICLE_SCHEMA 1
#define VEHICLE_OBJECT "vehicle"
#define VEHICLE_SIGNAL_COUNT 28
/* Live connection means recent subscribed CAN traffic, not an open UART. */
#define VEHICLE_CONNECTION_TTL_MS 5000
enum vehicle_signal_id {
    V_SPEED, V_RPM, V_LEFT, V_RIGHT, V_PDC_ACTIVE,
    V_PDC_FL, V_PDC_FLM, V_PDC_FRM, V_PDC_FR,
    V_PDC_RR, V_PDC_RRM, V_PDC_RLM, V_PDC_RL,
    V_LIGHTS, V_DOORS, V_CRUISE_SPEED, V_CRUISE_ACTIVE,
    V_COOLANT, V_FUEL,
    V_TIME_HOUR, V_TIME_MINUTE, V_TIME_SECOND, V_DATE_DAY, V_DATE_MONTH, V_DATE_YEAR,
    V_FUEL_LEFT_ML, V_FUEL_RIGHT_ML, V_MANUAL_GEAR
};
/* Forward gears use their displayed number; zero is neutral, seven reverse. */
enum vehicle_manual_gear { V_GEAR_NEUTRAL=0, V_GEAR_REVERSE=7 };
enum vehicle_action { V_ACTION_NONE, V_NEXT, V_PREVIOUS, V_PLAY, V_STOP };
struct vehicle_signal { int32_t value; bool valid, seen; uint64_t updated_ms; };
struct vehicle_state {
    char generation[33];
    uint64_t sequence, event_sequence, received_frames, dropped_frames;
    bool connected, simulated;
    struct vehicle_signal signals[VEHICLE_SIGNAL_COUNT];
};
struct vehicle_event { enum vehicle_action action; uint64_t sequence, timestamp_ms; };
struct vehicle_decoder {
    struct vehicle_state state;
    enum vehicle_action held;
    uint64_t first_press_ms, last_press_ms;
    uint64_t last_frame_ms;
    unsigned fuel_capacity_ml;
    unsigned speed_clock_hz;
    uint16_t speed_counter,speed_clock;
    uint64_t speed_sample_ms;
    bool speed_baseline;
    bool long_sent;
};
extern const char *const vehicle_signal_names[VEHICLE_SIGNAL_COUNT];
extern const char *const vehicle_signal_units[VEHICLE_SIGNAL_COUNT];
extern const unsigned vehicle_signal_ttl_ms[VEHICLE_SIGNAL_COUNT];
const char *vehicle_action_name(enum vehicle_action action);
uint64_t vehicle_now_ms(void);
/* A zero TTL retains a state until an explicit update or connection loss. */
bool vehicle_signal_fresh(const struct vehicle_state *, unsigned id, uint64_t now);
/* Night (low or high beam on, as the HMI LCD dimming): 1, day: 0, unknown: -1. */
int vehicle_night(const struct vehicle_state *, uint64_t now);
/* Vehicle local calendar time, advanced while the complete sample is valid. */
bool vehicle_clock_time(const struct vehicle_state *, uint64_t now, struct tm *);
bool vehicle_frame_timestamp(uint64_t now,const struct timespec *wall,const struct timespec *received,uint64_t *frame_time);
void vehicle_init(struct vehicle_decoder *, const char *generation, bool simulated);
void vehicle_set(struct vehicle_decoder *, enum vehicle_signal_id, int32_t, uint64_t);
void vehicle_disconnect(struct vehicle_decoder *);
bool vehicle_tick(struct vehicle_decoder *, uint64_t, struct vehicle_event *);
bool vehicle_frame(struct vehicle_decoder *, const struct can_frame *, uint64_t, struct vehicle_event *);
#endif
