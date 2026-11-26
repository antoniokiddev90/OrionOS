#include "elf.h"
#include "../fs/vfs.h"
#include "../mm/heap.h"
#include "../x64/paging.h"
#include "../x64/pmm.h"
#include <stddef.h>
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_PT_INTERP 3
#define ELF_ET_EXEC 2
#define ELF_EM_X86_64 62
#define USER_STACK_TOP 0x000000007FFFF000ULL
#define USER_STACK_PAGES 32
#define ELF_MAX_SIZE (16ULL * 1024 * 1024)
struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));
struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));
static uint64_t page_entry(uint64_t *pml4, uint64_t va) {
    uint64_t *table = paging_walk_in(pml4, va, 0);
    return table ? table[(va >> 12) & 0x1ff] : 0;
}
static int map_zeroed_page(uint64_t *pml4, uint64_t va) {
    if (page_entry(pml4, va) & PTE_PRESENT) return 0;
    uint64_t physical = pmm_alloc_frame();
    if (!physical) return -1;
    uint8_t *bytes = (uint8_t *)physical;
    for (size_t i = 0; i < PAGE_SIZE; ++i) bytes[i] = 0;
    if (paging_map_user_page_in(pml4, va, physical) != 0) {
        pmm_free_frame(physical);
        return -1;
    }
    return 0;
}
static int copy_to_user(uint64_t *pml4, uint64_t destination, const uint8_t *source, size_t size) {
    while (size) {
        uint64_t page = destination & PAGE_MASK;
        uint64_t entry = page_entry(pml4, page);
        if (!(entry & PTE_PRESENT) || !(entry & PTE_USER)) return -1;
        size_t offset = (size_t)(destination & (PAGE_SIZE - 1));
        size_t part = PAGE_SIZE - offset;
        if (part > size) part = size;
        uint8_t *to = (uint8_t *)(entry & PTE_ADDR_MASK) + offset;
        for (size_t i = 0; i < part; ++i) to[i] = source[i];
        destination += part;
        source += part;
        size -= part;
    }
    return 0;
}
static int map_segment(uint64_t *pml4, const uint8_t *image, size_t image_size, const struct elf64_program_header *program) {
    if (program->filesz > program->memsz || program->offset > image_size || program->filesz > image_size - program->offset) return -1;
    if (program->vaddr < USER_SPACE_BASE || program->vaddr >= USER_SPACE_END || program->memsz > USER_SPACE_END - program->vaddr) return -1;
    uint64_t start = program->vaddr & PAGE_MASK;
    uint64_t end = (program->vaddr + program->memsz + PAGE_SIZE - 1) & PAGE_MASK;
    if (end < program->vaddr || end > USER_SPACE_END) return -1;
    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        if (map_zeroed_page(pml4, address) != 0) return -1;
    }
    if (program->filesz && copy_to_user(pml4, program->vaddr, image + program->offset, (size_t)program->filesz) != 0) return -1;
    return 0;
}
static int build_stack(uint64_t *pml4, const char *const *arguments, uint64_t *stack, uint64_t *argc, uint64_t *argv) {
    const char *defaults[2] = {"/init", NULL};
    if (!arguments) arguments = defaults;
    size_t count = 0;
    while (arguments[count] && count < 16) ++count;
    if (count == 16 && arguments[count]) return -1;
    uint64_t low = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    for (uint64_t page = low; page < USER_STACK_TOP; page += PAGE_SIZE) {
        if (map_zeroed_page(pml4, page) != 0) return -1;
    }
    uint64_t cursor = USER_STACK_TOP;
    uint64_t user_arguments[16];
    for (size_t i = count; i > 0; --i) {
        size_t length = 0;
        while (arguments[i - 1][length] && length < 255) ++length;
        if (length == 255) return -1;
        ++length;
        if (cursor < low + length) return -1;
        cursor -= length;
        if (copy_to_user(pml4, cursor, (const uint8_t *)arguments[i - 1], length) != 0) return -1;
        user_arguments[i - 1] = cursor;
    }
    cursor &= ~15ULL;
    uint64_t vector[17];
    for (size_t i = 0; i < count; ++i) vector[i] = user_arguments[i];
    vector[count] = 0;
    size_t vector_size = (count + 1) * sizeof(uint64_t);
    cursor -= vector_size;
    cursor &= ~15ULL;
    if (copy_to_user(pml4, cursor, (const uint8_t *)vector, vector_size) != 0) return -1;
    *argc = count;
    *argv = cursor;
    *stack = cursor & ~15ULL;
    if ((*stack & 15ULL) != 8ULL) *stack -= 8;
    if (*stack < low) return -1;
    return 0;
}
int elf_load_path(const char *path, const char *const *arguments, uint64_t **pml4_out, uint64_t *entry_out, uint64_t *stack_out, uint64_t *argc_out, uint64_t *argv_out) {
    if (!path || !pml4_out || !entry_out || !stack_out || !argc_out || !argv_out) return -1;
    int descriptor = vfs_open(path);
    if (descriptor < 0) return -1;
    uint64_t file_size = 0;
    if (vfs_size(descriptor, &file_size) != 0 || file_size < sizeof(struct elf64_header) || file_size > ELF_MAX_SIZE) {
        vfs_close(descriptor);
        return -1;
    }
    uint8_t *image = (uint8_t *)kmalloc((size_t)file_size);
    if (!image) {
        vfs_close(descriptor);
        return -1;
    }
    int64_t read_size = vfs_read(descriptor, image, (size_t)file_size);
    vfs_close(descriptor);
    if (read_size != (int64_t)file_size) {
        kfree(image);
        return -1;
    }
    struct elf64_header *header = (struct elf64_header *)image;
    if (header->ident[0] != 0x7f || header->ident[1] != 'E' || header->ident[2] != 'L' || header->ident[3] != 'F' || header->ident[4] != 2 || header->ident[5] != 1 || header->type != ELF_ET_EXEC || header->machine != ELF_EM_X86_64 || header->version != 1 || header->phentsize != sizeof(struct elf64_program_header) || header->phnum == 0 || header->phnum > 32 || header->phoff > file_size || (uint64_t)header->phnum * header->phentsize > file_size - header->phoff) {
        kfree(image);
        return -1;
    }
    uint64_t *pml4 = paging_new_pml4();
    if (!pml4) {
        kfree(image);
        return -1;
    }
    struct elf64_program_header *programs = (struct elf64_program_header *)(image + header->phoff);
    int loaded = 0;
    int failure = 0;
    for (size_t i = 0; i < header->phnum; ++i) {
        if (programs[i].type == ELF_PT_DYNAMIC || programs[i].type == ELF_PT_INTERP) {
            failure = 1;
            break;
        }
        if (programs[i].type != ELF_PT_LOAD || !programs[i].memsz) continue;
        if (map_segment(pml4, image, (size_t)file_size, &programs[i]) != 0) {
            failure = 1;
            break;
        }
        loaded = 1;
    }
    if (!loaded || failure || header->entry < USER_SPACE_BASE || header->entry >= USER_SPACE_END || !(page_entry(pml4, header->entry) & PTE_PRESENT)) {
        paging_free_pml4(pml4);
        kfree(image);
        return -1;
    }
    uint64_t argc;
    uint64_t argv;
    uint64_t stack;
    if (build_stack(pml4, arguments, &stack, &argc, &argv) != 0) {
        paging_free_pml4(pml4);
        kfree(image);
        return -1;
    }
    uint64_t loaded_entry = header->entry;
    kfree(image);
    *pml4_out = pml4;
    *entry_out = loaded_entry;
    *stack_out = stack;
    *argc_out = argc;
    *argv_out = argv;
    return 0;
}
