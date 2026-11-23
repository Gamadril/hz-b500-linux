/* SPDX-License-Identifier: LGPL-3.0-or-later */
#define _GNU_SOURCE
#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
uint64_t aa_media_now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
int aa_media_open(const char *path) {
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0) return -errno;
    char name[128]={0};
    if(ioctl(fd,EVIOCGNAME(sizeof(name)),name)<0 || strcmp(name,"HZ-B500 vehicle media")) {
        close(fd);return -ENODEV;
    }
    int clock_id = CLOCK_MONOTONIC;
    if (ioctl(fd, EVIOCSCLOCKID, &clock_id)) { int e = errno; close(fd); return -e; }
    return fd;
}
/* Reuse the scalar event ABI and native reader from touch_input.c. */
