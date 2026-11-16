#include "ata.h"
#include <stddef.h>
#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_ALT_STATUS 0x3F6
#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_DF 0x20
#define ATA_SR_BSY 0x80
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static inline uint16_t inw(uint16_t port) { uint16_t value; __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static inline void outw(uint16_t port, uint16_t value) { __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port)); }
static void delay_400ns(void) {
    for (size_t i = 0; i < 4; ++i) (void)inb(ATA_ALT_STATUS);
}
static int wait_not_busy(void) {
    for (uint32_t i = 0; i < 1000000; ++i) {
        uint8_t status = inb(ATA_STATUS);
        if (status == 0 || status == 0xFF) return -1;
        if (!(status & ATA_SR_BSY)) return (status & (ATA_SR_ERR | ATA_SR_DF)) ? -1 : 0;
    }
    return -1;
}
static int wait_data(void) {
    for (uint32_t i = 0; i < 1000000; ++i) {
        uint8_t status = inb(ATA_STATUS);
        if (!status || status == 0xFF || (status & (ATA_SR_ERR | ATA_SR_DF))) return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
    }
    return -1;
}
static int valid_range(uint64_t lba, uint32_t sector_count, const void *buffer) {
    return buffer && sector_count && sector_count <= 256 && lba <= 0x0FFFFFFFULL && lba + sector_count - 1 <= 0x0FFFFFFFULL;
}
int ata_init(void) {
    outb(ATA_DRIVE, 0xA0);
    delay_400ns();
    return inb(ATA_STATUS) == 0xFF ? -1 : 0;
}
int ata_pio_read(uint64_t lba, uint32_t sector_count, void *buffer) {
    if (!valid_range(lba, sector_count, buffer)) return -1;
    uint16_t *words = (uint16_t *)buffer;
    uint32_t remaining = sector_count;
    while (remaining) {
        uint8_t chunk = (uint8_t)(remaining > 255 ? 255 : remaining);
        if (wait_not_busy() != 0) return -1;
        outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        delay_400ns();
        outb(ATA_ERROR, 0);
        outb(ATA_SECTOR_COUNT, chunk);
        outb(ATA_LBA_LOW, (uint8_t)lba);
        outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
        outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
        outb(ATA_COMMAND, 0x20);
        for (uint32_t sector = 0; sector < chunk; ++sector) {
            delay_400ns();
            if (wait_data() != 0) return -1;
            for (uint32_t word = 0; word < 256; ++word) *words++ = inw(ATA_DATA);
        }
        lba += chunk;
        remaining -= chunk;
    }
    return 0;
}
int ata_pio_write(uint64_t lba, uint32_t sector_count, const void *buffer) {
    if (!valid_range(lba, sector_count, buffer)) return -1;
    const uint16_t *words = (const uint16_t *)buffer;
    uint32_t remaining = sector_count;
    while (remaining) {
        uint8_t chunk = (uint8_t)(remaining > 255 ? 255 : remaining);
        if (wait_not_busy() != 0) return -1;
        outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
        delay_400ns();
        outb(ATA_ERROR, 0);
        outb(ATA_SECTOR_COUNT, chunk);
        outb(ATA_LBA_LOW, (uint8_t)lba);
        outb(ATA_LBA_MID, (uint8_t)(lba >> 8));
        outb(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
        outb(ATA_COMMAND, 0x30);
        for (uint32_t sector = 0; sector < chunk; ++sector) {
            delay_400ns();
            if (wait_data() != 0) return -1;
            for (uint32_t word = 0; word < 256; ++word) outw(ATA_DATA, *words++);
        }
        if (wait_not_busy() != 0) return -1;
        lba += chunk;
        remaining -= chunk;
    }
    return 0;
}
