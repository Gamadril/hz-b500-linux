/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_SETTINGS_DEVICE_H
#define HZ_SETTINGS_DEVICE_H
#include "settings_store.h"
extern const struct hz_settings_driver hz_settings_device;
void hz_settings_device_init(unsigned brightness,unsigned dimmed_brightness);
/* One ab5301b control-socket request; 0 for an "OK ..." reply. Blocks up to 15 s. */
int hz_companion(const char *request,char *reply,size_t size);
/* Runs Android Auto (wireless or USB) until it exits; nonzero with a reason on failure. */
int hz_android_auto_launch(bool usb,char *message,size_t size);
#endif
