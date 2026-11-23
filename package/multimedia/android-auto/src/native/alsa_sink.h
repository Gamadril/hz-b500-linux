#ifndef AA_ALSA_SINK_H
#define AA_ALSA_SINK_H
#include <stddef.h>
void *aa_alsa_open(const char *device, const char *config, unsigned rate,
                   unsigned channels, char *error, size_t error_size);
int aa_alsa_start(void *sink, char *error, size_t error_size);
int aa_alsa_write(void *sink, const unsigned char *data, size_t len,
                  char *error, size_t error_size);
int aa_alsa_stop(void *sink, char *error, size_t error_size);
void aa_alsa_close(void *sink);
#endif
