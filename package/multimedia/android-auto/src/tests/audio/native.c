#define _POSIX_C_SOURCE 200809L
#include <alsa/asoundlib.h>
#include <assert.h>
#define AA_ALSA_SYSTEM_HEADERS
static int fake_once(pthread_once_t *once, void (*fn)(void));
static int fake_clock(clockid_t id, struct timespec *t);
#define pthread_once fake_once
#define clock_gettime fake_clock
#include "../../native/alsa_sink.c"
#undef clock_gettime
#undef pthread_once

/* Check every public declaration against the actual installed ALSA headers. */
#define ABI(ret, name, args) _Static_assert(__builtin_types_compatible_p(__typeof__(api.name), __typeof__(&name)), #name);
ALSA_FUNCTIONS(ABI)
#undef ABI
_Static_assert(SND_PCM_STREAM_PLAYBACK == 0 && SND_PCM_FORMAT_S16_LE == 2 &&
               SND_PCM_ACCESS_RW_INTERLEAVED == 3 && SND_PCM_NONBLOCK == 1, "ALSA public constants");

static int64_t ticks;
static int closes, prepares, drops, waits, resumes, calls, drains;
static int config_error, params_error, recovery_error, stalled, suspended, drain_stalled;
static unsigned format_rate, format_channels;
static unsigned char written[32];
static size_t written_bytes;
static snd_pcm_sframes_t sequence[16];
static size_t sequence_len, sequence_at;
static int fake_once(pthread_once_t *once, void (*fn)(void)) { (void)once; (void)fn; return 0; }
static int fake_clock(clockid_t id, struct timespec *t)
{ assert(id == CLOCK_MONOTONIC); ++ticks; t->tv_sec = ticks / 1000; t->tv_nsec = ticks % 1000 * 1000000; return 0; }
static int fake_open(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode)
{ assert(!strcmp(name, "test")); assert(stream == SND_PCM_STREAM_PLAYBACK && mode == SND_PCM_NONBLOCK); *pcm = (void *)1; return 0; }
static int fake_config_top(snd_config_t **config) { *config = (void *)2; return 0; }
static int fake_input_open(snd_input_t **in, const char *path, const char *mode)
{ assert(!strcmp(path, "test.conf") && !strcmp(mode, "r")); *in = (void *)3; return config_error; }
static int fake_config_load(snd_config_t *config, snd_input_t *in)
{ assert(config == (void *)2 && in == (void *)3); return 0; }
static int fake_config_delete(snd_config_t *config) { assert(config == (void *)2); return 0; }
static int fake_input_close(snd_input_t *in) { assert(in == (void *)3); return 0; }
static int fake_open_lconf(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode, snd_config_t *config)
{ assert(config == (void *)2); return fake_open(pcm, name, stream, mode); }
static int fake_params(snd_pcm_t *pcm, snd_pcm_format_t format, snd_pcm_access_t access,
                       unsigned channels, unsigned rate, int resample, unsigned latency)
{ assert(pcm == (void *)1 && format == SND_PCM_FORMAT_S16_LE && access == SND_PCM_ACCESS_RW_INTERLEAVED); assert(resample && latency == 320000); format_rate = rate; format_channels = channels; return params_error; }
static snd_pcm_sframes_t fake_write(snd_pcm_t *pcm, const void *data, snd_pcm_uframes_t frames)
{
    assert(pcm == (void *)1); calls++;
    if (stalled) return -EAGAIN;
    if (suspended) return -ESTRPIPE;
    snd_pcm_sframes_t n = sequence_at < sequence_len ? sequence[sequence_at++] : (snd_pcm_sframes_t)frames;
    if (n > 0 && (unsigned long)n <= frames) {
        size_t bytes = (size_t)n * format_channels * 2;
        assert(written_bytes + bytes <= sizeof(written));
        memcpy(written + written_bytes, data, bytes); written_bytes += bytes;
    }
    return n;
}
static int fake_wait(snd_pcm_t *pcm, int timeout) { assert(pcm == (void *)1 && timeout == 20); waits++; ticks += timeout; return 1; }
static int fake_prepare(snd_pcm_t *pcm) { assert(pcm == (void *)1); prepares++; return recovery_error; }
static int fake_resume(snd_pcm_t *pcm) { assert(pcm == (void *)1); resumes++; return -EAGAIN; }
static int fake_drain(snd_pcm_t *pcm) { assert(pcm == (void *)1); drains++; return drain_stalled ? -EAGAIN : 0; }
static int fake_drop(snd_pcm_t *pcm) { assert(pcm == (void *)1); drops++; return 0; }
static int fake_close(snd_pcm_t *pcm) { assert(pcm == (void *)1); closes++; return 0; }
static const char *fake_strerror(int status) { (void)status; return "test error"; }

