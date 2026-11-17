#ifndef BLOCK_GPT_H
#define BLOCK_GPT_H

#include <stdint.h>

#define GPT_MAX_PARTITIONS 32
#define GPT_NAME_LENGTH 36

struct gpt_partition {
    uint32_t index;
    uint64_t first_lba;
    uint64_t last_lba;
    char name[GPT_NAME_LENGTH];
};

int gpt_scan(struct gpt_partition *partitions, int max);

#endif
