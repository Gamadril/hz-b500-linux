/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_ANDROID_AUTO_H
#define HZ_ANDROID_AUTO_H
#include <stdbool.h>
#include <stddef.h>
/* launch runs the receiver (wireless or USB) and returns when it exits: 0, or
 * nonzero with a user-facing reason. Without init (desktop preview) the page says so. */
void hz_android_auto_init(int (*launch)(bool usb, char *message, size_t size));
void hz_android_auto_open(bool usb);
#endif
