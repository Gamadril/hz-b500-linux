#ifndef SBI_HOST_TEST_H
#define SBI_HOST_TEST_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8;
typedef uint32_t u32;
struct sbi_console_device {
    const char *name;
    void (*console_putc)(char);
    int (*console_getc)(void);
    int (*console_puts)(const char *, unsigned long);
};
const struct sbi_console_device *sbi_console_get_device(void);
void sbi_console_set_device(struct sbi_console_device *);
void sbi_printf(const char *, ...);
void sbi_timer_udelay(unsigned long);
void sbi_timer_mdelay(unsigned long);
u32 readl(const volatile void *);
void writel(u32, volatile void *);
#define sbi_snprintf snprintf
#define SBI_EINVAL (-3)
#define SBI_ENOTSUPP (-2)
#define SBI_ETIMEDOUT (-9)
#endif
