#ifndef SCHED_TASK_H
#define SCHED_TASK_H

#include <stdint.h>
#include <stddef.h>

#define TASK_READY   1
#define TASK_RUNNING 2
#define TASK_DEAD    3

#define KSTACK_SIZE  16384
struct task {
    uint64_t rsp;
    uint64_t kstack_top;
    uint64_t pid;
    uint32_t state;
    uint64_t *pml4;
    struct task *next;
};
void         task_init(void);
struct task *task_create(uint64_t entry, uint64_t user_stack_top);
struct task *task_current(void);
void         task_exit(void);
uint64_t     schedule(uint64_t current_rsp);
void         sched_set_need_resched(void);
uint64_t     sched_should_switch(void);

#endif
