/* Run the production mapping/clock functions with mocked kernel resources. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ve_mem_list.h"
struct file { int id; };
struct dma_buf { int refs; };
struct dma_buf_attachment { struct dma_buf *buffer; };
struct scatterlist { unsigned long dma_address; };
struct sg_table { struct scatterlist *sgl; int nents; };
struct dma_buf_info {
    struct aw_mem_list_head i_list;
    int fd;
    unsigned long addr;
    struct dma_buf *dma_buf;
    struct dma_buf_attachment *attachment;
    struct sg_table *sgt;
    int p_id;
    struct file *filp;
};
struct cedar_dev {
    struct aw_mem_list_head list;
    int lock_mem, lock;
    int bMemDevAttachFlag;
    void *plat_dev, *reset, *bus_clk, *mbus_clk, *ve_clk;
};
static struct cedar_dev device, *cedar_devp = &device;
static struct { int tgid; } task = {123}, *current = &task;
static int clk_status, failure, attachments, mappings, clocks;
static struct dma_buf buffers[8];
#define GFP_KERNEL 0
#define DMA_BIDIRECTIONAL 0
#define PRINTK_IOMMU_ADDR 0
#define VE_LOGD(...) ((void)0)
#define VE_LOGI(...) ((void)0)
#define VE_LOGW(...) ((void)0)
#define VE_LOGE(...) ((void)0)
#define IS_ERR_OR_NULL(p) (!(p) || (intptr_t)(p) < 0)
#define sg_dma_address(sg) ((sg)->dma_address)
#define spin_lock_irqsave(p,f) do { (f)=0; assert(!*(p)); *(p)=1; } while (0)
#define spin_unlock_irqrestore(p,f) do { (void)(f); assert(*(p)); *(p)=0; } while (0)
static void mutex_lock(int *lock) { assert(!*lock); *lock=1; }
static void mutex_unlock(int *lock) { assert(*lock); *lock=0; }
static void *kmalloc(size_t size, int flags) { (void)flags; return malloc(size); }
static void kfree(void *p) { free(p); }
static void reset_control_deassert(void *r) { (void)r; }
static void reset_control_assert(void *r) { (void)r; }
static int clk_prepare_enable(void *c) { (void)c; clocks++; return 0; }
static void clk_disable_unprepare(void *c) { (void)c; clocks--; }
static struct dma_buf *dma_buf_get(int fd)
{
    assert(fd >= 0 && fd < 8);
    if (failure == 1) return (void *)(intptr_t)-EINVAL;
    buffers[fd].refs++;
    return &buffers[fd];
}
static void dma_buf_put(struct dma_buf *b) { assert(b->refs > 0); b->refs--; }
static struct dma_buf_attachment *dma_buf_attach(struct dma_buf *b, void *dev)
{
    (void)dev;
    if (failure == 2) return (void *)(intptr_t)-EINVAL;
    struct dma_buf_attachment *a = malloc(sizeof(*a));
    assert(a); a->buffer=b; attachments++;
    return a;
}
static void dma_buf_detach(struct dma_buf *b, struct dma_buf_attachment *a)
{
    assert(a->buffer == b && attachments > 0); attachments--; free(a);
}
static struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *a, int dir)
{
    (void)dir;
    if (failure == 3) return (void *)(intptr_t)-EINVAL;
    struct sg_table *table=malloc(sizeof(*table));
    assert(table); table->sgl=malloc(sizeof(*table->sgl)); assert(table->sgl);
    table->sgl->dma_address=0xf0000000UL + (a->buffer-buffers)*4096;
    table->nents=1; mappings++;
    return table;
}
static void dma_buf_unmap_attachment(struct dma_buf_attachment *a, struct sg_table *t, int dir)
{
    (void)a; (void)dir;
    assert(mappings > 0); mappings--; free(t->sgl); free(t);
}
#include "driver-functions.inc"
static int list_size(void)
{
    struct aw_mem_list_head *p;
    int n=0;
    assert(device.list.aw_next && device.list.aw_prev);
    for (p=device.list.aw_next; p != &device.list; p=p->aw_next) {
        assert(p->aw_next->aw_prev == p && ++n <= 8);
    }
    return n;
}
int main(void)
{
    struct file a={1}, b={2};
    unsigned int addr;
    probe_mapping_state(); /* Statements extracted from the real probe. */
    device.bMemDevAttachFlag=1; /* Set by both mapping ioctls. */
    assert(!clk_status && !clocks);
    assert(!map_dma_buf_addr(1, &addr, &a)); /* First ioctl, no engine request. */
    assert(addr == 0xf0001000 && list_size() == 1);
    assert(!enable_cedar_hw_clk());
    assert(list_size() == 1); /* Power-up must preserve the first mapping. */
    assert(!map_dma_buf_addr(2, &addr, &a));
    assert(!map_dma_buf_addr(3, &addr, &b));
    assert(list_size() == 3 && attachments == 3 && mappings == 3);
    assert(!disable_cedar_hw_clk() && !clocks);
    assert(list_size() == 3);
    assert(!enable_cedar_hw_clk());
    assert(list_size() == 3);
    unmap_dma_buf_addr(0, 1, &a);
    assert(list_size() == 2 && mappings == 2);
    unmap_dma_buf_addr(1, 0, &a); /* Release only this open file's mappings. */
    assert(list_size() == 1 && mappings == 1);
    unmap_dma_buf_addr(1, 0, &b);
    assert(!list_size() && !mappings && !attachments);
    assert(!disable_cedar_hw_clk() && !clocks);
    for (failure=1; failure<=3; ++failure) {
        assert(map_dma_buf_addr(4, &addr, &a) < 0);
        assert(!list_size() && !mappings && !attachments);
    }
    for (int i=0; i<8; ++i) assert(!buffers[i].refs);
    assert(!device.lock_mem && !device.lock);
    puts("PASS: cold DMA mapping, live mappings across clock cycles, per-file cleanup and failures");
    return 0;
}
