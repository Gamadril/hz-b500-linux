/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VEHICLE_CLIENT_H
#define VEHICLE_CLIENT_H
#include "vehicle.h"
struct vehicle_callbacks {
    void (*state)(const struct vehicle_state *,void *);
    void (*event)(const struct vehicle_event *,void *);
    bool (*stop)(void *);
};
/* Reconnecting loop; callbacks run on its calling thread. Never calls UI code. */
void vehicle_client_run(const char *ubus_socket,const struct vehicle_callbacks *,void *);
#endif
