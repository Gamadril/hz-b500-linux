#ifndef AA_TOUCH_INPUT_H
#define AA_TOUCH_INPUT_H
#include <stdint.h>
#define AA_TOUCH_SLOTS 16
/* Scalar-only boundary; native Linux input_event/ioctl layouts stay in C. */
struct aa_touch_info {
    int32_t protocol, x_min, x_max, y_min, y_max, slot_min, slots, down, current_slot;
    int32_t ids[AA_TOUCH_SLOTS], xs[AA_TOUCH_SLOTS], ys[AA_TOUCH_SLOTS];
    char name[128];
};
struct aa_touch_event { uint16_t type, code; int32_t value; uint64_t micros; };
/* File descriptor or negative errno. Protocols: 0=single, 1=MT type A, 2=MT slots. */
int aa_touch_open(const char *, struct aa_touch_info *);
int aa_touch_state(int, struct aa_touch_info *);
/* 1=event, 0=would block, negative errno=read failure. */
int aa_touch_read(int, struct aa_touch_event *);
#endif
