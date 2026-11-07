#include "user.h"
#include "sched/task.h"
#include "x64/paging.h"
#include "x64/pmm.h"
#include "x64/vga.h"
#include <stdint.h>
#include <stddef.h>

extern uint8_t user_demo_start[];
extern uint8_t user_demo_end[];

#define USER_CODE_VA   0x0000000040000000ULL
#define USER_STACK_VA  0x0000000040001000ULL
#define USER_STACK_TOP (USER_STACK_VA + PAGE_SIZE)

void user_demo_init(void) {
    size_t code_size = (size_t)(user_demo_end - user_demo_start);
    if (code_size > PAGE_SIZE) {
        vga_write("user code too large\n");
        return;
    }

    uint64_t code_pa = pmm_alloc_frame();
    if (!code_pa) return;
    uint8_t *dst = (uint8_t *)code_pa;
    for (size_t i = 0; i < code_size; ++i) dst[i] = user_demo_start[i];
    if (paging_map_user_page(USER_CODE_VA, code_pa) != 0) {
        pmm_free_frame(code_pa);
        return;
    }

    uint64_t stack_pa = pmm_alloc_frame();
    if (!stack_pa) return;
    uint8_t *stk = (uint8_t *)stack_pa;
    for (size_t i = 0; i < PAGE_SIZE; ++i) stk[i] = 0;
    if (paging_map_user_page(USER_STACK_VA, stack_pa) != 0) {
        pmm_free_frame(stack_pa);
        return;
    }

    if (!task_create(USER_CODE_VA, USER_STACK_TOP)) {
        vga_write("task create failed\n");
    }
}
