/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_MUSIC_PLAYER_H
#define HZ_MUSIC_PLAYER_H
#include <stdbool.h>
#define HZ_MUSIC_TRACK "/usr/share/sounds/hz-ui/u_98o9hlkn7r-synthwave.mp3"
#define HZ_MUSIC_DURATION 130
enum hz_music_status { HZ_MUSIC_STOPPED, HZ_MUSIC_OPENING, HZ_MUSIC_PLAYING, HZ_MUSIC_PAUSED, HZ_MUSIC_ERROR };
struct hz_music_state { enum hz_music_status status; unsigned position; char message[192]; };
void hz_music_play(void);
void hz_music_pause(void);
void hz_music_stop(void);
void hz_music_seek(unsigned seconds);
const struct hz_music_state *hz_music_poll(void);
bool hz_music_quiescent(void);
#endif
