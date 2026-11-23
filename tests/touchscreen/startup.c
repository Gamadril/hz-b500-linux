/* Actual driver helpers with GPIO/I2C/clock stubs; not a hardware timing test. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
struct i2c_client { int addr; };
struct { struct { int gpio; } irq_gpio, wakeup_gpio; } config_info;
#define GTP_ADDR_LENGTH 2
#define msecs_to_jiffies(ms) ((unsigned long)(ms))
#define time_before(a, b) ((long)((a) - (b)) < 0)
static unsigned long jiffies;
static int direction_calls, fail_direction, inputs, resets, settle_ms;
static bool int_is_input, reset_held;
static int reads, ready_after, transfer_result, sleeps;
static u8 ready_status;

static int gpio_direction_output(int gpio, int value)
{
    assert(value == 0);
    assert(gpio == config_info.irq_gpio.gpio ||
           gpio == config_info.wakeup_gpio.gpio);
    if (++direction_calls == fail_direction) return -EBUSY;
    if (gpio == config_info.irq_gpio.gpio) int_is_input = false;
    if (gpio == config_info.wakeup_gpio.gpio) reset_held = true;
    return 0;
}
static int gpio_direction_input(int gpio)
{
    assert(gpio == config_info.irq_gpio.gpio);
    inputs++;
    if (++direction_calls == fail_direction) return -EBUSY;
    int_is_input = true;
    return 0;
}
static void gtp_reset_guitar(struct i2c_client *client, int delay)
{
    assert(client->addr == 0x5d || client->addr == 0x14);
    assert(reset_held && !int_is_input && delay >= 20);
    resets++;
    reset_held = false;
}
static void msleep(int ms)
{
    assert(int_is_input && !reset_held);
    settle_ms += ms;
}
static void usleep_range(int low, int high)
{
    assert(low == 1000 && high == 2000);
    sleeps++;
    jiffies += 2;
}
static int gtp_i2c_read(struct i2c_client *client, u8 *data, int length)
{
    assert(client->addr == 0x5d && length == 12);
    assert(data[0] == 0x81 && data[1] == 0x4e);
    reads++;
    data[2] = ready_after && reads >= ready_after ? ready_status : 0;
    return transfer_result;
}

#include "startup-functions.inc"

static void reset(void)
{
    direction_calls = fail_direction = inputs = resets = settle_ms = 0;
    int_is_input = true;
    reset_held = false;
    jiffies = 0;
    reads = sleeps = 0;
    ready_after = 1;
    ready_status = 0x81;
    transfer_result = 2;
}
int main(void)
{
    struct i2c_client client = { .addr = 0x5d };
    config_info.irq_gpio.gpio = 138;
    config_info.wakeup_gpio.gpio = 139;
    reset();
    assert(gtp_power_on_reset(&client) == 0);
    assert(int_is_input && resets == 1 && inputs == 1 && settle_ms == 20);
    for (int failure = 1; failure <= 3; failure++) {
        reset();
        fail_direction = failure;
        assert(gtp_power_on_reset(&client) < 0);
        assert(settle_ms == 0);
        if (failure == 2) assert(int_is_input && inputs == 1 && !resets);
    }

    u8 data[12] = { 0x81, 0x4e };
    reset();
    assert(gtp_read_ready_report(&client, data, sizeof(data)) == 0);
    assert(reads == 1 && !sleeps && data[2] == 0x81);

    reset();
    ready_after = 6;
    assert(gtp_read_ready_report(&client, data, sizeof(data)) == 0);
    assert(reads == 6 && sleeps == 5 && data[2] == 0x81);

    reset();
    ready_status = 0x80; /* A fresh zero-contact report is a valid release. */
    assert(gtp_read_ready_report(&client, data, sizeof(data)) == 0);
    assert(data[2] == 0x80);

    reset();
    ready_after = 0;
    assert(gtp_read_ready_report(&client, data, sizeof(data)) == -ENOMSG);
    assert(reads == 10 && jiffies == 20); /* Bounded, not indefinite polling. */

    reset();
    jiffies = ~0UL - 9; /* Also bound the wait across a timer wrap. */
    ready_after = 0;
    assert(gtp_read_ready_report(&client, data, sizeof(data)) == -ENOMSG);
    assert(reads == 10 && sleeps == 10);

    const int errors[] = { -ETIMEDOUT, 0, 1 };
    for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        reset();
        transfer_result = errors[i];
        assert(gtp_read_ready_report(&client, data, sizeof(data)) ==
               (errors[i] < 0 ? errors[i] : -EIO));
        assert(reads == 1 && sleeps == 0);
    }
    puts("Power-on INT release/error handling and ready-report timing checks passed");
}
