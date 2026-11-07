#ifndef X64_TSS_H
#define X64_TSS_H

#include <stdint.h>

void tss_init(void);
void tss_set_rsp0(uint64_t rsp0);
uint64_t tss_get_rsp0(void);

#endif
