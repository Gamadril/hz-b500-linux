/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_VIDEO_PLAYER_H
#define HZ_VIDEO_PLAYER_H
#include <stdbool.h>
#define HZ_VIDEO_TRACK "/usr/share/videos/hz-ui/big_buck_bunny_720p_h264.mov"
#define HZ_VIDEO_DURATION 596
enum hz_video_status { HZ_VIDEO_STOPPED, HZ_VIDEO_OPENING, HZ_VIDEO_PLAYING, HZ_VIDEO_PAUSED, HZ_VIDEO_CLOSING, HZ_VIDEO_ERROR };
struct hz_video_state { enum hz_video_status status; unsigned position,duration; bool idle; char message[160]; };
void hz_video_play(void);
void hz_video_pause(void);
void hz_video_stop(void);
void hz_video_seek(unsigned seconds);
void hz_video_obscured(bool hidden);
void hz_video_clear_stale_plane(void);
const struct hz_video_state *hz_video_poll(void);
#endif
