/* Exercise the production constructor/destructor with failed DMA setup. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t __s32;
typedef uint32_t __u32;
typedef unsigned char u8;
#define __iomem
#define GFP_KERNEL 0
#define __GFP_ZERO 1
#define G2D_ERR_MSG(...) ((void)0)
struct list_head { struct list_head *next, *prev; };
#define INIT_LIST_HEAD(p) do { (p)->next = (p); (p)->prev = (p); } while (0)
static struct list_head g2d_task_list = {&g2d_task_list, &g2d_task_list};
static int g2d_task_ida, ids, allocations, fail_allocation, fail_setup, frames_freed;
static void list_add_tail(struct list_head *p, struct list_head *head)
{
    p->prev = head->prev; p->next = head;
    head->prev->next = p; head->prev = p;
}
static void list_del(struct list_head *p)
{
    p->next->prev = p->prev; p->prev->next = p->next;
}
static void *kmalloc(size_t size, int flags)
{
    (void)flags;
    if (fail_allocation && --fail_allocation == 0) return NULL;
    void *p = calloc(1, size);
    assert(p); allocations++; return p;
}
#define kmalloc_array(n,s,f) kmalloc((n)*(s),(f))
static void kfree(void *p) { if (p) { allocations--; free(p); } }
static int ida_simple_get(int *ida, int start, int end, int flags)
{
    (void)start; (void)end; (void)flags;
    ids++; return ++*ida;
}
static void ida_simple_remove(int *ida, unsigned id)
{
    (void)ida; assert(id && ids > 0); ids--;
}
struct mixer_para { int value; };
typedef struct { void *io; } __g2d_info_t;
struct g2d_rcq_mem_info { void *reg_blk; };
struct g2d_mixer_task;
struct g2d_mixer_frame {
    u8 *g2d_base;
    __s32 (*destory)(struct g2d_mixer_frame *);
};
struct g2d_mixer_task {
    struct list_head list;
    __u32 task_id, frame_cnt;
    struct g2d_mixer_frame *frame;
    struct g2d_rcq_mem_info *p_rcq_info;
    struct mixer_para *p_para;
    __g2d_info_t *p_g2d_info;
    __s32 (*mixer_mem_setup)(struct g2d_mixer_task *, struct mixer_para *);
    __s32 (*apply)(struct g2d_mixer_task *, struct mixer_para *);
    __s32 (*destory)(struct g2d_mixer_task *);
};
static __s32 frame_destroy(struct g2d_mixer_frame *frame)
{
    (void)frame; frames_freed++; return 0;
}
static int g2d_mixer_frame_setup(struct g2d_mixer_frame *frame, unsigned index)
{
    (void)index; frame->destory = frame_destroy; return 0;
}
static int g2d_mixer_mem_setup(struct g2d_mixer_task *task, struct mixer_para *para)
{
    (void)para;
    task->p_rcq_info->reg_blk = kmalloc(16, 0);
    return fail_setup ? -1 : 0;
}
static __s32 g2d_mixer_apply(struct g2d_mixer_task *task, struct mixer_para *para)
{
    (void)task; (void)para; return 0;
}
static void g2d_top_mem_pool_free(struct g2d_rcq_mem_info *rcq) { assert(rcq); }
#include "driver-functions.inc"
static void empty(void)
{
    assert(allocations == 0 && ids == 0);
    assert(g2d_task_list.next == &g2d_task_list);
    assert(g2d_task_list.prev == &g2d_task_list);
}
int main(void)
{
    __g2d_info_t device = {0};
    struct mixer_para para[3] = {{1}, {2}, {3}};
    fail_setup = 1;
    assert(!create_mixer_task(&device, para, 3));
    assert(frames_freed == 3); empty();
    /* A rejected request must not unlink another, already published task. */
    fail_setup = 0;
    assert(create_mixer_task(&device, para, 1));
    struct g2d_mixer_task *live = (void *)g2d_task_list.next;
    fail_setup = 1;
    assert(!create_mixer_task(&device, para, 2));
    assert(g2d_task_list.next == &live->list && g2d_task_list.prev == &live->list);
    live->destory(live); empty();
    fail_setup = 0;
    /* p_para allocation fails after setup, but before publication. */
    fail_allocation = 5;
    assert(!create_mixer_task(&device, para, 1)); empty();
    /* Constructor allocation failures and normal multi-frame destruction. */
    for (int step = 1; step <= 3; step++) {
        fail_allocation = step;
        assert(!create_mixer_task(&device, para, 1)); empty();
    }
    fail_allocation = 0;
    assert(create_mixer_task(&device, para, 3));
    live = (void *)g2d_task_list.next;
    assert(!memcmp(live->p_para, para, sizeof(para)));
    live->destory(live); empty();
    puts("PASS: failed DMA setup, pre-publication allocation failures, live task isolation and normal destruction");
    return 0;
}
