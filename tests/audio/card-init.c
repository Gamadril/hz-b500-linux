/* Exercise the actual machine-driver initialization without hardware. */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define SND_JACK_HEADSET 1
#define SND_JACK_HEADPHONE 2
#define SND_JACK_BTN_0 4
#define SND_JACK_BTN_1 8
#define SND_JACK_BTN_2 16
#define SND_JACK_BTN_3 32
#define KEY_MEDIA 1
#define KEY_VOLUMEUP 2
#define KEY_VOLUMEDOWN 3
#define KEY_VOICECOMMAND 4
#define LOG_ERR(...) ((void)0)
#define LOG_WARN(...) ((void)0)
struct snd_soc_dapm_context { int unused; };
struct snd_soc_component { struct snd_soc_dapm_context dapm; };
struct device { void *of_node; };
struct snd_soc_card { struct device *dev; };
struct codec_dai { struct snd_soc_component *component; };
struct snd_soc_pcm_runtime { struct codec_dai *codec_dai; struct snd_soc_card *card; };
struct sunxi_card_priv { struct snd_soc_component *component; struct { void *jack; } jack; };
static struct sunxi_card_priv priv;
static bool fixed;
static int jack_calls, jack_error, sync_error;
static unsigned enabled, disabled;
static unsigned pin(const char *name)
{
    if (!strcmp(name, "HPOUTL")) return 1;
    if (!strcmp(name, "HPOUTR")) return 2;
    if (!strcmp(name, "Headphone")) return 4;
    if (!strcmp(name, "LINEOUT")) return 8;
    if (!strcmp(name, "HpSpeaker")) return 16;
    assert(0); return 0;
}
static struct sunxi_card_priv *snd_soc_card_get_drvdata(struct snd_soc_card *c) { return &priv; }
static bool of_property_read_bool(void *n, const char *p)
{
    assert(!strcmp(p, "allwinner,fixed-headphone-output")); return fixed;
}
static int snd_soc_dapm_disable_pin(struct snd_soc_dapm_context *d, const char *p)
{ disabled |= pin(p); return 0; }
static int snd_soc_dapm_enable_pin(struct snd_soc_dapm_context *d, const char *p)
{ enabled |= pin(p); return 0; }
static int snd_soc_dapm_sync(struct snd_soc_dapm_context *d) { return sync_error; }
static int snd_soc_card_jack_new(struct snd_soc_card *c, const char *name, int type,
                                void *jack, void *pins, int count)
{ jack_calls++; return jack_error; }
static void snd_jack_set_key(void *j, int button, int key) {}
#include "card-init.inc"
int main(void)
{
    struct device dev = {0};
    struct snd_soc_card card = {&dev};
    struct snd_soc_component component = {0};
    struct codec_dai dai = {&component};
    struct snd_soc_pcm_runtime rtd = {&dai, &card};
    fixed = true;
    assert(sunxi_card_init(&rtd) == 0);
    assert(priv.component == &component);
    assert(enabled == 7 && disabled == 24 && jack_calls == 0);
    sync_error = -5;
    assert(sunxi_card_init(&rtd) == -5);
    fixed = false; sync_error = 0; enabled = disabled = 0;
    assert(sunxi_card_init(&rtd) == 0);
    assert(enabled == 0 && disabled == 31 && jack_calls == 1);
    jack_error = -7;
    assert(sunxi_card_init(&rtd) == -7);
    puts("PASS: fixed stereo path, no jack creation, sync errors, legacy route/errors");
    return 0;
}
