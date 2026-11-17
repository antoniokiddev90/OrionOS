#ifndef FS_EXT2_H
#define FS_EXT2_H
#include <stddef.h>
#include <stdint.h>
struct ext2_file {
    uint32_t inode;
    uint32_t mode;
    uint64_t size;
    uint32_t blocks[15];
};
struct ext2_dirent {
    uint32_t inode;
    uint8_t type;
    uint8_t name_length;
    char name[256];
};
int ext2_mount(void);
int ext2_lookup(const char *path, struct ext2_file *file);
int64_t ext2_read(const struct ext2_file *file, uint64_t offset, void *buffer, size_t size);
int64_t ext2_write(struct ext2_file *file, uint64_t offset, const void *buffer, size_t size);
int ext2_truncate(struct ext2_file *file);
int ext2_create(const char *path, uint32_t mode, struct ext2_file *file);
int ext2_mkdir(const char *path);
int ext2_unlink(const char *path);
int ext2_readdir(const struct ext2_file *directory, uint64_t *offset, struct ext2_dirent *entry);
uint32_t ext2_block_size(void);
void ext2_set_base_lba(uint64_t lba);
#endif
