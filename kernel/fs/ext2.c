#include "ext2.h"
#include "../block/ata.h"
#include <stddef.h>
static uint64_t base_lba;

void ext2_set_base_lba(uint64_t lba) {
    base_lba = lba;
}

static int disk_read(uint64_t lba, uint32_t count, void *buffer) {
    return ata_pio_read(base_lba + lba, count, buffer);
}

static int disk_write(uint64_t lba, uint32_t count, const void *buffer) {
    return ata_pio_write(base_lba + lba, count, buffer);
}
#define EXT2_MAGIC 0xEF53
#define EXT2_ROOT_INODE 2
#define EXT2_MODE_DIRECTORY 0x4000
#define EXT2_MODE_REGULAR 0x8000
#define EXT2_TYPE_UNKNOWN 0
#define EXT2_TYPE_REGULAR 1
#define EXT2_TYPE_DIRECTORY 2
static uint8_t block_buffer[4096];
static uint8_t indirect_buffer[4096];
static uint8_t bitmap_buffer[4096];
static uint32_t block_size;
static uint32_t blocks_per_group;
static uint32_t inodes_per_group;
static uint32_t inode_size;
static uint32_t group_table_block;
static uint32_t blocks_count;
static uint32_t inodes_count;
static uint32_t first_data_block;
static uint32_t first_inode;
static uint32_t group_count;
static uint32_t mounted;
static uint16_t read_u16(const uint8_t *bytes, size_t offset) {
    return (uint16_t)(bytes[offset] | ((uint16_t)bytes[offset + 1] << 8));
}
static uint32_t read_u32(const uint8_t *bytes, size_t offset) {
    return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) | ((uint32_t)bytes[offset + 2] << 16) | ((uint32_t)bytes[offset + 3] << 24);
}
static void put_u16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}
static void put_u32(uint8_t *bytes, size_t offset, uint32_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
    bytes[offset + 2] = (uint8_t)(value >> 16);
    bytes[offset + 3] = (uint8_t)(value >> 24);
}
static int read_bytes(uint64_t offset, void *destination, size_t size) {
    uint8_t *out = (uint8_t *)destination;
    uint8_t sector[512];
    while (size) {
        uint64_t lba = offset / 512;
        uint32_t within = (uint32_t)(offset % 512);
        if (disk_read(lba, 1, sector) != 0) return -1;
        size_t part = 512 - within;
        if (part > size) part = size;
        for (size_t i = 0; i < part; ++i) out[i] = sector[within + i];
        out += part;
        offset += part;
        size -= part;
    }
    return 0;
}
static int write_bytes(uint64_t offset, const void *source, size_t size) {
    const uint8_t *in = (const uint8_t *)source;
    uint8_t sector[512];
    while (size) {
        uint64_t lba = offset / 512;
        uint32_t within = (uint32_t)(offset % 512);
        size_t part = 512 - within;
        if (part > size) part = size;
        if (within || part != 512) {
            if (disk_read(lba, 1, sector) != 0) return -1;
        }
        for (size_t i = 0; i < part; ++i) sector[within + i] = in[i];
        if (disk_write(lba, 1, sector) != 0) return -1;
        in += part;
        offset += part;
        size -= part;
    }
    return 0;
}
static int read_block(uint32_t block, uint8_t *buffer) {
    if (!mounted || !block || block >= blocks_count) return -1;
    return disk_read((uint64_t)block * (block_size / 512), block_size / 512, buffer);
}
static int write_block(uint32_t block, const uint8_t *buffer) {
    if (!mounted || !block || block >= blocks_count) return -1;
    return disk_write((uint64_t)block * (block_size / 512), block_size / 512, buffer);
}
static int inode_location(uint32_t inode_number, uint64_t *offset, uint32_t *group_out) {
    if (!inode_number || inode_number > inodes_count) return -1;
    uint32_t group = (inode_number - 1) / inodes_per_group;
    uint32_t index = (inode_number - 1) % inodes_per_group;
    uint64_t descriptor_offset = (uint64_t)group_table_block * block_size + (uint64_t)group * 32;
    uint8_t descriptor[32];
    if (read_bytes(descriptor_offset, descriptor, sizeof(descriptor)) != 0) return -1;
    uint32_t table_block = read_u32(descriptor, 8);
    *offset = (uint64_t)table_block * block_size + (uint64_t)index * inode_size;
    if (group_out) *group_out = group;
    return 0;
}
static int read_inode(uint32_t inode_number, struct ext2_file *file) {
    if (!file) return -1;
    uint64_t inode_offset;
    if (inode_location(inode_number, &inode_offset, NULL) != 0) return -1;
    uint8_t raw[256];
    if (inode_size > sizeof(raw) || read_bytes(inode_offset, raw, inode_size) != 0) return -1;
    file->inode = inode_number;
    file->mode = read_u16(raw, 0);
    file->uid = read_u16(raw, 2);
    file->gid = read_u16(raw, 24);
    file->allocated_blocks = read_u32(raw, 28);
    file->size = read_u32(raw, 4);
    if ((file->mode & EXT2_MODE_REGULAR) && inode_size >= 112) file->size |= (uint64_t)read_u32(raw, 108) << 32;
    for (size_t i = 0; i < 15; ++i) file->blocks[i] = read_u32(raw, 40 + i * 4);
    return 0;
}
static int write_inode(const struct ext2_file *file, uint16_t links, uint32_t size_high) {
    uint64_t inode_offset;
    if (!file || inode_location(file->inode, &inode_offset, NULL) != 0) return -1;
    uint8_t raw[256];
    if (inode_size > sizeof(raw) || read_bytes(inode_offset, raw, inode_size) != 0) return -1;
    put_u16(raw, 0, (uint16_t)file->mode);
    put_u32(raw, 4, (uint32_t)file->size);
    put_u16(raw, 2, (uint16_t)file->uid);
    put_u16(raw, 24, (uint16_t)file->gid);
    put_u16(raw, 26, links);
    put_u32(raw, 28, file->allocated_blocks);
    for (size_t i = 0; i < 15; ++i) put_u32(raw, 40 + i * 4, file->blocks[i]);
    if ((file->mode & EXT2_MODE_REGULAR) && inode_size >= 112) put_u32(raw, 108, size_high);
    return write_bytes(inode_offset, raw, inode_size);
}
static uint32_t file_data_block(const struct ext2_file *file, uint64_t index) {
    if (index < 12) return file->blocks[index];
    index -= 12;
    uint64_t pointers = block_size / 4;
    if (index < pointers) {
        if (!file->blocks[12]) return 0;
        if (read_block(file->blocks[12], indirect_buffer) != 0) return UINT32_MAX;
        return read_u32(indirect_buffer, (size_t)index * 4);
    }
    index -= pointers;
    if (index >= pointers * pointers) return UINT32_MAX;
    if (!file->blocks[13]) return 0;
    if (read_block(file->blocks[13], indirect_buffer) != 0) return UINT32_MAX;
    uint32_t second_level = read_u32(indirect_buffer, (size_t)(index / pointers) * 4);
    if (!second_level) return 0;
    if (read_block(second_level, indirect_buffer) != 0) return UINT32_MAX;
    return read_u32(indirect_buffer, (size_t)(index % pointers) * 4);
}
int ext2_mount(void) {
    mounted = 0;
    uint8_t superblock[1024];
    if (read_bytes(1024, superblock, sizeof(superblock)) != 0 || read_u16(superblock, 56) != EXT2_MAGIC) return -1;
    uint32_t log_block_size = read_u32(superblock, 24);
    if (log_block_size > 2) return -1;
    block_size = 1024U << log_block_size;
    blocks_count = read_u32(superblock, 4);
    inodes_count = read_u32(superblock, 0);
    first_data_block = read_u32(superblock, 20);
    blocks_per_group = read_u32(superblock, 32);
    inodes_per_group = read_u32(superblock, 40);
    first_inode = read_u32(superblock, 84);
    inode_size = read_u32(superblock, 76) ? read_u16(superblock, 88) : 128;
    if (inode_size < 128 || inode_size > 256 || !blocks_per_group || !inodes_per_group || !blocks_count || !inodes_count) return -1;
    if (!first_inode) first_inode = 11;
    group_table_block = block_size == 1024 ? 2 : 1;
    group_count = (blocks_count + blocks_per_group - 1) / blocks_per_group;
    mounted = 1;
    struct ext2_file root;
    if (read_inode(EXT2_ROOT_INODE, &root) != 0 || (root.mode & EXT2_MODE_DIRECTORY) != EXT2_MODE_DIRECTORY) {
        mounted = 0;
        return -1;
    }
    return 0;
}
static int lookup_child(const struct ext2_file *directory, const char *name, size_t name_length, struct ext2_file *result) {
    if ((directory->mode & EXT2_MODE_DIRECTORY) != EXT2_MODE_DIRECTORY) return -1;
    uint64_t offset = 0;
    while (offset < directory->size) {
        uint64_t block_index = offset / block_size;
        uint32_t physical = file_data_block(directory, block_index);
        if (!physical || physical == UINT32_MAX || read_block(physical, block_buffer) != 0) return -1;
        size_t limit = (size_t)(directory->size - offset);
        if (limit > block_size) limit = block_size;
        size_t position = 0;
        while (position + 8 <= limit) {
            uint32_t inode_number = read_u32(block_buffer, position);
            uint16_t record_length = read_u16(block_buffer, position + 4);
            uint8_t entry_name_length = block_buffer[position + 6];
            if (record_length < 8 || position + record_length > limit || entry_name_length > record_length - 8) return -1;
            if (inode_number && entry_name_length == name_length) {
                size_t i = 0;
                while (i < name_length && block_buffer[position + 8 + i] == (uint8_t)name[i]) ++i;
                if (i == name_length) return read_inode(inode_number, result);
            }
            position += record_length;
        }
        offset += limit;
    }
    return -1;
}
int ext2_lookup(const char *path, struct ext2_file *file) {
    if (!mounted || !path || !file) return -1;
    struct ext2_file current;
    if (read_inode(EXT2_ROOT_INODE, &current) != 0) return -1;
    while (*path == '/') ++path;
    if (!*path) {
        *file = current;
        return 0;
    }
    while (*path) {
        const char *end = path;
        while (*end && *end != '/') ++end;
        size_t length = (size_t)(end - path);
        if (!length || length > 255 || lookup_child(&current, path, length, file) != 0) return -1;
        current = *file;
        path = end;
        while (*path == '/') ++path;
    }
    *file = current;
    return 0;
}
int64_t ext2_read(const struct ext2_file *file, uint64_t offset, void *buffer, size_t size) {
    if (!mounted || !file || (!buffer && size) || offset > file->size) return -1;
    uint64_t available = file->size - offset;
    if (size > available) size = (size_t)available;
    uint8_t *out = (uint8_t *)buffer;
    size_t remaining = size;
    while (remaining) {
        uint64_t block_index = offset / block_size;
        size_t block_offset = (size_t)(offset % block_size);
        uint32_t physical = file_data_block(file, block_index);
        if (physical == UINT32_MAX) return -1;
        size_t part = block_size - block_offset;
        if (part > remaining) part = remaining;
        if (!physical) {
            for (size_t i = 0; i < part; ++i) out[i] = 0;
        } else {
            if (read_block(physical, block_buffer) != 0) return -1;
            for (size_t i = 0; i < part; ++i) out[i] = block_buffer[block_offset + i];
        }
        out += part;
        offset += part;
        remaining -= part;
    }
    return (int64_t)size;
}
static int update_group_counts(uint32_t group, int32_t free_blocks_delta, int32_t free_inodes_delta, int32_t used_dirs_delta) {
    uint8_t descriptor[32];
    uint64_t descriptor_offset = (uint64_t)group_table_block * block_size + (uint64_t)group * 32;
    uint8_t superblock[1024];
    if (read_bytes(descriptor_offset, descriptor, sizeof(descriptor)) != 0 || read_bytes(1024, superblock, sizeof(superblock)) != 0) return -1;
    if (free_blocks_delta) {
        put_u16(descriptor, 12, (uint16_t)(read_u16(descriptor, 12) + free_blocks_delta));
        put_u32(superblock, 12, (uint32_t)(read_u32(superblock, 12) + free_blocks_delta));
    }
    if (free_inodes_delta) {
        put_u16(descriptor, 14, (uint16_t)(read_u16(descriptor, 14) + free_inodes_delta));
        put_u32(superblock, 16, (uint32_t)(read_u32(superblock, 16) + free_inodes_delta));
    }
    if (used_dirs_delta) put_u16(descriptor, 16, (uint16_t)(read_u16(descriptor, 16) + used_dirs_delta));
    if (write_bytes(descriptor_offset, descriptor, sizeof(descriptor)) != 0 || write_bytes(1024, superblock, sizeof(superblock)) != 0) return -1;
    return 0;
}
static uint32_t allocate_block(void) {
    for (uint32_t group = 0; group < group_count; ++group) {
        uint8_t descriptor[32];
        uint64_t descriptor_offset = (uint64_t)group_table_block * block_size + (uint64_t)group * 32;
        if (read_bytes(descriptor_offset, descriptor, sizeof(descriptor)) != 0) return 0;
        if (!read_u16(descriptor, 12)) continue;
        uint32_t bitmap_block = read_u32(descriptor, 0);
        if (read_block(bitmap_block, bitmap_buffer) != 0) return 0;
        uint32_t group_start = first_data_block + group * blocks_per_group;
        for (uint32_t bit = 0; bit < blocks_per_group && bit < block_size * 8; ++bit) {
            uint32_t block = group_start + bit;
            if (block >= blocks_count) break;
            if (!(bitmap_buffer[bit / 8] & (1U << (bit % 8)))) {
                bitmap_buffer[bit / 8] |= (uint8_t)(1U << (bit % 8));
                if (write_block(bitmap_block, bitmap_buffer) != 0 || update_group_counts(group, -1, 0, 0) != 0) return 0;
                for (size_t i = 0; i < block_size; ++i) block_buffer[i] = 0;
                if (write_block(block, block_buffer) != 0) return 0;
                return block;
            }
        }
    }
    return 0;
}
static uint32_t allocate_inode(int directory) {
    for (uint32_t group = 0; group < group_count; ++group) {
        uint8_t descriptor[32];
        uint64_t descriptor_offset = (uint64_t)group_table_block * block_size + (uint64_t)group * 32;
        if (read_bytes(descriptor_offset, descriptor, sizeof(descriptor)) != 0) return 0;
        if (!read_u16(descriptor, 14)) continue;
        uint32_t bitmap_block = read_u32(descriptor, 4);
        if (read_block(bitmap_block, bitmap_buffer) != 0) return 0;
        for (uint32_t bit = 0; bit < inodes_per_group && bit < block_size * 8; ++bit) {
            uint32_t inode = group * inodes_per_group + bit + 1;
            if (inode < first_inode || inode > inodes_count) continue;
            if (!(bitmap_buffer[bit / 8] & (1U << (bit % 8)))) {
                bitmap_buffer[bit / 8] |= (uint8_t)(1U << (bit % 8));
                if (write_block(bitmap_block, bitmap_buffer) != 0 || update_group_counts(group, 0, -1, directory ? 1 : 0) != 0) return 0;
                return inode;
            }
        }
    }
    return 0;
}
static int allocate_file_block(struct ext2_file *file, uint64_t index, uint32_t *physical) {
    uint32_t sectors_per_block = block_size / 512;
    uint64_t pointers = block_size / 4;
    if (index < 12) {
        if (!file->blocks[index]) {
            file->blocks[index] = allocate_block();
            if (!file->blocks[index]) return -1;
            file->allocated_blocks += sectors_per_block;
        }
        *physical = file->blocks[index];
        return 0;
    }
    index -= 12;
    if (index < pointers) {
        if (!file->blocks[12]) {
            file->blocks[12] = allocate_block();
            if (!file->blocks[12]) return -1;
            file->allocated_blocks += sectors_per_block;
            for (size_t i = 0; i < block_size; ++i) indirect_buffer[i] = 0;
            if (write_block(file->blocks[12], indirect_buffer) != 0) return -1;
        }
        if (read_block(file->blocks[12], indirect_buffer) != 0) return -1;
        uint32_t data_block = read_u32(indirect_buffer, (size_t)index * 4);
        if (!data_block) {
            data_block = allocate_block();
            if (!data_block) return -1;
            put_u32(indirect_buffer, (size_t)index * 4, data_block);
            if (write_block(file->blocks[12], indirect_buffer) != 0) return -1;
            file->allocated_blocks += sectors_per_block;
        }
        *physical = data_block;
        return 0;
    }
    index -= pointers;
    if (index >= pointers * pointers) return -1;
    if (!file->blocks[13]) {
        file->blocks[13] = allocate_block();
        if (!file->blocks[13]) return -1;
        file->allocated_blocks += sectors_per_block;
        for (size_t i = 0; i < block_size; ++i) indirect_buffer[i] = 0;
        if (write_block(file->blocks[13], indirect_buffer) != 0) return -1;
    }
    uint32_t first_level_index = (uint32_t)(index / pointers);
    uint32_t second_level_index = (uint32_t)(index % pointers);
    if (read_block(file->blocks[13], indirect_buffer) != 0) return -1;
    uint32_t second_level = read_u32(indirect_buffer, (size_t)first_level_index * 4);
    if (!second_level) {
        second_level = allocate_block();
        if (!second_level) return -1;
        put_u32(indirect_buffer, (size_t)first_level_index * 4, second_level);
        if (write_block(file->blocks[13], indirect_buffer) != 0) return -1;
        file->allocated_blocks += sectors_per_block;
        for (size_t i = 0; i < block_size; ++i) indirect_buffer[i] = 0;
        if (write_block(second_level, indirect_buffer) != 0) return -1;
    }
    if (read_block(second_level, indirect_buffer) != 0) return -1;
    uint32_t data_block = read_u32(indirect_buffer, (size_t)second_level_index * 4);
    if (!data_block) {
        data_block = allocate_block();
        if (!data_block) return -1;
        put_u32(indirect_buffer, (size_t)second_level_index * 4, data_block);
        if (write_block(second_level, indirect_buffer) != 0) return -1;
        file->allocated_blocks += sectors_per_block;
    }
    *physical = data_block;
    return 0;
}
int64_t ext2_write(struct ext2_file *file, uint64_t offset, const void *buffer, size_t size) {
    if (!mounted || !file || (!buffer && size) || (file->mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY) return -1;
    uint64_t pointers = block_size / 4;
    uint64_t maximum_size = (12 + pointers + pointers * pointers) * block_size;
    if (offset > maximum_size || size > maximum_size - offset) return -1;
    const uint8_t *in = (const uint8_t *)buffer;
    size_t remaining = size;
    while (remaining) {
        uint64_t index = offset / block_size;
        size_t within = (size_t)(offset % block_size);
        size_t part = block_size - within;
        if (part > remaining) part = remaining;
        uint32_t physical;
        if (allocate_file_block(file, index, &physical) != 0) return -1;
        if (read_block(physical, block_buffer) != 0) return -1;
        for (size_t i = 0; i < part; ++i) block_buffer[within + i] = in[i];
        if (write_block(physical, block_buffer) != 0) return -1;
        in += part;
        offset += part;
        remaining -= part;
    }
    if (offset > file->size) file->size = offset;
    if (write_inode(file, 1, (uint32_t)(file->size >> 32)) != 0) return -1;
    return (int64_t)size;
}
int ext2_truncate(struct ext2_file *file) {
    if (!mounted || !file || (file->mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY) return -1;
    file->size = 0;
    return write_inode(file, 1, 0);
}
static size_t aligned_entry_size(size_t name_length) {
    return (8 + name_length + 3) & ~(size_t)3;
}
static int insert_directory_entry(struct ext2_file *directory, const char *name, uint32_t inode_number, uint8_t type) {
    size_t name_length = 0;
    while (name[name_length] && name_length < 255) ++name_length;
    if (!name_length || name_length > 255) return -1;
    size_t needed = aligned_entry_size(name_length);
    for (uint64_t offset = 0; offset < directory->size; ) {
        uint64_t index = offset / block_size;
        size_t block_offset = (size_t)(offset % block_size);
        uint32_t physical = file_data_block(directory, index);
        if (!physical || physical == UINT32_MAX || read_block(physical, block_buffer) != 0) return -1;
        size_t limit = block_size - block_offset;
        if (directory->size - offset < limit) limit = (size_t)(directory->size - offset);
        for (size_t pos = 0; pos + 8 <= limit; ) {
            uint16_t record_length = read_u16(block_buffer, block_offset + pos + 4);
            uint8_t old_name_length = block_buffer[block_offset + pos + 6];
            uint32_t old_inode = read_u32(block_buffer, block_offset + pos);
            if (record_length < 8 || pos + record_length > limit) return -1;
            size_t actual = old_inode ? aligned_entry_size(old_name_length) : 0;
            if (!old_inode && record_length >= needed) {
                put_u32(block_buffer, block_offset + pos, inode_number);
                put_u16(block_buffer, block_offset + pos + 4, record_length);
                block_buffer[block_offset + pos + 6] = (uint8_t)name_length;
                block_buffer[block_offset + pos + 7] = type;
                for (size_t i = 0; i < name_length; ++i) block_buffer[block_offset + pos + 8 + i] = (uint8_t)name[i];
                if (write_block(physical, block_buffer) != 0) return -1;
                return 0;
            }
            if (old_inode && record_length >= actual + needed) {
                put_u16(block_buffer, block_offset + pos + 4, (uint16_t)actual);
                size_t new_pos = pos + actual;
                put_u32(block_buffer, block_offset + new_pos, inode_number);
                put_u16(block_buffer, block_offset + new_pos + 4, (uint16_t)(record_length - actual));
                block_buffer[block_offset + new_pos + 6] = (uint8_t)name_length;
                block_buffer[block_offset + new_pos + 7] = type;
                for (size_t i = 0; i < name_length; ++i) block_buffer[block_offset + new_pos + 8 + i] = (uint8_t)name[i];
                if (write_block(physical, block_buffer) != 0) return -1;
                return 0;
            }
            pos += record_length;
        }
        offset += limit;
    }
    uint64_t new_index = directory->size / block_size;
    size_t new_offset = (size_t)(directory->size % block_size);
    if (new_offset) return -1;
    uint32_t physical;
    if (allocate_file_block(directory, new_index, &physical) != 0) return -1;
    for (size_t i = 0; i < block_size; ++i) block_buffer[i] = 0;
    put_u32(block_buffer, 0, inode_number);
    put_u16(block_buffer, 4, (uint16_t)block_size);
    block_buffer[6] = (uint8_t)name_length;
    block_buffer[7] = type;
    for (size_t i = 0; i < name_length; ++i) block_buffer[8 + i] = (uint8_t)name[i];
    if (write_block(physical, block_buffer) != 0) return -1;
    directory->size += block_size;
    return write_inode(directory, 2, 0);
}
static int split_parent(const char *path, char *parent, char *name) {
    if (!path || !*path) return -1;
    size_t length = 0;
    while (path[length] && length < 255) ++length;
    if (path[length]) return -1;
    while (length > 1 && path[length - 1] == '/') --length;
    size_t split = length;
    while (split && path[split - 1] != '/') --split;
    size_t name_length = length - split;
    if (!name_length || name_length > 255) return -1;
    for (size_t i = 0; i < name_length; ++i) name[i] = path[split + i];
    name[name_length] = 0;
    if (!split) {
        parent[0] = '.';
        parent[1] = 0;
    } else if (split == 1) {
        parent[0] = '/';
        parent[1] = 0;
    } else {
        for (size_t i = 0; i < split - 1; ++i) parent[i] = path[i];
        parent[split - 1] = 0;
    }
    if (name_length == 1 && name[0] == '.') return -1;
    if (name_length == 2 && name[0] == '.' && name[1] == '.') return -1;
    return 0;
}
static int create_node(const char *path, uint32_t mode, uint32_t uid, uint32_t gid, struct ext2_file *out) {
    char parent_path[256];
    char name[256];
    struct ext2_file parent;
    if (split_parent(path, parent_path, name) != 0 || ext2_lookup(parent_path, &parent) != 0) return -1;
    struct ext2_file existing;
    size_t name_length = 0;
    while (name[name_length]) ++name_length;
    if (lookup_child(&parent, name, name_length, &existing) == 0) return -1;
    uint32_t inode_number = allocate_inode((mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY);
    if (!inode_number) return -1;
    struct ext2_file node;
    node.inode = inode_number;
    node.mode = mode;
    node.uid = uid;
    node.gid = gid;
    node.allocated_blocks = 0;
    node.size = 0;
    for (size_t i = 0; i < 15; ++i) node.blocks[i] = 0;
    if ((mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY) {
        uint32_t data_block = allocate_block();
        if (!data_block) return -1;
        node.blocks[0] = data_block;
        node.allocated_blocks = block_size / 512;
        node.size = block_size;
        for (size_t i = 0; i < block_size; ++i) block_buffer[i] = 0;
        put_u32(block_buffer, 0, inode_number);
        put_u16(block_buffer, 4, 12);
        block_buffer[6] = 1;
        block_buffer[7] = EXT2_TYPE_DIRECTORY;
        block_buffer[8] = '.';
        put_u32(block_buffer, 12, parent.inode);
        put_u16(block_buffer, 16, (uint16_t)(block_size - 12));
        block_buffer[18] = 2;
        block_buffer[19] = EXT2_TYPE_DIRECTORY;
        block_buffer[20] = '.';
        block_buffer[21] = '.';
        if (write_block(data_block, block_buffer) != 0) return -1;
    }
    if (write_inode(&node, (mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY ? 2 : 1, 0) != 0) return -1;
    if (insert_directory_entry(&parent, name, inode_number, (mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY ? EXT2_TYPE_DIRECTORY : EXT2_TYPE_REGULAR) != 0) return -1;
    if ((mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY) {
        uint64_t parent_offset;
        uint8_t raw[256];
        if (inode_location(parent.inode, &parent_offset, NULL) == 0 && read_bytes(parent_offset, raw, inode_size) == 0) {
            put_u16(raw, 26, (uint16_t)(read_u16(raw, 26) + 1));
            write_bytes(parent_offset, raw, inode_size);
        }
    }
    if (out) *out = node;
    return 0;
}
int ext2_create(const char *path, uint32_t mode, uint32_t uid, uint32_t gid, struct ext2_file *file) {
    if (!mounted) return -1;
    if ((mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY) return -1;
    if (create_node(path, EXT2_MODE_REGULAR | (mode & 0777), uid, gid, file) != 0) return -1;
    return 0;
}
int ext2_mkdir(const char *path, uint32_t uid, uint32_t gid) {
    if (!mounted) return -1;
    return create_node(path, EXT2_MODE_DIRECTORY | 0755, uid, gid, NULL);
}
int ext2_unlink(const char *path) {
    char parent_path[256];
    char name[256];
    struct ext2_file parent;
    if (!mounted || split_parent(path, parent_path, name) != 0 || ext2_lookup(parent_path, &parent) != 0) return -1;
    uint64_t offset = 0;
    while (offset < parent.size) {
        uint64_t index = offset / block_size;
        size_t block_offset = (size_t)(offset % block_size);
        uint32_t physical = file_data_block(&parent, index);
        if (!physical || physical == UINT32_MAX || read_block(physical, block_buffer) != 0) return -1;
        size_t limit = block_size - block_offset;
        if (parent.size - offset < limit) limit = (size_t)(parent.size - offset);
        for (size_t pos = 0; pos + 8 <= limit; ) {
            uint16_t record_length = read_u16(block_buffer, block_offset + pos + 4);
            uint8_t name_length = block_buffer[block_offset + pos + 6];
            uint32_t inode_number = read_u32(block_buffer, block_offset + pos);
            if (record_length < 8 || pos + record_length > limit) return -1;
            size_t wanted = 0;
            while (name[wanted] && wanted < 255) ++wanted;
            size_t j = 0;
            while (j < wanted && j < name_length && block_buffer[block_offset + pos + 8 + j] == (uint8_t)name[j]) ++j;
            if (inode_number && j == wanted && wanted == name_length) {
                struct ext2_file target;
                if (read_inode(inode_number, &target) != 0 || (target.mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY) return -1;
                if (pos) {
                    size_t previous = 0;
                    while (previous + 8 < pos) previous += read_u16(block_buffer, block_offset + previous + 4);
                    if (previous < pos) return -1;
                    put_u16(block_buffer, block_offset + previous + 4, (uint16_t)(read_u16(block_buffer, block_offset + previous + 4) + record_length));
                } else {
                    put_u32(block_buffer, block_offset + pos, 0);
                }
                if (write_block(physical, block_buffer) != 0) return -1;
                uint64_t target_offset;
                uint8_t raw[256];
                if (inode_location(inode_number, &target_offset, NULL) == 0 && read_bytes(target_offset, raw, inode_size) == 0) {
                    put_u16(raw, 26, 0);
                    put_u32(raw, 20, 1);
                    write_bytes(target_offset, raw, inode_size);
                }
                return 0;
            }
            pos += record_length;
        }
        offset += limit;
    }
    return -1;
}
int ext2_chown(const char *path, uint32_t uid, uint32_t gid) {
    struct ext2_file file;
    uint64_t offset;
    uint8_t raw[256];
    if (!mounted || ext2_lookup(path, &file) != 0 || inode_location(file.inode, &offset, NULL) != 0) return -1;
    if (inode_size > sizeof(raw) || read_bytes(offset, raw, inode_size) != 0) return -1;
    put_u16(raw, 2, (uint16_t)uid);
    put_u16(raw, 24, (uint16_t)gid);
    return write_bytes(offset, raw, inode_size);
}

int ext2_readdir(const struct ext2_file *directory, uint64_t *offset, struct ext2_dirent *entry) {
    if (!mounted || !directory || !offset || !entry || (directory->mode & EXT2_MODE_DIRECTORY) != EXT2_MODE_DIRECTORY) return -1;
    while (*offset < directory->size) {
        uint64_t current = *offset;
        uint64_t index = current / block_size;
        size_t block_offset = (size_t)(current % block_size);
        uint32_t physical = file_data_block(directory, index);
        if (!physical || physical == UINT32_MAX || read_block(physical, block_buffer) != 0) return -1;
        uint16_t record_length = read_u16(block_buffer, block_offset + 4);
        if (record_length < 8 || block_offset + record_length > block_size) return -1;
        uint32_t inode_number = read_u32(block_buffer, block_offset);
        uint8_t name_length = block_buffer[block_offset + 6];
        uint8_t type = block_buffer[block_offset + 7];
        if (name_length > record_length - 8) return -1;
        *offset += record_length;
        if (!inode_number) continue;
        entry->inode = inode_number;
        entry->type = type;
        entry->name_length = name_length;
        for (size_t i = 0; i < name_length; ++i) entry->name[i] = (char)block_buffer[block_offset + 8 + i];
        entry->name[name_length] = 0;
        return 1;
    }
    return 0;
}
uint32_t ext2_block_size(void) { return block_size; }
