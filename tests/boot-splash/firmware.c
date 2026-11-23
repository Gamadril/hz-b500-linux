/* Exercise actual firmware with the real board DTB and synthetic MMIO. */
#include <assert.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <libfdt.h>
#include <sbi/test.h>
#include "mmio.h"
static char uart_output[4096];
static size_t uart_size;
static void putc_uart(char c) { if (uart_size < sizeof(uart_output)) uart_output[uart_size++] = c; }
static struct sbi_console_device uart = {.name="uart", .console_putc=putc_uart};
static const struct sbi_console_device *console = &uart;
const struct sbi_console_device *sbi_console_get_device(void) {return console;}
void sbi_console_set_device(struct sbi_console_device *dev) {console=dev;}
void sbi_printf(const char *fmt, ...)
{
    char buf[512]; va_list a; va_start(a,fmt); vsnprintf(buf,sizeof(buf),fmt,a);va_end(a);
    for (char *p=buf; *p; ++p) console->console_putc(*p);
}
void sbi_timer_udelay(unsigned long n) {(void)n;}
void sbi_timer_mdelay(unsigned long n) {(void)n;}
static int fdt_node_is_enabled(const void *fdt,int node)
{
    const char *s=fdt_getprop(fdt,node,"status",NULL);
    return !s || !strcmp(s,"okay") || !strcmp(s,"ok");
}
#define HZ_B500_FB_TEST
#include "../../lichee/brandy-2.0/opensbi/platform/generic/allwinner/hz-b500-fb.c"
#include "../../lichee/brandy-2.0/opensbi/platform/generic/allwinner/hz-b500-lcd.c"
static char tree[200000], original[200000];
static void reset(void)
{
    memcpy(tree,original,sizeof(tree)); register_count=0;console=&uart;
    assert(!fdt_open_into(tree,tree,sizeof(tree)-2048));
    console_active=false;uart_size=0;
}
int main(int argc,char **argv)
{
    assert(argc==5);
    assert(mmap((void *)HZ_SPLASH_BASE,HZ_SPLASH_RESERVE,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void *)HZ_SPLASH_BASE);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(original,1,sizeof(original),f)>0);fclose(f);
    f=fopen(argv[2],"rb");assert(f);
    assert(fread((void *)HZ_SPLASH_META,1,512,f)==512);
    assert(fread((void *)HZ_SPLASH_BASE,1,HZ_SPLASH_BYTES,f)==HZ_SPLASH_BYTES);fclose(f);
    assert(hz_splash_crc("123456789",9)==0xcbf43926);
    unsigned crc=hz_splash_crc((void *)HZ_SPLASH_BASE,HZ_SPLASH_BYTES);
    reset();hz_b500_lcd_init(tree);
    assert(console==&uart && uart_size>0);
    assert(hz_splash_crc((void *)HZ_SPLASH_BASE,HZ_SPLASH_BYTES)==crc);
    int node=fdt_node_offset_by_compatible(tree,-1,"allwinner,sunxi-disp");
    assert(fdt_getprop(tree,node,"allwinner,opensbi-splash-active",NULL));
    assert(fdt32_to_cpu(*(fdt32_t *)fdt_getprop(tree,node,"boot_disp",NULL))==0x100);
    assert(readl((void *)0x02000130)&(1<<15));
    f=fopen(argv[3],"wb");assert(f);assert(fwrite(tree,1,fdt_totalsize(tree),f)==(size_t)fdt_totalsize(tree));fclose(f);
    f=fopen(argv[4],"wb");assert(f);assert(fwrite(registers,sizeof(registers[0]),register_count,f)==register_count);fclose(f);
    /* Corrupt pixels or headers must leave the backlight off and no handoff. */
    ((unsigned char *)HZ_SPLASH_BASE)[0]^=1;
    reset();hz_b500_lcd_init(tree);assert(!(readl((void *)0x02000130)&(1<<15)));
    assert(!fdt_getprop(tree,fdt_node_offset_by_compatible(tree,-1,"allwinner,sunxi-disp"),"allwinner,opensbi-splash-active",NULL));
    ((unsigned char *)HZ_SPLASH_BASE)[0]^=1;
    ((unsigned char *)HZ_SPLASH_META)[12]^=1;
    reset();hz_b500_lcd_init(tree);assert(!(readl((void *)0x02000130)&(1<<15)));
    /* Console mode still works, then permanently stops on native DE takeover. */
    reset();node=fdt_node_offset_by_compatible(tree,-1,"allwinner,sunxi-lcd0");
    assert(!fdt_delprop(tree,node,"allwinner,opensbi-splash"));
    assert(!fdt_setprop(tree,node,"allwinner,opensbi-fb-console",NULL,0));
    hz_b500_lcd_init(tree);assert(console!=&uart && console_active);
    console->console_putc('A');writel(0,(void *)0x05100000);
    crc=hz_splash_crc((void *)HZ_SPLASH_BASE,HZ_SPLASH_BYTES);
    console->console_putc('B');assert(!console_active);
    assert(hz_splash_crc((void *)HZ_SPLASH_BASE,HZ_SPLASH_BYTES)==crc);
    puts("PASS: OpenSBI preserves verified pixels, UART-only splash, corrupt-image fallback and console-mode takeover");
}
