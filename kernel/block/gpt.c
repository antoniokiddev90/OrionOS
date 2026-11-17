#include "gpt.h"
#include "ata.h"
#include <stddef.h>

#define GPT_SIGNATURE_LBA 1
#define GPT_HEADER_SIZE 92
#define GPT_ENTRY_SIZE_MIN 128
#define GPT_ENTRY_SIZE_MAX 256
#define GPT_MAX_ENTRY_COUNT 128
#define SECTOR_SIZE 512

struct gpt_header {
    char signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t disk_guid[16];
    uint64_t entries_lba;
    uint32_t entry_count;
    uint32_t entry_size;
    uint32_t entries_crc32;
} __attribute__((packed));

struct gpt_entry {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name[36];
} __attribute__((packed));

static const char gpt_magic[8] = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'};

static uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            uint32_t mask = -(crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

static int read_sector(uint64_t lba, uint32_t count, void *buffer) {
    return ata_pio_read(lba, count, buffer);
}

static int header_valid(const struct gpt_header *header, uint64_t disk_sectors) {
    if (header->signature[0] != gpt_magic[0] || header->signature[1] != gpt_magic[1] ||
        header->signature[2] != gpt_magic[2] || header->signature[3] != gpt_magic[3] ||
        header->signature[4] != gpt_magic[4] || header->signature[5] != gpt_magic[5] ||
        header->signature[6] != gpt_magic[6] || header->signature[7] != gpt_magic[7]) {
        return 0;
    }
    if (header->header_size < GPT_HEADER_SIZE || header->header_size > SECTOR_SIZE) return 0;
    if (header->entry_size < GPT_ENTRY_SIZE_MIN || header->entry_size > GPT_ENTRY_SIZE_MAX) return 0;
    if (header->entry_count == 0 || header->entry_count > GPT_MAX_ENTRY_COUNT) return 0;
    if (header->entries_lba == 0 || header->entries_lba >= disk_sectors) return 0;
    return 1;
}

static int check_header_crc(const uint8_t *sector, const struct gpt_header *header) {
    uint8_t copy[SECTOR_SIZE];
    for (size_t i = 0; i < SECTOR_SIZE; ++i) copy[i] = sector[i];
    for (size_t i = 16; i < 20; ++i) copy[i] = 0;
    return crc32(copy, header->header_size) == header->header_crc32;
}

static int check_entries_crc(const uint8_t *entries, size_t length, uint32_t expected) {
    return crc32(entries, length) == expected;
}

static int entry_used(const struct gpt_entry *entry) {
    for (size_t i = 0; i < 16; ++i) if (entry->type_guid[i]) return 1;
    return 0;
}

static void copy_name(char *out, const uint16_t *in, size_t capacity) {
    size_t i = 0;
    for (; i + 1 < capacity && in[i]; ++i) {
        out[i] = (in[i] < 0x80) ? (char)in[i] : '?';
    }
    out[i] = 0;
}

int gpt_scan(struct gpt_partition *partitions, int max) {
    uint8_t header_sector[SECTOR_SIZE];
    if (!partitions || max <= 0) return -1;
    if (read_sector(GPT_SIGNATURE_LBA, 1, header_sector) != 0) return -1;

    const struct gpt_header *header = (const struct gpt_header *)header_sector;
    uint64_t disk_sectors = 0x0FFFFFFFULL;

    if (!header_valid(header, disk_sectors)) return 0;
    if (!check_header_crc(header_sector, header)) return -1;

    size_t table_bytes = (size_t)header->entry_count * header->entry_size;
    if (table_bytes > GPT_MAX_ENTRY_COUNT * GPT_ENTRY_SIZE_MAX) return -1;

    uint8_t table[GPT_MAX_ENTRY_COUNT * GPT_ENTRY_SIZE_MAX];
    size_t sectors = (table_bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (read_sector(header->entries_lba, (uint32_t)sectors, table) != 0) return -1;
    if (!check_entries_crc(table, table_bytes, header->entries_crc32)) return -1;

    int found = 0;
    for (uint32_t i = 0; i < header->entry_count && found < max; ++i) {
        const struct gpt_entry *entry = (const struct gpt_entry *)(table + (size_t)i * header->entry_size);
        if (!entry_used(entry)) continue;
        if (entry->first_lba < header->first_usable_lba || entry->last_lba > header->last_usable_lba) continue;
        if (entry->last_lba < entry->first_lba) continue;
        partitions[found].index = i;
        partitions[found].first_lba = entry->first_lba;
        partitions[found].last_lba = entry->last_lba;
        copy_name(partitions[found].name, entry->name, GPT_NAME_LENGTH);
        ++found;
    }
    return found;
}
