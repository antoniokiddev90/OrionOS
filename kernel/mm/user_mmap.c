#include "user_mmap.h"
#include "../sched/task.h"
#include "../x64/paging.h"
#include "../x64/pmm.h"

static uint64_t round_up_to_page(uint64_t value) {
    return (value + PAGE_SIZE - 1) & PAGE_MASK;
}

static int range_inside_mmap(uint64_t address, uint64_t size) {
    return size && address >= USER_MMAP_BASE && address < USER_MMAP_END && size <= USER_MMAP_END - address;
}

static void release_range(struct task *task, uint64_t address, uint64_t size) {
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t physical = 0;
        if (paging_unmap_user_page_in(task->pml4, address + offset, &physical) == 1) pmm_free_frame(physical);
    }
}

static int back_range(struct task *task, uint64_t address, uint64_t size, int writable) {
    uint64_t offset = 0;
    for (; offset < size; offset += PAGE_SIZE) {
        uint64_t physical = pmm_alloc_frame();
        if (!physical) break;
        uint8_t *bytes = (uint8_t *)physical;
        for (size_t i = 0; i < PAGE_SIZE; ++i) bytes[i] = 0;
        if (paging_map_user_page_access_in(task->pml4, address + offset, physical, writable) != 0) {
            pmm_free_frame(physical);
            break;
        }
    }
    if (offset == size) return 0;
    release_range(task, address, offset);
    return -1;
}

int64_t user_mmap(uint64_t address, uint64_t length, uint64_t protection, uint64_t flags) {
    struct task *task = task_current();
    if (!task || !length || length > USER_MMAP_END - USER_MMAP_BASE) return -1;
    if (protection & ~(USER_MMAP_PROT_READ | USER_MMAP_PROT_WRITE | USER_MMAP_PROT_EXEC)) return -1;
    if (!(flags & USER_MMAP_MAP_ANONYMOUS) || (flags & USER_MMAP_MAP_SHARED) || !(flags & USER_MMAP_MAP_PRIVATE)) return -1;
    uint64_t size = round_up_to_page(length);
    uint64_t start;
    if (flags & USER_MMAP_MAP_FIXED) {
        if ((address & (PAGE_SIZE - 1)) || !range_inside_mmap(address, size)) return -1;
        start = address;
        release_range(task, start, size);
    } else {
        if (!range_inside_mmap(task->mmap_next, size)) return -1;
        start = task->mmap_next;
    }
    if (protection != USER_MMAP_PROT_NONE) {
        int writable = (protection & USER_MMAP_PROT_WRITE) != 0;
        if (back_range(task, start, size, writable) != 0) return -1;
    }
    if (start + size > task->mmap_next) task->mmap_next = start + size;
    return (int64_t)start;
}

int64_t user_munmap(uint64_t address, uint64_t length) {
    struct task *task = task_current();
    if (!task || !length || (address & (PAGE_SIZE - 1)) || length > USER_MMAP_END - USER_MMAP_BASE) return -1;
    uint64_t size = round_up_to_page(length);
    if (!range_inside_mmap(address, size)) return -1;
    release_range(task, address, size);
    return 0;
}
