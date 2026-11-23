#ifndef AA_CEDAR_SINK_H
#define AA_CEDAR_SINK_H
#include <stddef.h>
#include <stdint.h>

/* The C boundary uses SDK headers internally; Rust never reproduces vendor structs. */
struct aa_cedar;
struct aa_cedar_config {
    unsigned screen, channel, layer, width, height, vbv_bytes, timeout_ms;
    /* Total horizontal/vertical margins inside the stream (AA VideoConfig);
     * only the centred remainder is shown, scaled to fit the screen. */
    unsigned margin_width, margin_height;
};
struct aa_cedar *aa_cedar_open(const struct aa_cedar_config *, char *, size_t);
int aa_cedar_push(struct aa_cedar *, const uint8_t *, size_t, int64_t, char *, size_t);
int aa_cedar_set_visible(struct aa_cedar *, int, char *, size_t);
/* On failure the context remains allocated: display buffers may still be in use. */
int aa_cedar_close(struct aa_cedar *, char *, size_t);
/* Read display dimensions without acquiring a decoder or display layer. */
int aa_cedar_display_size(unsigned, uint32_t *, uint32_t *, char *, size_t);
#endif
