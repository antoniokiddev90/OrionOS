#ifndef SCHED_TASK_H
#define SCHED_TASK_H
#include <stdint.h>
#include <stddef.h>
#define TASK_READY 1
#define TASK_RUNNING 2
#define TASK_DEAD 3
#define TASK_BLOCKED 4
#define KSTACK_SIZE 16384
#define USER_FRAME_SIZE 160
struct task {
    uint64_t rsp;
    uint64_t kstack_top;
    uint64_t pid;
    uint32_t state;
    uint32_t exit_status;
    uint64_t parent_pid;
    uint64_t *pml4;
    char cwd[256];
    struct task *next;
    uint64_t mmap_next;
    uint32_t uid;
    uint32_t gid;
    uint32_t ruid;
    uint32_t rgid;
};
void task_init(void);
struct task *task_create(uint64_t entry, uint64_t user_stack, uint64_t arg0, uint64_t arg1, uint64_t *pml4);
struct task *task_current(void);
uint64_t task_fork(uint64_t *user_frame);
int task_replace_address_space(uint64_t *pml4);
void task_exit(uint32_t status);
int64_t task_wait4(int64_t pid, uint32_t *status, int options);
int task_set_cwd(const char *path);
uint64_t schedule(uint64_t current_rsp);
void sched_set_need_resched(void);
uint64_t sched_should_switch(void);
#endif
