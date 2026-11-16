#include "process.h"
#include "elf.h"
#include "../fs/vfs.h"
#include "../sched/task.h"
#include "../x64/paging.h"
#include "../log/kprintf.h"
int process_start_init(void) {
    static const char *arguments[] = {"/init", NULL};
    uint64_t *pml4;
    uint64_t entry;
    uint64_t stack;
    uint64_t argc;
    uint64_t argv;
    if (vfs_init() != 0) {
        kprintf("ext2 mount failed\n");
        return -1;
    }
    kprintf("ext2 mounted, loading /init\n");
    if (elf_load_path("/init", arguments, &pml4, &entry, &stack, &argc, &argv) != 0) {
        kprintf("ELF load failed\n");
        return -1;
    }
    struct task *initial = task_create(entry, stack, argc, argv, pml4);
    if (!initial) {
        paging_free_pml4(pml4);
        kprintf("init task creation failed\n");
        return -1;
    }
    kprintf("init task pid=%u ready\n", (unsigned)initial->pid);
    return 0;
}
