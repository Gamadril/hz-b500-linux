/* Actual pinctrl clock-selection and bank-address helpers with host stubs. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;
struct clk { unsigned long rate; };
struct device_node { unsigned int debounce[6]; bool property; };
struct sunxi_pinctrl_desc { unsigned int irq_banks; const unsigned int *irq_bank_map; };
struct sunxi_pinctrl { void *dev; const struct sunxi_pinctrl_desc *desc; u8 *membase; };
#define BUG_ON(condition) assert(!(condition))
#define IS_ERR(pointer) (!(pointer))
#define PTR_ERR(pointer) (-ENODEV)
#define USEC_PER_SEC 1000000
#define DIV_ROUND_CLOSEST(a, b) (((a) + (b) / 2) / (b))
/* The kernel abs macro converts unsigned-long input to signed long first. */
#undef abs
#define abs(value) labs((long)(value))
static unsigned int writes, offset_written;
static u32 value_written;
static u8 registers[0x500];
static struct clk hosc = { 24000000 }, losc = { 32768 };
static unsigned long clk_get_rate(struct clk *clk) { return clk->rate; }
static struct clk *devm_clk_get(void *dev, const char *name)
{
    if (!strcmp(name, "hosc")) return &hosc;
    assert(!strcmp(name, "losc")); return &losc;
}
static int of_clk_get_parent_count(struct device_node *node) { return 3; }
static void *of_find_property(struct device_node *node, const char *name, void *unused)
{
    assert(!strcmp(name, "input-debounce"));
    return node->property ? node : NULL;
}
static int of_property_read_u32_index(struct device_node *node, const char *name,
                                      unsigned int index, u32 *value)
{
    assert(!strcmp(name, "input-debounce") && index < 6);
    *value = node->debounce[index]; return 0;
}
static void writel(u32 value, u8 *address)
{
    writes++;
    assert(address >= registers && address + sizeof(value) <= registers + sizeof(registers));
    offset_written = address - registers;
    value_written = value;
}

#include "clock-registers.inc"
#include "clock-functions.inc"

int main(void)
{
    const unsigned int bank_map[] = { 1, 2, 3, 4, 5, 6 };
    struct sunxi_pinctrl_desc desc = { 6, bank_map };
    struct sunxi_pinctrl pctl = { .desc = &desc, .membase = registers };
    struct device_node node = { .debounce = { 0, 0, 0, 1, 0, 0 }, .property = true };
    assert(!sunxi_pinctrl_setup_debounce(&pctl, &node));
    assert(writes == 1 && offset_written == 0x298 && value_written == 0x51);
    /* An absent/disabled LOSC must not change the PE selection. */
    losc.rate = 0;
    writes = 0;
    assert(!sunxi_pinctrl_setup_debounce(&pctl, &node));
    assert(writes == 1 && offset_written == 0x298 && value_written == 0x51);
    node.property = false;
    writes = 0;
    assert(!sunxi_pinctrl_setup_debounce(&pctl, &node) && !writes);
    node.property = true;
    memset(node.debounce, 0, sizeof(node.debounce));
    assert(!sunxi_pinctrl_setup_debounce(&pctl, &node) && !writes);
    puts("PE sampling selects HOSC / 32 at 0x298; other IRQ banks stay unchanged");
}
