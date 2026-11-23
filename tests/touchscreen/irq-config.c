/* Test the actual IRQ-selection and config-display functions with host stubs.
 * No claim is made about hardware delivery or the kernel seq_file internals.
 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef int8_t s8;
typedef int32_t s32;
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IRQF_TRIGGER_RISING 1
#define IRQF_TRIGGER_FALLING 2
#define IRQF_TRIGGER_HIGH 4
#define IRQF_TRIGGER_LOW 8
#define GTP_CONFIG_MAX_LENGTH 240
#define CLOCK_MONOTONIC 1
#define HRTIMER_MODE_REL 0
#define GTP_ERROR(...) ((void)0)

struct timer { int (*function)(void); bool initialized, started; };
struct goodix_ts_data {
    unsigned int int_trigger_type;
    int use_irq;
    struct timer timer;
};
struct { struct { int gpio; } irq_gpio; int input_type; } config_info;
struct seq_file { char buffer[4096]; size_t length; };
static void *i2c_connect_client;
static u8 config[GTP_CONFIG_MAX_LENGTH + 2];
static int transfer_result = 2, direction_result, request_result;
static unsigned long requested_flags;
static int requests, directions, disables, syncs;

static void seq_printf(struct seq_file *m, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(m->buffer + m->length, sizeof(m->buffer) - m->length,
                      format, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(m->buffer) - m->length);
    m->length += n;
}
#define seq_puts(m, s) seq_printf(m, "%s", s)
#define seq_putc(m, c) seq_printf(m, "%c", c)
static int gtp_i2c_read(void *client, char *bytes, int length)
{
    assert((u8)bytes[0] == 0x80 && (u8)bytes[1] == 0x47);
    assert(length == GTP_CONFIG_MAX_LENGTH + 2);
    for (int i = 2; i < length; i++)
        bytes[i] = (char)(i + 128);
    return transfer_result;
}
static int gpio_direction_input(int gpio)
{
    assert(gpio == config_info.irq_gpio.gpio);
    directions++;
    return direction_result;
}
static int goodix_ts_irq_handler(void) { return 0; }
static int goodix_ts_timer_handler(void) { return 0; }
static int input_request_int(int *type, int (*handler)(void),
                             unsigned long flags, struct goodix_ts_data *ts)
{
    assert(type == &config_info.input_type && handler == goodix_ts_irq_handler);
    assert(directions == 1 && !direction_result);
    requests++;
    requested_flags = flags;
    return request_result;
}
static void gtp_irq_disable(struct goodix_ts_data *ts) { disables++; }
static void gtp_set_int_value(int value) { assert(value == 0); syncs++; }
static void hrtimer_init(struct timer *timer, int clock, int mode)
{
    assert(clock == CLOCK_MONOTONIC && mode == HRTIMER_MODE_REL);
    timer->initialized = true;
}
static long ktime_set(long seconds, long nanos) { return seconds; }
static void hrtimer_start(struct timer *timer, long time, int mode)
{
    assert(timer->initialized && timer->function == goodix_ts_timer_handler);
    assert(time == 1 && mode == HRTIMER_MODE_REL);
    timer->started = true;
}

#include "functions.inc"

static void reset(void)
{
    request_result = direction_result = 0;
    directions = requests = disables = syncs = 0;
}
int main(void)
{
    const unsigned long expected[] = { 1, 2, 8, 4 };
    config_info.irq_gpio.gpio = 138; /* PE10 in the BSP GPIO numbering. */
    for (unsigned int mode = 0; mode < 4; mode++) {
        struct goodix_ts_data ts = { .int_trigger_type = mode };
        reset();
        assert(gtp_request_irq(&ts) == 0);
        assert(requested_flags == expected[mode] && requests == 1);
        assert(ts.use_irq && disables == 1 && !ts.timer.started);
    }
    for (int scenario = 0; scenario < 3; scenario++) {
        struct goodix_ts_data ts = { .int_trigger_type = 1 };
        reset();
        if (scenario == 0) request_result = -EBUSY;
        if (scenario == 1) direction_result = -EIO;
        if (scenario == 2) ts.int_trigger_type = 255;
        assert(gtp_request_irq(&ts) < 0);
        assert(!ts.use_irq && !disables && syncs == 1);
        assert(ts.timer.started);
        if (scenario) assert(!requests);
    }

    struct seq_file output = { 0 };
    for (unsigned int i = 0; i < sizeof(config); i++) config[i] = (u8)i;
    assert(gt91xx_config_show(&output, NULL) == 0);
    assert(strstr(output.buffer, "config init value"));
    assert(strstr(output.buffer, "config real value"));
    assert(strstr(output.buffer, "0xFF "));
    assert(!strstr(output.buffer, "FFFFFF"));
    int bytes = 0;
    for (char *p = output.buffer; (p = strstr(p, "0x")); p += 2) bytes++;
    assert(bytes == 480);
    const int failures[] = { -EIO, 0, 1 };
    for (unsigned int i = 0; i < ARRAY_SIZE(failures); i++) {
        memset(&output, 0, sizeof(output));
        transfer_result = failures[i];
        assert(gt91xx_config_show(&output, NULL) == -EIO);
        assert(!output.length);
    }
    puts("IRQ trigger modes/failure fallback and config display/error checks passed");
}