int main(void)
{
    api = (struct alsa_api){ fake_open, fake_open_lconf, fake_params, fake_write,
        fake_wait, fake_prepare, fake_resume, fake_drain, fake_drop, fake_close,
        fake_strerror, fake_config_top, fake_config_load, fake_config_delete,
        fake_input_open, fake_input_close };
    char error[256]; unsigned char data[16];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (unsigned char)i;
    void *s = aa_alsa_open("test", "test.conf", 48000, 2, error, sizeof(error));
    assert(s && format_rate == 48000 && format_channels == 2);
    assert(aa_alsa_write(s, data, 16, error, sizeof(error)) < 0 && !calls);
    assert(!aa_alsa_start(s, error, sizeof(error)));
    assert(aa_alsa_write(s, data, 3, error, sizeof(error)) < 0 && !calls);
    assert(aa_alsa_write(s, data, 65540, error, sizeof(error)) < 0 && !calls);
    snd_pcm_sframes_t results[] = { 1, -EINTR, -EAGAIN, -EPIPE, 2, 1 };
    memcpy(sequence, results, sizeof(results)); sequence_len = 6;
    assert(!aa_alsa_write(s, data, 16, error, sizeof(error)));
    assert(written_bytes == 16 && !memcmp(data, written, 16) && waits && prepares == 2);
    assert(!aa_alsa_stop(s, error, sizeof(error)) && drains == 1);
    assert(aa_alsa_write(s, data, 16, error, sizeof(error)) < 0);
    assert(!aa_alsa_start(s, error, sizeof(error)));
    stalled = 1;
    int64_t before = ticks;
    assert(aa_alsa_write(s, data, 16, error, sizeof(error)) < 0 && ticks - before < 550);
    stalled = 0; suspended = 1; before = ticks;
    assert(aa_alsa_write(s, data, 16, error, sizeof(error)) < 0 && resumes && ticks - before < 550);
    suspended = 0; drain_stalled = 1; before = ticks;
    assert(aa_alsa_stop(s, error, sizeof(error)) < 0 && ticks - before < 550);
    aa_alsa_close(s); assert(closes == 1);

    params_error = -EINVAL;
    assert(!aa_alsa_open("test", "", 16000, 1, error, sizeof(error)) && closes == 2);
    params_error = 0; config_error = -ENOENT;
    assert(!aa_alsa_open("test", "test.conf", 48000, 2, error, sizeof(error)) && closes == 2);
    config_error = 0;
    assert(!aa_alsa_open("test", "", 44100, 2, error, sizeof(error)));
    s = aa_alsa_open("test", "", 16000, 1, error, sizeof(error)); assert(s);
    assert(!aa_alsa_start(s, error, sizeof(error)));
    sequence_at = 0; sequence_len = 1; sequence[0] = -EPIPE; recovery_error = -EIO;
    assert(aa_alsa_write(s, data, 2, error, sizeof(error)) < 0);
    aa_alsa_close(s); assert(closes == 3);
    puts("PASS: ALSA public ABI, PCM formats, session control, partial writes, underrun/suspend recovery, bounded waits and cleanup");
}
