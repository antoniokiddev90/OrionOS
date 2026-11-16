#include "task.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "x64/paging.h"
#include "x64/tss.h"
#include "log/kprintf.h"
#include "../mm/user_mmap.h"

static struct task *pick_next(struct task *start);
static struct task *current;
static struct task *idle_task;
static uint64_t next_pid = 1;
static volatile uint64_t need_resched;
static void insert_task(struct task *task) {
    task->next = idle_task->next;
    idle_task->next = task;
}
void task_init(void) {
    idle_task = (struct task *)kcalloc(1, sizeof(struct task));
    if (!idle_task) for (;;) __asm__ volatile("cli; hlt");
    idle_task->state = TASK_RUNNING;
    idle_task->pml4 = paging_current_pml4();
    idle_task->cwd[0] = '/';
    idle_task->cwd[1] = 0;
    idle_task->next = idle_task;
    current = idle_task;
}
struct task *task_create(uint64_t entry, uint64_t user_stack, uint64_t arg0, uint64_t arg1, uint64_t *pml4) {
    if (!pml4) return NULL;
    struct task *task = (struct task *)kcalloc(1, sizeof(struct task));
    if (!task) return NULL;
    uint64_t stack = vmm_alloc(KSTACK_SIZE, PTE_PRESENT | PTE_WRITE);
    if (!stack) {
        kfree(task);
        return NULL;
    }
    task->pml4 = pml4;
    task->pid = next_pid++;
    task->mmap_next = USER_MMAP_BASE;
    task->state = TASK_READY;
    task->cwd[0] = '/';
    task->cwd[1] = 0;
    task->kstack_top = stack + KSTACK_SIZE;
    uint64_t *sp = (uint64_t *)task->kstack_top;
    *--sp = 0x1B;
    *--sp = user_stack;
    *--sp = 0x202;
    *--sp = 0x23;
    *--sp = entry;
    for (size_t i = 0; i < 15; ++i) *--sp = 0;
    sp[9] = arg0;
    sp[10] = arg1;
    task->rsp = (uint64_t)sp;
    insert_task(task);
    return task;
}
struct task *task_current(void) { return current; }
uint64_t task_fork(uint64_t *user_frame) {
    if (!current || !user_frame || current == idle_task) { kprintf("fork invalid context frame=%p\n", user_frame); return (uint64_t)-1; }
    struct task *child = (struct task *)kcalloc(1, sizeof(struct task));
    if (!child) { kprintf("fork task allocation failed\n"); return (uint64_t)-1; }
    uint64_t stack = vmm_alloc(KSTACK_SIZE, PTE_PRESENT | PTE_WRITE);
    if (!stack) {
        kfree(child);
        kprintf("fork stack allocation failed\n");
        return (uint64_t)-1;
    }
    child->pml4 = paging_new_pml4();
    if (!child->pml4 || paging_clone_user_pages(current->pml4, child->pml4) != 0) {
        if (child->pml4) paging_free_pml4(child->pml4);
        vmm_unmap(stack, KSTACK_SIZE);
        kfree(child);
        kprintf("fork address clone failed\n");
        return (uint64_t)-1;
    }
    child->pid = next_pid++;
    child->state = TASK_READY;
    child->parent_pid = current->pid;
    child->mmap_next = current->mmap_next;
    for (size_t i = 0; i < sizeof(child->cwd); ++i) child->cwd[i] = current->cwd[i];
    child->kstack_top = stack + KSTACK_SIZE;
    uint64_t *child_frame = (uint64_t *)(child->kstack_top - USER_FRAME_SIZE);
    for (size_t i = 0; i < USER_FRAME_SIZE / sizeof(uint64_t); ++i) child_frame[i] = user_frame[i];
    child_frame[14] = 0;
    child->rsp = (uint64_t)child_frame;
    insert_task(child);
    return child->pid;
}
int task_replace_address_space(uint64_t *pml4) {
    if (!current || !pml4 || current == idle_task) return -1;
    uint64_t *old = current->pml4;
    current->pml4 = pml4;
    current->mmap_next = USER_MMAP_BASE;
    paging_switch(pml4);
    paging_free_pml4(old);
    return 0;
}
void task_exit(uint32_t status) {
    if (!current || current == idle_task) return;
    current->exit_status = status;
    current->state = TASK_DEAD;
    for (struct task *task = idle_task->next; task != idle_task; task = task->next) {
        if (task->pid == current->parent_pid && task->state == TASK_BLOCKED) task->state = TASK_READY;
    }
    need_resched = 1;
    for (;;) __asm__ volatile("sti; hlt");
}
static void reap_task(struct task *target) {
    struct task *previous = idle_task;
    while (previous->next != idle_task && previous->next != target) previous = previous->next;
    if (previous->next != target || target == current) return;
    previous->next = target->next;
    if (target->kstack_top >= KSTACK_SIZE) vmm_unmap(target->kstack_top - KSTACK_SIZE, KSTACK_SIZE);
    paging_free_pml4(target->pml4);
    kfree(target);
}
int64_t task_wait4(int64_t pid, uint32_t *status, int options) {
    if (!current || current == idle_task || pid == 0 || pid < -1) return -1;
    struct task *found = NULL;
    int has_child = 0;
    for (struct task *task = idle_task->next; task != idle_task; task = task->next) {
        if (task->parent_pid != current->pid || (pid > 0 && task->pid != (uint64_t)pid)) continue;
        has_child = 1;
        if (task->state == TASK_DEAD) {
            found = task;
            break;
        }
    }
    if (found) {
        int64_t result = (int64_t)found->pid;
        if (status) *status = found->exit_status << 8;
        reap_task(found);
        return result;
    }
    if (!has_child) return -1;
    if (options & 1) return 0;
    current->state = TASK_BLOCKED;
    need_resched = 1;
    return 0;
}
int task_set_cwd(const char *path) {
    if (!current || !path) return -1;
    size_t length = 0;
    while (path[length] && length < sizeof(current->cwd) - 1) ++length;
    if (path[length]) return -1;
    for (size_t i = 0; i <= length; ++i) current->cwd[i] = path[i];
    return 0;
}
uint64_t schedule(uint64_t current_rsp) {
    struct task *previous = current;
    if (previous->state == TASK_RUNNING) previous->state = TASK_READY;
    if (previous->state != TASK_DEAD) previous->rsp = current_rsp;
    struct task *next = pick_next(previous);
    if (!next && previous != idle_task) next = idle_task;
    if (!next) {
        previous->state = TASK_RUNNING;
        return current_rsp;
    }
    next->state = TASK_RUNNING;
    current = next;
    tss_set_rsp0(next->kstack_top);
    paging_switch(next->pml4);
    return next->rsp;
}
static struct task *pick_next(struct task *start) {
    struct task *task = start->next;
    while (task != start) {
        if (task->state == TASK_READY) return task;
        task = task->next;
    }
    return NULL;
}
void sched_set_need_resched(void) { need_resched = 1; }
uint64_t sched_should_switch(void) {
    uint64_t result = need_resched;
    need_resched = 0;
    return result;
}
