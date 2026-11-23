/* Register model shared by the firmware writer and kernel adoption reader. */
struct reg { uintptr_t addr; uint32_t value; };
static struct reg registers[256];
static unsigned register_count;
uint32_t readl(const volatile void *ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    for (unsigned i = 0; i < register_count; ++i)
        if (registers[i].addr == addr) return registers[i].value;
    return 0;
}
void writel(uint32_t value, volatile void *ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    if (addr == 0x02001040 && (value & (1U << 31))) value |= 1U << 28;
    for (unsigned i = 0; i < register_count; ++i)
        if (registers[i].addr == addr) { registers[i].value = value; return; }
    assert(register_count < 256);
    registers[register_count++] = (struct reg){addr, value};
}
