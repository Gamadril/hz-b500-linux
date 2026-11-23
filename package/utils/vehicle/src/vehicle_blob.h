/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VEHICLE_BLOB_H
#define VEHICLE_BLOB_H
#include "vehicle.h"
#include <libubox/blobmsg.h>
void vehicle_state_blob(struct blob_buf *, const struct vehicle_state *);
bool vehicle_state_parse(struct blob_attr *, struct vehicle_state *);
void vehicle_event_blob(struct blob_buf *, const struct vehicle_state *, const struct vehicle_event *);
bool vehicle_event_parse(struct blob_attr *, char generation[33], struct vehicle_event *);
#endif
