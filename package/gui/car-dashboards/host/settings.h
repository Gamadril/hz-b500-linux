/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_SETTINGS_H
#define HZ_SETTINGS_H
#include <stdbool.h>
void hz_settings_open(void);
/* Desktop wheel input; distance is vertical pixels, positive toward the end. */
bool hz_settings_scroll_at(int x,int y,int distance);
#endif
