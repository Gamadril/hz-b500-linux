#define _POSIX_C_SOURCE 200809L
#include <alsa/asoundlib.h>
#include <assert.h>
#include "../../native/alsa_sink.h"
int main(int argc, char **argv)
{
    assert(argc == 2);
    snd_config_t *config; snd_input_t *input;
    assert(!snd_config_top(&config));
    assert(!snd_input_stdio_open(&input, argv[1], "r"));
    assert(!snd_config_load(config, input));
    snd_config_t *pcm;
    assert(!snd_config_search(config, "pcm.android_auto", &pcm));
    assert(!snd_config_search(config, "pcm.android_auto_mix.slave.rate", &pcm));
    long rate; assert(!snd_config_get_integer(pcm, &rate) && rate == 48000);
    snd_input_close(input); snd_config_delete(config);
    for (int i = 0; i < 3; i++) {
        char error[256]; unsigned char silence[1920] = {0};
        void *sink = aa_alsa_open("null", "", i ? 16000 : 48000, i ? 1 : 2, error, sizeof(error));
        if (!sink) { fprintf(stderr, "%s\n", error); return 1; }
        assert(!aa_alsa_start(sink, error, sizeof(error)));
        assert(!aa_alsa_write(sink, silence, sizeof(silence), error, sizeof(error)));
        assert(!aa_alsa_stop(sink, error, sizeof(error)));
        assert(!aa_alsa_start(sink, error, sizeof(error)));
        aa_alsa_close(sink);
    }
    puts("PASS: packaged ALSA configuration and real ALSA null PCM for all three audio formats");
}
