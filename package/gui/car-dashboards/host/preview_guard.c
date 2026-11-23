/* Disable the remaining RK board/media effects. This is not a sandbox. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode)
{
    if (!strncmp(path, "/dev/", 5) || !strncmp(path, "/sys/", 5) ||
        !strcmp(path, "/tmp/mplayer_fifo")) {
        fprintf(stderr, "[preview] board/media path disabled: %s\n", path);
        errno = EPERM;
        return NULL;
    }
    return __real_fopen(path, mode);
}

int __wrap_system(const char *command)
{
    if (command) fprintf(stderr, "[preview] external command disabled: %s\n", command);
    return 0;
}
