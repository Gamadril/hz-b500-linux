#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <linux/input.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int fake_open(const char *, int, ...);
static int fake_close(int);
static int fake_ioctl(int, unsigned long, ...);
static ssize_t fake_read(int, void *, size_t);
#define open fake_open
#define close fake_close
#define ioctl fake_ioctl
#define read fake_read
#include "../../native/touch_input.c"
#undef open
#undef close
#undef ioctl
#undef read

static int protocol = 1, closed, direct = 1, invalid_range, too_many, held, io_error;
static int read_error, short_read, eof, set_clock;
static int fake_open(const char *path, int flags, ...)
{ assert(!strcmp(path, "/dev/input/event7")); assert(flags == (O_RDONLY | O_NONBLOCK | O_CLOEXEC)); return 19; }
static int fake_close(int fd) { assert(fd == 19); closed++; return 0; }
static int fake_ioctl(int fd, unsigned long request, ...)
{
    assert(fd == 19);
    va_list args; va_start(args, request); void *p = va_arg(args, void *); va_end(args);
    if (io_error) { errno = EIO; return -1; }
    if (_IOC_NR(request) == 0x06) { strcpy(p, "gt9xxnew_ts"); return 12; }
    if (_IOC_NR(request) == 0x23) {
        unsigned long *bits = p;
        if (!protocol) {
            bits[ABS_X / BITS_PER_WORD] |= 1UL << (ABS_X % BITS_PER_WORD);
            bits[ABS_Y / BITS_PER_WORD] |= 1UL << (ABS_Y % BITS_PER_WORD);
        } else {
            bits[ABS_MT_POSITION_X / BITS_PER_WORD] |= 1UL << (ABS_MT_POSITION_X % BITS_PER_WORD);
            bits[ABS_MT_POSITION_Y / BITS_PER_WORD] |= 1UL << (ABS_MT_POSITION_Y % BITS_PER_WORD);
            bits[ABS_MT_TRACKING_ID / BITS_PER_WORD] |= 1UL << (ABS_MT_TRACKING_ID % BITS_PER_WORD);
            if (protocol == 2) bits[ABS_MT_SLOT / BITS_PER_WORD] |= 1UL << (ABS_MT_SLOT % BITS_PER_WORD);
        }
        return 0;
    }
    if (_IOC_NR(request) == 0x09) { if (direct) ((unsigned long *)p)[0] = 1UL << INPUT_PROP_DIRECT; return 0; }
    if (_IOC_NR(request) == 0x18) {
        if (held) ((unsigned long *)p)[BTN_TOUCH / BITS_PER_WORD] |= 1UL << (BTN_TOUCH % BITS_PER_WORD);
        return 0;
    }
    if (_IOC_NR(request) == 0x0a) {
        int32_t *values = p;
        for (int i = 0; i < 4; i++) values[i+1] = values[0] == ABS_MT_TRACKING_ID ? (held && i == 2 ? 200 : -1) : (100 + i);
        return 0;
    }
    if (request == EVIOCSCLOCKID) { assert(*(int *)p == CLOCK_MONOTONIC); set_clock++; return 0; }
    struct input_absinfo *abs = p; memset(abs, 0, sizeof(*abs));
    if (request == EVIOCGABS(ABS_MT_SLOT)) { abs->maximum = too_many ? 16 : 3; abs->value = 2; return 0; }
    if (request == EVIOCGABS(ABS_MT_POSITION_X) || request == EVIOCGABS(ABS_X)) { abs->maximum = invalid_range ? 0 : 1024; return 0; }
    if (request == EVIOCGABS(ABS_MT_POSITION_Y) || request == EVIOCGABS(ABS_Y)) { abs->maximum = 600; return 0; }
    assert(!"unexpected ioctl"); return -1;
}
static ssize_t fake_read(int fd, void *buffer, size_t size)
{
    assert(fd == 19 && size == sizeof(struct input_event));
    if (read_error) { errno = read_error; return -1; }
    if (eof) return 0;
    if (short_read) return 1;
    struct input_event e = {.time = {.tv_sec = 12, .tv_usec = 345}, .type = EV_ABS,
        .code = ABS_MT_TRACKING_ID, .value = -1};
    memcpy(buffer, &e, sizeof(e)); return sizeof(e);
}
int main(void)
{
    struct aa_touch_info info; struct aa_touch_event event;
    assert(aa_touch_open("/dev/input/event7", &info) == 19);
    assert(info.protocol == 1 && info.x_max == 1024 && info.y_max == 600 && !info.down && set_clock == 1);
    assert(!strcmp(info.name, "gt9xxnew_ts"));
    held = 1; assert(!aa_touch_state(19, &info) && info.down);
    assert(aa_touch_read(19, &event) == 1 && event.value == -1 && event.code == ABS_MT_TRACKING_ID && event.micros == 12000345);
    read_error = EAGAIN; assert(!aa_touch_read(19, &event));
    read_error = ENODEV; assert(aa_touch_read(19, &event) == -ENODEV);
    read_error = 0; short_read = 1; assert(aa_touch_read(19, &event) == -EPROTO);
    short_read = 0; eof = 1; assert(aa_touch_read(19, &event) == -ENODEV); eof = 0;
    protocol = 2;
    assert(aa_touch_open("/dev/input/event7", &info) == 19);
    assert(info.protocol == 2 && info.slots == 4 && info.current_slot == 2 && info.down && info.ids[2] == 200);
    assert(info.xs[2] == 102 && info.ys[2] == 102);
    too_many = 1; assert(aa_touch_open("/dev/input/event7", &info) == -E2BIG && closed == 1); too_many = 0;
    invalid_range = 1; assert(aa_touch_open("/dev/input/event7", &info) == -EINVAL && closed == 2); invalid_range = 0;
    direct = 0; assert(aa_touch_open("/dev/input/event7", &info) == -ENODEV && closed == 3); direct = 1;
    io_error = 1; assert(aa_touch_open("/dev/input/event7", &info) == -EIO && closed == 4); io_error = 0;
    protocol = 0; held = 0;
    assert(aa_touch_open("/dev/input/event7", &info) == 19 && info.protocol == 0 && !info.down);
    puts("PASS: evdev capabilities, type-A/type-B/single probing, current-slot resync, monotonic timestamps and failure cleanup");
}
