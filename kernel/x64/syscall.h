#ifndef X64_SYSCALL_H
#define X64_SYSCALL_H
#include <stdint.h>
void syscall_init(void);
uint64_t syscall_dispatch(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t *frame);
#endif
