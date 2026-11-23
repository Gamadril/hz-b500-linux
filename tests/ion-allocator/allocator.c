/* Exercise the production allocator against the BSP's modern ioctl contract. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include "ion_alloc.h"
#include <ion_mem_alloc.h>

static int fake_open(const char *, int, ...);
static int fake_close(int);
static int fake_ioctl(int, unsigned long, ...);
static void *fake_mmap(void *, size_t, int, int, int, off_t);
static int fake_munmap(void *, size_t);
static void *fake_calloc(size_t, size_t);
#define open(...) fake_open(__VA_ARGS__)
#define close(...) fake_close(__VA_ARGS__)
#define ioctl(...) fake_ioctl(__VA_ARGS__)
#define mmap(...) fake_mmap(__VA_ARGS__)
#define munmap(...) fake_munmap(__VA_ARGS__)
#define calloc(...) fake_calloc(__VA_ARGS__)
#include "../../package/allwinner/libuapi/src/ion_alloc.c"
#undef open
#undef close
#undef ioctl
#undef mmap
#undef munmap
#undef calloc

enum { NONE, QUERY, NO_HEAP, ALLOC, MAP, NODE, ATTACH, ZERO_ADDRESS, CEDAR_OPEN };
static int failure, fds[64], mappings, attachments, cache_flushes;
static unsigned int last_mask;

static int new_fd(int kind)
{
    for (int i = 0; i < 64; ++i) {
        if (!fds[i]) { fds[i] = kind; return i; }
    }
    abort();
}
static int fake_open(const char *path, int flags, ...)
{
    (void)flags;
    if (!strcmp(path, "/dev/ion")) return new_fd(1);
    assert(!strcmp(path, "/dev/cedar_dev"));
    if (failure == CEDAR_OPEN) { errno = ENODEV; return -1; }
    return new_fd(2);
}
static int fake_close(int fd)
{
    assert(fd >= 0 && fd < 64 && fds[fd]);
    fds[fd] = 0;
    return 0;
}
static int fake_ioctl(int fd, unsigned long command, ...)
{
    va_list ap;
    va_start(ap, command);
    void *arg = va_arg(ap, void *);
    va_end(ap);
    assert(fd >= 0 && fd < 64 && fds[fd]);
    if (command == AW_ION_IOC_HEAP_QUERY) {
        assert(fds[fd] == 1);
        if (failure == QUERY) { errno = ENOTTY; return -1; }
        struct aw_ion_heap_query *query = arg;
        struct aw_ion_heap_data *heaps = (void *)(uintptr_t)query->heaps;
        assert(query->cnt >= 3 && !query->reserved0 && !query->reserved1);
        heaps[0].type = CDC_ION_HEAP_TYPE_SYSTEM; heaps[0].heap_id = 0;
        heaps[1].type = CDC_ION_HEAP_TYPE_DMA; heaps[1].heap_id = 1;
        heaps[2].type = CDC_ION_HEAP_TYPE_DMA; heaps[2].heap_id = 7;
        query->cnt = failure == NO_HEAP ? 0 : 3;
        return 0;
    }
    if (command == AW_ION_IOC_NEW_ALLOC) {
        struct aw_ion_new_alloc_data *data = arg;
        assert(fds[fd] == 1 && data->len && !data->unused);
#ifdef CONF_KERNEL_IOMMU
        assert(data->heap_id_mask == 1);
#else
        assert(data->heap_id_mask == 0x82);
#endif
        last_mask = data->heap_id_mask;
        if (failure == ALLOC) { errno = ENOMEM; return -1; }
        data->fd = new_fd(3);
        return 0;
    }
    if (command == AW_MEM_GET_IOMMU_ADDR) {
        struct user_iommu_param *param = arg;
        assert(fds[fd] == 2 && fds[param->fd] == 3);
        if (failure == ATTACH) { errno = EIO; return -1; }
        param->iommu_addr = failure == ZERO_ADDRESS ? 0 : 0xf0000000U + param->fd * 4096;
        attachments++;
        return 0;
    }
    if (command == AW_MEM_FREE_IOMMU_ADDR) {
        struct user_iommu_param *param = arg;
        assert(fds[fd] == 2 && fds[param->fd] == 3 && attachments > 0);
        attachments--;
        return 0;
    }
    if (command == AW_MEM_FLUSH_CACHE_RANGE) {
        sunxi_cache_range *range = arg;
        assert(fds[fd] == 2 && range->end > range->start);
        cache_flushes++;
        return 0;
    }
    /* A modern allocation must never issue legacy handle/custom/free ioctls. */
    fprintf(stderr, "Unexpected ioctl: %lx\n", command);
    abort();
}
static void *fake_mmap(void *hint, size_t size, int prot, int flags, int fd, off_t offset)
{
    (void)hint; (void)prot; (void)flags; (void)offset;
    assert(fds[fd] == 3);
    if (failure == MAP) { errno = ENOMEM; return MAP_FAILED; }
    void *memory = malloc(size);
    assert(memory);
    mappings++;
    return memory;
}
static int fake_munmap(void *memory, size_t size)
{
    assert(memory && size && mappings > 0);
    free(memory); mappings--;
    return 0;
}
static void *fake_calloc(size_t n, size_t size)
{
    if (failure == NODE) { errno = ENOMEM; return NULL; }
    return calloc(n, size);
}
static void check_clean(void)
{
    assert(!g_ion_alloc_context && !mappings && !attachments);
    for (int i = 0; i < 64; ++i) assert(!fds[i]);
}
int main(void)
{
    sunxi_ion_alloc_pfree((void *)1); /* No held mutex after an unopened free. */
    sunxi_ion_alloc_close();
    for (failure = QUERY; failure <= CEDAR_OPEN; ++failure) {
        if (failure == QUERY || failure == NO_HEAP || failure == CEDAR_OPEN) {
            assert(sunxi_ion_alloc_open() < 0);
        } else {
            assert(sunxi_ion_alloc_open() == 0);
            assert(!sunxi_ion_alloc_palloc(2457600));
            assert(!mappings && !attachments);
            sunxi_ion_alloc_close();
        }
        check_clean();
    }
    failure = NONE;
    assert(!sunxi_ion_alloc_open());
    assert(g_ion_alloc_context->fd == 0); /* fd zero is valid. */
    assert(!sunxi_ion_alloc_open());
    assert(!sunxi_ion_alloc_palloc(0));
    unsigned char *a = sunxi_ion_alloc_palloc(2457600);
    unsigned char *b = sunxi_ion_alloc_palloc(2457600);
    assert(a && b && mappings == 2 && attachments == 2);
    assert(sunxi_ion_alloc_get_bufferFd(a) >= 0);
    assert((uintptr_t)sunxi_ion_alloc_vir2phy_cpu(a + 128) ==
           (uintptr_t)sunxi_ion_alloc_vir2phy_cpu(a) + 128);
    sunxi_ion_alloc_flush_cache(a, 2457600);
    assert(cache_flushes == 1 && last_mask);
    sunxi_ion_alloc_pfree(a);
    assert(mappings == 1 && attachments == 1);
    sunxi_ion_alloc_close(); /* One remaining allocator reference. */
    assert(g_ion_alloc_context);
    sunxi_ion_alloc_close(); /* Also releases the outstanding buffer. */
    check_clean();
    sunxi_ion_alloc_close();
    puts("PASS: modern ION heap selection, DMA mapping, failure cleanup and lifetime");
    return 0;
}
