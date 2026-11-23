#define _POSIX_C_SOURCE 200809L
#include "touch_input.h"
#include <linux/input.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define BITS_PER_WORD (8 * sizeof(unsigned long))
#define WORDS(n) (((n) + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define HAS(bits, n) (!!((bits)[(n) / BITS_PER_WORD] & (1UL << ((n) % BITS_PER_WORD))))

int aa_touch_state(int fd, struct aa_touch_info *info)
{
    unsigned long keys[WORDS(KEY_CNT)] = {0};
    if (info->protocol != 2) {
        if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) < 0) return -errno;
        info->down = HAS(keys, BTN_TOUCH);
        return 0;
    }
    /* EVIOCGMTSLOTS starts at slot zero, regardless of the advertised minimum. */
    if (info->slots < 1 || info->slots > AA_TOUCH_SLOTS || info->slot_min != 0) return -EINVAL;
    struct input_absinfo current;
    if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &current) < 0) return -errno;
    if (current.value < 0 || current.value >= info->slots) return -EINVAL;
    info->current_slot = current.value;
    int32_t values[1 + AA_TOUCH_SLOTS];
    const unsigned axes[] = {ABS_MT_TRACKING_ID, ABS_MT_POSITION_X, ABS_MT_POSITION_Y};
    int32_t *outputs[] = {info->ids, info->xs, info->ys};
    info->down = 0;
    for (int axis = 0; axis < 3; ++axis) {
        memset(values, 0, sizeof(values)); values[0] = (int32_t)axes[axis];
        if (ioctl(fd, EVIOCGMTSLOTS((info->slots + 1) * sizeof(int32_t)), values) < 0) return -errno;
        memcpy(outputs[axis], values + 1, info->slots * sizeof(int32_t));
    }
    for (int slot = 0; slot < info->slots; ++slot) info->down |= info->ids[slot] >= 0;
    return 0;
}

int aa_touch_open(const char *path, struct aa_touch_info *info)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -errno;
    memset(info, 0, sizeof(*info));
    for (int i = 0; i < AA_TOUCH_SLOTS; i++) info->ids[i] = -1;
    unsigned long bits[WORDS(ABS_CNT)] = {0}, props[WORDS(INPUT_PROP_CNT)] = {0};
    int rc = -ENODEV;
    if (ioctl(fd, EVIOCGNAME(sizeof(info->name)), info->name) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0 ||
        ioctl(fd, EVIOCGPROP(sizeof(props)), props) < 0) { rc = -errno; goto bad; }
    info->name[sizeof(info->name) - 1] = 0;
    if (!HAS(props, INPUT_PROP_DIRECT)) goto bad;
    unsigned x = ABS_X, y = ABS_Y;
    if (HAS(bits, ABS_MT_POSITION_X) && HAS(bits, ABS_MT_POSITION_Y) && HAS(bits, ABS_MT_TRACKING_ID)) {
        x = ABS_MT_POSITION_X; y = ABS_MT_POSITION_Y;
        info->protocol = HAS(bits, ABS_MT_SLOT) ? 2 : 1;
    } else if (!HAS(bits, ABS_X) || !HAS(bits, ABS_Y)) goto bad;
    struct input_absinfo ax, ay;
    if (ioctl(fd, EVIOCGABS(x), &ax) < 0 || ioctl(fd, EVIOCGABS(y), &ay) < 0) { rc = -errno; goto bad; }
    if (ax.maximum <= ax.minimum || ay.maximum <= ay.minimum) { rc = -EINVAL; goto bad; }
    info->x_min = ax.minimum; info->x_max = ax.maximum;
    info->y_min = ay.minimum; info->y_max = ay.maximum;
    info->xs[0] = ax.value; info->ys[0] = ay.value;
    if (info->protocol == 2) {
        struct input_absinfo slot;
        if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slot) < 0) { rc = -errno; goto bad; }
        if (slot.minimum != 0 || slot.maximum < 0 || slot.maximum >= AA_TOUCH_SLOTS) { rc = -E2BIG; goto bad; }
        info->slots = slot.maximum + 1;
    }
    rc = aa_touch_state(fd, info);
    if (rc < 0) goto bad;
    int clock = CLOCK_MONOTONIC;
    if (ioctl(fd, EVIOCSCLOCKID, &clock) < 0) { rc = -errno; goto bad; }
    return fd;
bad:
    close(fd); return rc;
}

int aa_touch_read(int fd, struct aa_touch_event *out)
{
    struct input_event event;
    ssize_t n = read(fd, &event, sizeof(event));
    if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -errno;
    if (n == 0) return -ENODEV;
    if (n != (ssize_t)sizeof(event)) return -EPROTO;
    out->type = event.type; out->code = event.code; out->value = event.value;
    out->micros = (uint64_t)event.time.tv_sec * 1000000 + event.time.tv_usec;
    return 1;
}
