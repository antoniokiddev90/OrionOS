#include "task.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "x64/paging.h"
#include "x64/tss.h"

static struct task *current;
static struct task *idle_task;
static uint64_t next_pid = 1;
static volatile uint64_t need_resched = 0;

void task_init(void) {
    idle_task = (struct task *)kmalloc(sizeof(struct task));
    idle_task->rsp = 0;
    idle_task->kstack_top = 0;
    idle_task->pid = 0;
    idle_task->state = TASK_RUNNING;
    idle_task->next = idle_task;
    current = idle_task;
}

struct task *task_create(uint64_t entry, uint64_t user_stack_top) {
    struct task *t = (struct task *)kmalloc(sizeof(struct task));
    if (!t) return 0;
    t->pid = next_pid++;
    t->state = TASK_READY;

    uint64_t kstack = vmm_alloc(KSTACK_SIZE, PTE_PRESENT | PTE_WRITE);
    if (!kstack) return 0;
    uint64_t top = kstack + KSTACK_SIZE;
    t->kstack_top = top;

    uint64_t *sp = (uint64_t *)top;
    *--sp = 0x1B;
    *--sp = user_stack_top;
    *--sp = 0x202;
    *--sp = 0x23;
    *--sp = entry;
    for (int i = 0; i < 15; ++i) *--sp = 0;
    t->rsp = (uint64_t)sp;

    t->next = idle_task->next;
    idle_task->next = t;

    return t;
}

struct task *task_current(void) { return current; }

void task_exit(void) {
    current->state = TASK_DEAD;
    need_resched = 1;
    for (;;) __asm__ volatile("hlt");
}

static struct task *pick_next(struct task *start) {
    struct task *t = start->next;
    while (t != start) {
        if (t->state == TASK_READY) return t;
        t = t->next;
    }
    return 0;
}

uint64_t schedule(uint64_t current_rsp) {
    struct task *prev = current;
    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    if (prev->state != TASK_DEAD) prev->rsp = current_rsp;

    struct task *next = pick_next(prev);
    if (!next) {
        if (prev->state == TASK_DEAD) {
            for (;;) __asm__ volatile("cli; hlt");
        }
        prev->state = TASK_RUNNING;
        return current_rsp;
    }

    next->state = TASK_RUNNING;
    current = next;
    tss_set_rsp0(next->kstack_top);
    return next->rsp;
}

void sched_set_need_resched(void) { need_resched = 1; }

uint64_t sched_should_switch(void) {
    uint64_t r = need_resched;
    need_resched = 0;
    return r;
}
