/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef HZ_SETTINGS_STORE_H
#define HZ_SETTINGS_STORE_H
#include <stdbool.h>
#include <stddef.h>
enum hz_setting { HZ_BRIGHTNESS, HZ_VOLUME, HZ_MUTE, HZ_SPEAKER, HZ_DIMMED_BRIGHTNESS, HZ_BLUETOOTH,
    /* Display-engine picture, 0..100, 50 neutral (like Melis Setup > Display). */
    HZ_LCD_BRIGHT, HZ_LCD_CONTRAST, HZ_LCD_HUE, HZ_LCD_SATURATION, HZ_SETTING_COUNT };
#define HZ_SETTING_PICTURE(key) ((key)>=HZ_LCD_BRIGHT && (key)<=HZ_LCD_SATURATION)
/* Applied by the HMI itself, without the audio companion. */
#define HZ_SETTING_DISPLAY(key) ((key)==HZ_BRIGHTNESS || (key)==HZ_DIMMED_BRIGHTNESS || HZ_SETTING_PICTURE(key))
struct hz_settings_values {
    int brightness, volume, mute, speaker, fuel_litres, speed_clock_hz, dimmed_brightness, bluetooth;
    int picture[4]; /* HZ_LCD_BRIGHT..HZ_LCD_SATURATION */
    bool audio_available, audio_acknowledged;
    char timezone[64], bluetooth_name[64], source[32];
};
struct hz_settings_snapshot {
    struct hz_settings_values values;
    bool busy, ready, preview, lights_on;
    char message[160];
};
/* load/apply run on one worker. lights is a fast, thread-safe LCD-only hook;
 * it must not perform persistence or wait for companion communication. */
struct hz_settings_driver {
    int (*load)(struct hz_settings_values *values);
    int (*apply)(enum hz_setting key, int value, char *message, size_t size);
    int (*lights)(bool on);
    /* Picture or backlight value while a slider is dragged: fast, display only, not saved. */
    int (*preview)(enum hz_setting key, int value);
    /* Sends a new Bluetooth name; load() then shows what the radio reports. */
    int (*rename)(const char *name, char *message, size_t size);
};
/* Matches the companion service: printable ASCII, no spaces. */
#define HZ_BLUETOOTH_NAME_MAX 16
bool hz_settings_valid_name(const char *name);
bool hz_settings_rename(const char *name);
void hz_settings_init(const struct hz_settings_driver *driver, unsigned brightness, unsigned dimmed_brightness);
/* -1 preserves the last reported light state and services a pending retry. */
void hz_settings_lights(int on);
/* Shows a picture or backlight value now (UI thread); hz_settings_change saves it on release. */
void hz_settings_preview(enum hz_setting key, int value);
/* Ends an uncommitted preview: back to the saved picture value or the backlight level the lights select. */
void hz_settings_preview_restore(enum hz_setting key);
void hz_settings_refresh(void);
bool hz_settings_change(enum hz_setting key, int value);
void hz_settings_snapshot(struct hz_settings_snapshot *out);
#endif
