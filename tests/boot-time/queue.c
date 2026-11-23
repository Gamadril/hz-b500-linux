/* SPDX-License-Identifier: GPL-2.0-only */
#include "runqueue.h"
#include "ustream.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <glob.h>
#include <fcntl.h>
#include <assert.h>
#define RC_TASK_NAME_SIZE 64
static unsigned int rc_boot_init;
static struct runqueue_task *hmi_first_task;
struct rc_task {char file_name[RC_TASK_NAME_SIZE];struct runqueue_task *task;};
static struct rc_task *rc_task_list;
struct initd {struct ustream_fd fd;struct runqueue_process proc;char *file,*param;};
static bool priority;
#define DEBUG(...) ((void)0)
#define ERROR(...) ((void)0)
static int test_access(const char *path,int mode) {(void)mode;assert(!strcmp(path,"/etc/procd/hmi-first"));return priority?0:-1;}
#define access test_access
static void *test_alloc(size_t size,char **file,size_t flen,char **param,size_t plen) {
    char *memory=calloc(1,size+flen+plen);assert(memory);*file=memory+size;*param=memory+size+flen;return memory;
}
#undef calloc_a
#define calloc_a test_alloc
static void q_initd_run(struct runqueue *q,struct runqueue_task *t) {(void)q;(void)t;}
static void q_initd_complete(struct runqueue *q,struct runqueue_task *t) {(void)q;(void)t;}
void runqueue_process_cancel_cb(struct runqueue *q,struct runqueue_task *t,int type) {(void)q;(void)t;(void)type;}
void runqueue_process_kill_cb(struct runqueue *q,struct runqueue_task *t) {(void)q;(void)t;}
void runqueue_task_add(struct runqueue *q,struct runqueue_task *task,bool running) {
    assert(!running);safe_list_add(&task->list,&q->tasks_inactive);
}
#include "queue.inc"
static const char *name(struct runqueue_task *task) {
    struct initd *process=container_of(task,struct initd,proc.task);return strrchr(process->file,'/')+1;
}
int main(int argc,char **argv) {
    assert(argc==2);
    for(unsigned enabled=0;enabled<2;enabled++) {
        struct runqueue q={0};INIT_SAFE_LIST(&q.tasks_active);INIT_SAFE_LIST(&q.tasks_inactive);
        rc_boot_init=0;priority=enabled;assert(!_rc(&q,argv[1],"S","*","boot"));
        struct runqueue_task *first=get_next_task_from_inactive_list(&q);assert(first);
        assert(!strcmp(name(first),enabled?"S09hz-ui":"S00clock"));
        safe_list_del(&first->list);safe_list_add(&first->list,&q.tasks_active);
        if(enabled)assert(!get_next_task_from_inactive_list(&q));
        safe_list_del(&first->list);
        unsigned remaining=0;
        while((first=get_next_task_from_inactive_list(&q))) {safe_list_del(&first->list);remaining++;}
        assert(remaining==3);
    }
    puts("PASS: actual procd queue schedules UI first only when opted in and releases all normal tasks after the UI boot task completes");
    return 0;
}
