#ifndef ORION_SYSCALLS_H
#define ORION_SYSCALLS_H
#include <stdint.h>
enum {
    ORION_SYS_WRITE = 1,
    ORION_SYS_READ = 2,
    ORION_SYS_OPEN = 3,
    ORION_SYS_CLOSE = 4,
    ORION_SYS_LSEEK = 5,
    ORION_SYS_FORK = 6,
    ORION_SYS_EXECVE = 7,
    ORION_SYS_WAIT4 = 8,
    ORION_SYS_GETDENTS64 = 9,
    ORION_SYS_CHDIR = 10,
    ORION_SYS_GETCWD = 11,
    ORION_SYS_STAT = 12,
    ORION_SYS_FSTAT = 13,
    ORION_SYS_MKDIR = 14,
    ORION_SYS_UNLINK = 15,
    ORION_SYS_GETPID = 16,
    ORION_SYS_EXIT = 17,
    ORION_SYS_YIELD = 18,
    ORION_SYS_DUP = 19,
    ORION_SYS_ACCESS = 20
};
long orion_syscall0(uint64_t number);
long orion_syscall1(uint64_t number, uint64_t first);
long orion_syscall2(uint64_t number, uint64_t first, uint64_t second);
long orion_syscall3(uint64_t number, uint64_t first, uint64_t second, uint64_t third);
#endif
