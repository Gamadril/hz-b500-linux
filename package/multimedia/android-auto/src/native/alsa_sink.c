#define _POSIX_C_SOURCE 200809L
#include "alsa_sink.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Stable public ALSA C ABI: opaque objects and scalar parameters only. This
 * keeps a standalone cross-build independent of installed ALSA development
 * files. The host regression compiles this same code with real ALSA headers. */
#ifndef AA_ALSA_SYSTEM_HEADERS
typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_config snd_config_t;
typedef struct _snd_input snd_input_t;
typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;
typedef enum { SND_PCM_STREAM_PLAYBACK = 0 } snd_pcm_stream_t;
typedef enum { SND_PCM_FORMAT_S16_LE = 2 } snd_pcm_format_t;
typedef enum { SND_PCM_ACCESS_RW_INTERLEAVED = 3 } snd_pcm_access_t;
#define SND_PCM_NONBLOCK 1
#endif

#define ALSA_FUNCTIONS(X) \
 X(int, snd_pcm_open, (snd_pcm_t **, const char *, snd_pcm_stream_t, int)) \
 X(int, snd_pcm_open_lconf, (snd_pcm_t **, const char *, snd_pcm_stream_t, int, snd_config_t *)) \
 X(int, snd_pcm_set_params, (snd_pcm_t *, snd_pcm_format_t, snd_pcm_access_t, unsigned, unsigned, int, unsigned)) \
 X(snd_pcm_sframes_t, snd_pcm_writei, (snd_pcm_t *, const void *, snd_pcm_uframes_t)) \
 X(int, snd_pcm_wait, (snd_pcm_t *, int)) \
 X(int, snd_pcm_prepare, (snd_pcm_t *)) \
 X(int, snd_pcm_resume, (snd_pcm_t *)) \
 X(int, snd_pcm_drain, (snd_pcm_t *)) \
 X(int, snd_pcm_drop, (snd_pcm_t *)) \
 X(int, snd_pcm_close, (snd_pcm_t *)) \
 X(const char *, snd_strerror, (int)) \
 X(int, snd_config_top, (snd_config_t **)) \
 X(int, snd_config_load, (snd_config_t *, snd_input_t *)) \
 X(int, snd_config_delete, (snd_config_t *)) \
 X(int, snd_input_stdio_open, (snd_input_t **, const char *, const char *)) \
 X(int, snd_input_close, (snd_input_t *))

struct alsa_api {
#define FIELD(ret, name, args) ret (*name) args;
    ALSA_FUNCTIONS(FIELD)
#undef FIELD
};
static struct alsa_api api;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static char load_error[256];

static void load_alsa(void)
{
    /* Kept for process lifetime; workers may use the function pointers. */
    void *lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!lib) { snprintf(load_error, sizeof(load_error), "%s", dlerror()); return; }
