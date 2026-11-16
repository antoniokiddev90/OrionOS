#ifndef BLOCK_ATA_H
#define BLOCK_ATA_H
#include <stdint.h>
int ata_init(void);
int ata_pio_read(uint64_t lba, uint32_t sector_count, void *buffer);
int ata_pio_write(uint64_t lba, uint32_t sector_count, const void *buffer);
#endif
