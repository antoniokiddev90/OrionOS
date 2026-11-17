#include "process.h"
#include "elf.h"
#include "../block/gpt.h"
#include "../fs/vfs.h"
#include "../fs/ext2.h"
#include "../sched/task.h"
#include "../x64/paging.h"
#include "../x64/mb2.h"
#include "../x64/atkbd.h"
#include "../x64/vga.h"
#include "../log/kprintf.h"

#define PARTITION_MAX 32
#define ROOT_KEY "root="

static struct gpt_partition partitions[PARTITION_MAX];
static char cmdline[MB2_CMDLINE_MAX];

#include "../x64/init.h"

static int parse_root_index(const char *text, int count) {
    const char *key = "root=";
    for (const char *p = text; *p; ++p) {
        if (p != text && p[-1] != ' ') continue;
        const char *a = p;
        const char *b = key;
        while (*b && *a == *b) { ++a; ++b; }
        if (*b) continue;
        int value = 0;
        int digits = 0;
        while (*a >= '0' && *a <= '9') {
            value = value * 10 + (*a - '0');
            ++a;
            ++digits;
        }
        if (!digits || (*a != ' ' && *a != 0) || value >= count) return -2;
        return value;
    }
    return -1;
}

static int read_choice(int count) {
    int value = -1;
    for (;;) {
        uint8_t key = atkbd_read_char();
        if (key == '\n' && value >= 0 && value < count) return value;
        if (key >= '0' && key <= '9') {
            int next = (value < 0 ? 0 : value) * 10 + (key - '0');
            if (next < count) {
                value = next;
                kprintf("%c", (char)key);
            }
        }
        if (!key) __asm__ volatile("sti; hlt; cli" : : : "memory");
    }
}

static int choose_partition(int count) {
    kprintf("Select root partition:\n");
    for (int i = 0; i < count; ++i) {
        kprintf("  [%d] %s  lba %lu..%lu\n", i, partitions[i].name,
                (unsigned long)partitions[i].first_lba, (unsigned long)partitions[i].last_lba);
    }
    kprintf("Choice: ");
    int index = read_choice(count);
    kprintf("\n");
    return index;
}

static int select_base_lba(void) {
    int count = gpt_scan(partitions, PARTITION_MAX);
    if (count == 0) return 0;
    if (count < 0) {
        kprintf("GPT read failed\n");
        return -1;
    }

    if (x64_boot_cmdline(cmdline, sizeof(cmdline)) < 0) cmdline[0] = 0;

    int index = parse_root_index(cmdline, count);
    if (index == -2) {
        kprintf("invalid root= in cmdline\n");
        return -1;
    }
    if (index < 0) index = choose_partition(count);

    return (int)partitions[index].first_lba;
}

int process_start_init(void) {
    static const char *arguments[] = {"/init", NULL};
    uint64_t *pml4;
    uint64_t entry;
    uint64_t stack;
    uint64_t argc;
    uint64_t argv;
    int base = select_base_lba();
    if (base < 0) {
        kprintf("root partition selection failed\n");
        return -1;
    }
    ext2_set_base_lba((uint64_t)base);
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
