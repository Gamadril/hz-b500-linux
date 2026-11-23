/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_VIDEO_PLANE_H
#define HZ_VIDEO_PLANE_H
/* Call only with the HMI instance lock held and its video worker idle. */
int hz_video_plane_recover(void);
/* Apply a persistent user setting through the BSP display API, range 0..255. */
int hz_lcd_brightness(unsigned value);
/* index 0..3: bright, contrast, hue, saturation (display-engine CSC). */
int hz_lcd_picture(unsigned index,unsigned value);
#endif
