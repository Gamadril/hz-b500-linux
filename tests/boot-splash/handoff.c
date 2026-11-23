/* Feed the actual kernel adoption function the state emitted by firmware.c. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libfdt.h>
#include "mmio.h"
typedef uint32_t u32;
#define __iomem
#define BIT(n) (1U<<(n))
#define EINVAL 22
#define IOMMU_READ 1
#define DISP_MOD_DE 0
#define DISP_MOD_LCD0 1
#define HZ_B500_HANDOFF_TEST
struct device_node {int offset;};
struct device {struct device_node *of_node;};
struct platform_device {struct device dev;};
struct resource {unsigned long start,end;};
struct iommu_domain {int unused;};
static struct {uintptr_t reg_base[2];} g_disp_drv={{0x05000000,0x05461000}};
static struct iommu_domain domain;
static unsigned reserve_calls,map_calls,node_count;
static int reserve_error,map_error;
static char tree[200000];
static struct device_node nodes[64];
#define dev_warn(...) ((void)0)
#define dev_info(...) ((void)0)
#define pr_info(...) ((void)0)
static bool of_property_read_bool(struct device_node *n,const char *s)
{return fdt_getprop(tree,n->offset,s,NULL)!=NULL;}
static int of_property_read_string(struct device_node *n,const char *s,const char **v)
{*v=fdt_getprop(tree,n->offset,s,NULL);return *v?0:-1;}
static int of_property_read_u32(struct device_node *n,const char *s,u32 *v)
{
    const fdt32_t *p=fdt_getprop(tree,n->offset,s,NULL);
    if(!p)return -1;*v=fdt32_to_cpu(*p);return 0;
}
static struct device_node *node(int offset)
{
    if(offset<0)return NULL;assert(node_count<64);
    nodes[node_count].offset=offset;return &nodes[node_count++];
}
static struct device_node *of_find_node_by_path(const char *s) {return node(fdt_path_offset(tree,s));}
static struct device_node *of_find_compatible_node(void *a,void *b,const char *s)
{(void)a;(void)b;return node(fdt_node_offset_by_compatible(tree,-1,s));}
static void of_node_put(struct device_node *n) {(void)n;}
static int of_address_to_resource(struct device_node *n,int i,struct resource *r)
{
    (void)i;const fdt32_t *p=fdt_getprop(tree,n->offset,"reg",NULL);
    if(!p)return -1;r->start=fdt32_to_cpu(p[1]);r->end=r->start+fdt32_to_cpu(p[3])-1;return 0;
}
static unsigned long resource_size(struct resource *r){return r->end-r->start+1;}
static void *ioremap(unsigned long p,unsigned long size){(void)size;return(void *)p;}
static void iounmap(void *p){(void)p;}
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d){(void)d;return &domain;}
static int iommu_dma_reserve_iova(struct device *d,unsigned long addr,unsigned long size)
{(void)d;assert(addr==0x43c00000 && size==0x300000);++reserve_calls;return reserve_error;}
static int iommu_map(struct iommu_domain *d,unsigned long iova,unsigned long pa,unsigned long size,int prot)
{
    assert(d==&domain && iova==0x43c00000 && pa==iova && size==0x300000 && prot==IOMMU_READ);
    assert(reserve_calls);++map_calls;return map_error;
}
#include "../../lichee/linux-5.4/drivers/video/fbdev/sunxi/disp2/disp/hz-b500-handoff.c"
int main(int argc,char **argv)
{
    assert(argc==3);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(tree,1,sizeof(tree),f)>0);fclose(f);
    f=fopen(argv[2],"rb");assert(f);register_count=fread(registers,sizeof(registers[0]),256,f);fclose(f);
    struct platform_device p={.dev={.of_node=node(fdt_node_offset_by_compatible(tree,-1,"allwinner,sunxi-disp"))}};
    assert(hz_b500_prepare_handoff(&p));assert(reserve_calls==1 && map_calls==1);
    assert(hz_b500_adopting_display());hz_b500_handoff_complete();assert(!hz_b500_adopting_display());
    /* The handoff reader must reject an incompatible stride or running clock. */
    writel(4092,(void *)0x0510300c);
    assert(!hz_b500_prepare_handoff(&p));assert(reserve_calls==1);
    writel(4096,(void *)0x0510300c);writel(0x80000000,(void *)0x02001600);
    assert(!hz_b500_prepare_handoff(&p));assert(reserve_calls==1);
    writel(0x81000003,(void *)0x02001600);
    reserve_error=-1;assert(!hz_b500_prepare_handoff(&p));assert(map_calls==1);
    reserve_error=0;map_error=-1;assert(!hz_b500_prepare_handoff(&p));assert(!hz_b500_adopting_display());
    assert(!fdt_delprop(tree,p.dev.of_node->offset,"allwinner,opensbi-splash-active"));
    unsigned calls=reserve_calls;assert(!hz_b500_prepare_handoff(&p));assert(reserve_calls==calls);
    puts("PASS: kernel adopts real firmware state; rejects clock/stride mismatches and IOVA/map failures before smooth takeover");
}