#define LOAD(ret, name, args) do { \
    *(void **)(&api.name) = dlsym(lib, #name); \
    if (!api.name) { snprintf(load_error, sizeof(load_error), "missing %s", #name); return; } \
} while (0);
    ALSA_FUNCTIONS(LOAD)
#undef LOAD
}

struct sink { snd_pcm_t *pcm; unsigned frame_bytes; int running; };

static int fail(char *error, size_t size, const char *operation, int status)
{
    snprintf(error, size, "%s: %s", operation, api.snd_strerror(status));
    return -1;
}

static int64_t milliseconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void *aa_alsa_open(const char *device, const char *path, unsigned rate,
                   unsigned channels, char *error, size_t size)
{
    if (!device || !*device || !((rate == 48000 && channels == 2) ||
                                (rate == 16000 && channels == 1))) {
        snprintf(error, size, "invalid PCM device or format"); return NULL;
    }
    pthread_once(&once, load_alsa);
    if (*load_error) { snprintf(error, size, "ALSA: %s", load_error); return NULL; }
    struct sink *s = calloc(1, sizeof(*s));
    if (!s) { snprintf(error, size, "ALSA allocation failed"); return NULL; }
    snd_config_t *config = NULL;
    snd_input_t *input = NULL;
    int rc;
    if (path && *path) {
        rc = api.snd_config_top(&config);
        if (rc < 0) goto failed;
        rc = api.snd_input_stdio_open(&input, path, "r");
        if (rc < 0) goto failed;
        rc = api.snd_config_load(config, input);
        api.snd_input_close(input); input = NULL;
        if (rc < 0) goto failed;
        rc = api.snd_pcm_open_lconf(&s->pcm, device, SND_PCM_STREAM_PLAYBACK,
                                    SND_PCM_NONBLOCK, config);
    } else {
        rc = api.snd_pcm_open(&s->pcm, device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    }
    if (rc < 0) goto failed;
    /* 320 ms ALSA buffer (the board dmix maximum): the single-threaded receiver
     * stalls for tens of ms (TLS, video, USB) on the one-core F133; 80 ms
     * underran audibly with music.
     * Writes/drain keep a separate 500 ms progress deadline. */
    rc = api.snd_pcm_set_params(s->pcm, SND_PCM_FORMAT_S16_LE,
                                SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 1, 320000);
    if (rc < 0) goto failed;
    if (config) api.snd_config_delete(config);
    s->frame_bytes = channels * 2;
    return s;
failed:
    fail(error, size, "open/configure ALSA PCM", rc);
    if (input) api.snd_input_close(input);
    if (config) api.snd_config_delete(config);
    if (s->pcm) api.snd_pcm_close(s->pcm);
    free(s);
    return NULL;
}

int aa_alsa_start(void *sink, char *error, size_t size)
{
    struct sink *s = sink;
    s->running = 0;
    int rc = api.snd_pcm_drop(s->pcm);
    if (rc >= 0) rc = api.snd_pcm_prepare(s->pcm);
    if (rc < 0) return fail(error, size, "start ALSA PCM", rc);
    s->running = 1;
    return 0;
}

int aa_alsa_write(void *sink, const unsigned char *data, size_t len,
                  char *error, size_t size)
{
    struct sink *s = sink;
    if (!s->running || !len || len > 65536 || len % s->frame_bytes) {
        snprintf(error, size, "PCM packet outside active stream, unaligned or oversized");
        return -1;
    }
    int64_t deadline = milliseconds() + 500;
    while (len) {
        if (milliseconds() >= deadline) return fail(error, size, "ALSA write timeout", -ETIMEDOUT);
        snd_pcm_sframes_t n = api.snd_pcm_writei(s->pcm, data, len / s->frame_bytes);
        if (n > 0) {
            if ((size_t)n > len / s->frame_bytes) return fail(error, size, "ALSA invalid write count", -EIO);
            size_t bytes = (size_t)n * s->frame_bytes;
            data += bytes; len -= bytes;
            continue;
        }
        if (n == -EINTR) continue;
        if (n == -EPIPE) {
            int rc = api.snd_pcm_prepare(s->pcm);
            if (rc < 0) return fail(error, size, "recover ALSA underrun", rc);
            continue;
        }
        if (n == -ESTRPIPE) {
            int rc = api.snd_pcm_resume(s->pcm);
            if (rc == -EAGAIN) { /* Resume is retried within the same deadline. */ }
            else if (rc < 0) {
                rc = api.snd_pcm_prepare(s->pcm);
                if (rc < 0) return fail(error, size, "recover suspended ALSA PCM", rc);
                continue;
            } else continue;
        } else if (n < 0 && n != -EAGAIN) return fail(error, size, "write ALSA PCM", (int)n);
        int rc = api.snd_pcm_wait(s->pcm, 20);
        if (rc < 0 && rc != -EINTR && rc != -EPIPE && rc != -ESTRPIPE)
            return fail(error, size, "wait ALSA PCM", rc);
    }
    return 0;
}

int aa_alsa_stop(void *sink, char *error, size_t size)
{
    struct sink *s = sink;
    if (!s->running) return 0;
    s->running = 0;
    int64_t deadline = milliseconds() + 500;
    int rc;
    while ((rc = api.snd_pcm_drain(s->pcm)) == -EAGAIN || rc == -EINTR) {
        if (milliseconds() >= deadline) {
            api.snd_pcm_drop(s->pcm);
            return fail(error, size, "ALSA drain timeout", -ETIMEDOUT);
        }
        /* A draining PCM can report POLLERR; the next drain decides its state. */
        api.snd_pcm_wait(s->pcm, 20);
    }
    return rc < 0 ? fail(error, size, "drain ALSA PCM", rc) : 0;
}

void aa_alsa_close(void *sink)
{
    struct sink *s = sink;
    api.snd_pcm_drop(s->pcm);
    api.snd_pcm_close(s->pcm);
    free(s);
}
