#include <sys/mman.h>
#include <syscalls.h>
#include <stdint.h>
#include <stddef.h>

long orion_syscall4(uint64_t number, uint64_t first, uint64_t second, uint64_t third, uint64_t fourth) {
    uint64_t result;
    __asm__ volatile("int $0x9E" : "=a"(result) : "a"(number), "D"(first), "S"(second), "d"(third), "c"(fourth) : "r11", "memory");
    return (long)result;
}

void *mmap(void *address, size_t length, int protection, int flags, int descriptor, off_t offset) {
    if (!length) return MAP_FAILED;
    if (!(flags & MAP_ANONYMOUS) || (flags & MAP_SHARED) || !(flags & MAP_PRIVATE)) return MAP_FAILED;
    if (descriptor != -1 || offset != 0) return MAP_FAILED;
    if (protection & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) return MAP_FAILED;
    long result = orion_syscall4(ORION_SYS_MMAP, (uint64_t)(uintptr_t)address, (uint64_t)length, (uint64_t)protection, (uint64_t)flags);
    if (result < 0) return MAP_FAILED;
    return (void *)(uintptr_t)result;
}

int munmap(void *address, size_t length) {
    if (!address || !length) return -1;
    return (int)orion_syscall2(ORION_SYS_MUNMAP, (uint64_t)(uintptr_t)address, (uint64_t)length);
}
