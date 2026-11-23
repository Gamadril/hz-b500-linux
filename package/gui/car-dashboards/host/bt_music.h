/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_BT_MUSIC_H
#define HZ_BT_MUSIC_H
#include <stddef.h>
/* request sends one ab5301b control line and returns 0 for an "OK ..." reply.
 * Without init (desktop preview) the page shows the service as unavailable. */
void hz_bt_music_init(int (*request)(const char *line, char *reply, size_t size));
void hz_bt_music_open(void);
/* Local Music/Video: pause the phone and give the speakers back to source sd. */
void hz_bt_music_release(void);
#endif
