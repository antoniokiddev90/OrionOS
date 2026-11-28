#ifndef FS_VFS_H
#define FS_VFS_H
#include <stddef.h>
#include <stdint.h>
#define VFS_O_RDONLY 0
#define VFS_O_WRONLY 1
#define VFS_O_RDWR 2
#define VFS_O_CREAT 64
#define VFS_O_EXCL 128
#define VFS_O_TRUNC 512
#define VFS_O_APPEND 1024
#define VFS_S_IFMT 0170000
#define VFS_S_IFREG 0100000
#define VFS_S_IFDIR 0040000
#define VFS_S_IFCHR 0020000
struct vfs_stat {
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
};
int vfs_init(void);
int vfs_open(const char *path);
int vfs_open_flags(const char *path, int flags, uint32_t mode);
int64_t vfs_read(int descriptor, void *buffer, size_t size);
int64_t vfs_write(int descriptor, const void *buffer, size_t size);
int64_t vfs_getdents(int descriptor, void *buffer, size_t size);
int vfs_close(int descriptor);
int vfs_dup(int descriptor);
int vfs_size(int descriptor, uint64_t *size);
int64_t vfs_seek(int descriptor, int64_t offset, int whence);
int vfs_stat_path(const char *path, struct vfs_stat *status);
int vfs_stat_fd(int descriptor, struct vfs_stat *status);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_is_directory(const char *path);
int vfs_is_tty(int descriptor);
int64_t vfs_ioctl(int descriptor, unsigned long request, void *argument);
int vfs_can_enter(const char *path);
int vfs_chown(const char *path, uint32_t uid, uint32_t gid);
#endif
