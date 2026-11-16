#include "syscall.h"
#include "log/log.h"
#include "idt.h"
#include "atkbd.h"
#include "sched/task.h"
#include "../exec/elf.h"
#include "../fs/vfs.h"
#include "../x64/paging.h"
#include "../mm/heap.h"
#include <stdint.h>
#include <stddef.h>
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_OPEN 3
#define SYS_CLOSE 4
#define SYS_LSEEK 5
#define SYS_FORK 6
#define SYS_EXECVE 7
#define SYS_WAIT4 8
#define SYS_GETDENTS64 9
#define SYS_CHDIR 10
#define SYS_GETCWD 11
#define SYS_STAT 12
#define SYS_FSTAT 13
#define SYS_MKDIR 14
#define SYS_UNLINK 15
#define SYS_GETPID 16
#define SYS_EXIT 17
#define SYS_YIELD 18
#define SYS_DUP 19
#define SYS_ACCESS 20
#define USER_ARG_MAX 16
#define USER_STRING_MAX 255
#define USER_IO_MAX 65536
struct user_stat {
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint32_t reserved;
};
static int user_range_readable(uint64_t address, size_t size) {
    struct task *task = task_current();
    if (!task || address < USER_SPACE_BASE || address >= USER_SPACE_END || size > USER_SPACE_END - address) return 0;
    uint64_t end = address + size;
    for (uint64_t page = address & PAGE_MASK; page < end; page += PAGE_SIZE) {
        uint64_t *table = paging_walk_in(task->pml4, page, 0);
        if (!table || !(table[(page >> 12) & 0x1ff] & PTE_PRESENT) || !(table[(page >> 12) & 0x1ff] & PTE_USER)) return 0;
    }
    return 1;
}
static int user_range_writable(uint64_t address, size_t size) {
    struct task *task = task_current();
    if (!task || address < USER_SPACE_BASE || address >= USER_SPACE_END || size > USER_SPACE_END - address) return 0;
    uint64_t end = address + size;
    for (uint64_t page = address & PAGE_MASK; page < end; page += PAGE_SIZE) {
        uint64_t *table = paging_walk_in(task->pml4, page, 0);
        if (!table) return 0;
        uint64_t entry = table[(page >> 12) & 0x1ff];
        if (!(entry & PTE_PRESENT) || !(entry & PTE_USER) || !(entry & PTE_WRITE)) return 0;
    }
    return 1;
}
static int copy_user_string(char *destination, uint64_t source, size_t capacity) {
    if (!destination || !capacity) return -1;
    for (size_t i = 0; i < capacity; ++i) {
        if (!user_range_readable(source + i, 1)) return -1;
        destination[i] = ((const char *)source)[i];
        if (!destination[i]) return 0;
    }
    destination[capacity - 1] = 0;
    return -1;
}
static size_t string_length(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}
static int resolve_path(const char *path, char *output) {
    if (!path || !*path || !output) return -1;
    char source[512];
    size_t length = 0;
    if (path[0] == '/') {
        while (path[length] && length < sizeof(source) - 1) {
            source[length] = path[length];
            ++length;
        }
    } else {
        struct task *task = task_current();
        const char *cwd = task ? task->cwd : "/";
        size_t cwd_length = string_length(cwd);
        if (cwd_length + 1 >= sizeof(source)) return -1;
        for (size_t i = 0; i < cwd_length; ++i) source[length++] = cwd[i];
        if (length == 0 || source[length - 1] != '/') source[length++] = '/';
        while (path[length - (cwd_length + (cwd_length && source[cwd_length - 1] != '/' ? 1 : 0))] && length < sizeof(source) - 1) {
            size_t index = length - (cwd_length + (cwd_length && source[cwd_length - 1] != '/' ? 1 : 0));
            source[length++] = path[index];
        }
    }
    if (path[0] == '/' && path[length]) return -1;
    source[length] = 0;
    char segments[64][64];
    size_t counts[64];
    size_t depth = 0;
    size_t position = 0;
    while (position < length) {
        while (source[position] == '/') ++position;
        if (!source[position]) break;
        size_t start = position;
        while (source[position] && source[position] != '/') ++position;
        size_t part = position - start;
        if (part == 1 && source[start] == '.') continue;
        if (part == 2 && source[start] == '.' && source[start + 1] == '.') {
            if (depth) --depth;
            continue;
        }
        if (!part || part >= sizeof(segments[0]) || depth >= 64) return -1;
        for (size_t i = 0; i < part; ++i) segments[depth][i] = source[start + i];
        segments[depth][part] = 0;
        counts[depth] = part;
        ++depth;
    }
    size_t out = 0;
    output[out++] = '/';
    for (size_t i = 0; i < depth; ++i) {
        if (out + counts[i] + (i + 1 < depth ? 1 : 0) >= 256) return -1;
        for (size_t j = 0; j < counts[i]; ++j) output[out++] = segments[i][j];
        if (i + 1 < depth) output[out++] = '/';
    }
    output[out] = 0;
    return 0;
}
static uint64_t sys_write(int descriptor, uint64_t buffer, size_t length) {
    if (length > USER_IO_MAX || (length && !user_range_readable(buffer, length))) return (uint64_t)-1;
    if (descriptor == 1 || descriptor == 2 || vfs_is_tty(descriptor)) {
        for (size_t i = 0; i < length; ++i) log_putc(((const char *)buffer)[i]);
        return length;
    }
    if (descriptor < 3) return (uint64_t)-1;
    return (uint64_t)vfs_write(descriptor, (const void *)buffer, length);
}
static uint64_t read_keyboard(uint64_t buffer, size_t length) {
    if (length > USER_IO_MAX || (length && !user_range_writable(buffer, length))) return (uint64_t)-1;
    if (!length) return 0;
    size_t count = 0;
    while (count < length) {
        uint8_t character = atkbd_read_char();
        if (!character) {
            __asm__ volatile("sti; hlt; cli" : : : "memory");
            continue;
        }
        ((char *)buffer)[count++] = (char)character;
        if (character == '\n') break;
    }
    return count;
}
static uint64_t sys_read(int descriptor, uint64_t buffer, size_t length) {
    if (descriptor == 0 || vfs_is_tty(descriptor)) return read_keyboard(buffer, length);
    if (length > USER_IO_MAX || (length && !user_range_writable(buffer, length))) return (uint64_t)-1;
    if (descriptor < 3) return (uint64_t)-1;
    return (uint64_t)vfs_read(descriptor, (void *)buffer, length);
}
static uint64_t sys_open(uint64_t path_address, int flags, uint32_t mode) {
    char user_path[USER_STRING_MAX + 1];
    char path[256];
    if (copy_user_string(user_path, path_address, sizeof(user_path)) != 0 || resolve_path(user_path, path) != 0) return (uint64_t)-1;
    return (uint64_t)vfs_open_flags(path, flags, mode);
}
static uint64_t sys_execve(uint64_t path_address, uint64_t argv_address, uint64_t *frame) {
    char user_path[USER_STRING_MAX + 1];
    char path[256];
    char arguments_storage[USER_ARG_MAX][USER_STRING_MAX + 1];
    const char *arguments[USER_ARG_MAX + 1];
    if (copy_user_string(user_path, path_address, sizeof(user_path)) != 0 || resolve_path(user_path, path) != 0) return (uint64_t)-1;
    size_t count = 0;
    if (argv_address) {
        for (; count < USER_ARG_MAX; ++count) {
            uint64_t pointer_address = argv_address + count * sizeof(uint64_t);
            if (!user_range_readable(pointer_address, sizeof(uint64_t))) return (uint64_t)-1;
            uint64_t string_address = *(const uint64_t *)pointer_address;
            if (!string_address) break;
            if (copy_user_string(arguments_storage[count], string_address, sizeof(arguments_storage[count])) != 0) return (uint64_t)-1;
            arguments[count] = arguments_storage[count];
        }
        if (count == USER_ARG_MAX) return (uint64_t)-1;
    }
    arguments[count] = NULL;
    uint64_t *pml4;
    uint64_t entry;
    uint64_t stack;
    uint64_t argc;
    uint64_t argv;
    if (elf_load_path(path, arguments, &pml4, &entry, &stack, &argc, &argv) != 0) return (uint64_t)-1;
    if (task_replace_address_space(pml4) != 0) {
        paging_free_pml4(pml4);
        return (uint64_t)-1;
    }
    for (size_t i = 0; i < 15; ++i) frame[i] = 0;
    frame[9] = argc;
    frame[10] = argv;
    frame[15] = entry;
    frame[16] = 0x23;
    frame[17] = 0x202;
    frame[18] = stack;
    frame[19] = 0x1B;
    return 0;
}
static int copy_stat_to_user(uint64_t address, const struct vfs_stat *status) {
    if (!user_range_writable(address, sizeof(struct user_stat))) return -1;
    struct user_stat output;
    output.inode = status->inode;
    output.size = status->size;
    output.mode = status->mode;
    output.reserved = 0;
    *(struct user_stat *)address = output;
    return 0;
}
static uint64_t sys_stat(uint64_t path_address, uint64_t status_address) {
    char user_path[USER_STRING_MAX + 1];
    char path[256];
    struct vfs_stat status;
    if (copy_user_string(user_path, path_address, sizeof(user_path)) != 0 || resolve_path(user_path, path) != 0 || vfs_stat_path(path, &status) != 0) return (uint64_t)-1;
    return copy_stat_to_user(status_address, &status) == 0 ? 0 : (uint64_t)-1;
}
uint64_t syscall_dispatch(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t *frame) {
    switch (number) {
        case SYS_WRITE: return sys_write((int)a1, a2, (size_t)a3);
        case SYS_READ: return sys_read((int)a1, a2, (size_t)a3);
        case SYS_OPEN: return sys_open(a1, (int)a2, (uint32_t)a3);
        case SYS_CLOSE: return (uint64_t)vfs_close((int)a1);
        case SYS_LSEEK: return (uint64_t)vfs_seek((int)a1, (int64_t)a2, (int)a3);
        case SYS_FORK: return task_fork(frame);
        case SYS_EXECVE: return sys_execve(a1, a2, frame);
        case SYS_WAIT4: {
            uint32_t *status = NULL;
            if (a2) {
                if (!user_range_writable(a2, sizeof(uint32_t))) return (uint64_t)-1;
                status = (uint32_t *)a2;
            }
            return (uint64_t)task_wait4((int64_t)a1, status, (int)a3);
        }
        case SYS_GETDENTS64:
            if ((size_t)a3 > USER_IO_MAX || !user_range_writable(a2, (size_t)a3)) return (uint64_t)-1;
            return (uint64_t)vfs_getdents((int)a1, (void *)a2, (size_t)a3);
        case SYS_CHDIR: {
            char user_path[USER_STRING_MAX + 1];
            char path[256];
            if (copy_user_string(user_path, a1, sizeof(user_path)) != 0 || resolve_path(user_path, path) != 0 || !vfs_is_directory(path)) return (uint64_t)-1;
            return (uint64_t)task_set_cwd(path);
        }
        case SYS_GETCWD: {
            struct task *task = task_current();
            size_t length = string_length(task->cwd) + 1;
            if ((size_t)a2 < length || !user_range_writable(a1, length)) return (uint64_t)-1;
            for (size_t i = 0; i < length; ++i) ((char *)a1)[i] = task->cwd[i];
            return a1;
        }
        case SYS_STAT: return sys_stat(a1, a2);
        case SYS_FSTAT: {
            struct vfs_stat status;
            if (vfs_stat_fd((int)a1, &status) != 0 || copy_stat_to_user(a2, &status) != 0) return (uint64_t)-1;
            return 0;
        }
        case SYS_MKDIR: {
            char user_path[USER_STRING_MAX + 1];
            char path[256];
            if (copy_user_string(user_path, a1, sizeof(user_path)) != 0 || resolve_path(user_path, path) != 0) return (uint64_t)-1;
            return (uint64_t)vfs_mkdir(path);
        }
        case SYS_UNLINK: {
            char user_path[USER_STRING_MAX + 1];
            char path[256];
            if (copy_user_string(user_path, a1, sizeof(user_path)) != 0 || resolve_path(user_path, path) != 0) return (uint64_t)-1;
            return (uint64_t)vfs_unlink(path);
        }
        case SYS_GETPID: {
            struct task *task = task_current();
            return task ? task->pid : 0;
        }
        case SYS_EXIT: task_exit((uint32_t)a1); return 0;
        case SYS_YIELD: sched_set_need_resched(); return 0;
        case SYS_DUP: return (uint64_t)vfs_dup((int)a1);
        case SYS_ACCESS: {
            char user_path[USER_STRING_MAX + 1];
            char path[256];
            struct vfs_stat status;
            if (copy_user_string(user_path, a1, sizeof(user_path)) != 0 || resolve_path(user_path, path) != 0) return (uint64_t)-1;
            (void)a2;
            return (uint64_t)vfs_stat_path(path, &status);
        }
        default: (void)a4; (void)a5; return (uint64_t)-1;
    }
}
void syscall_init(void) {
    extern void syscall_entry(void);
    idt_set_gate_dpl(0x9E, syscall_entry, 3);
}
