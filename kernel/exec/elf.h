#ifndef EXEC_ELF_H
#define EXEC_ELF_H
#include <stdint.h>
int elf_load_path(const char *path, const char *const *arguments, uint64_t **pml4, uint64_t *entry, uint64_t *stack, uint64_t *argc, uint64_t *argv);
#endif
