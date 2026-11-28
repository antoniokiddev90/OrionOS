#include "vfs.h"
#include "ext2.h"
#include "../sched/task.h"
#include "../x64/atkbd.h"
#include "../x64/framebuffer.h"
#include "../x64/ps2mouse.h"
#include <stddef.h>
#define VFS_MAX_OPEN 64
#define VFS_MAX_TMP 24
#define VFS_TMP_SIZE 8192
#define VFS_KIND_EXT2 1
#define VFS_KIND_NULL 2
#define VFS_KIND_ZERO 3
#define VFS_KIND_TTY 4
#define VFS_KIND_PROC 5
#define VFS_KIND_TMP 6
#define VFS_KIND_DIRECTORY 7
#define VFS_KIND_TMP_DIRECTORY 8
#define VFS_KIND_DEV_DIRECTORY 9
#define VFS_KIND_PROC_DIRECTORY 10
#define VFS_KIND_PROC_SELF_DIRECTORY 11
#define VFS_KIND_INPUT_DIRECTORY 12
#define VFS_KIND_FRAMEBUFFER 13
#define VFS_KIND_KEYBOARD 14
#define VFS_KIND_MOUSE 15
#define EXT2_MODE_DIRECTORY 0x4000
#define EXT2_MODE_REGULAR 0x8000
struct vfs_descriptor {
    struct ext2_file file;
    uint64_t offset;
    uint32_t used;
    uint32_t kind;
    int flags;
    int tmp_index;
    char data[256];
    size_t data_size;
};
struct tmp_file {
    uint32_t used;
    char path[128];
    uint8_t data[VFS_TMP_SIZE];
    size_t size;
};
struct linux_dirent64 {
    uint64_t inode;
    int64_t offset;
    uint16_t record_length;
    uint8_t type;
    char name[];
} __attribute__((packed));
static struct vfs_descriptor descriptors[VFS_MAX_OPEN];
static struct tmp_file tmp_files[VFS_MAX_TMP];
static int string_equal(const char *left, const char *right) {
    size_t i = 0;
    while (left[i] && right[i] && left[i] == right[i]) ++i;
    return left[i] == right[i];
}
static size_t string_length(const char *text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}
static int string_copy(char *destination, size_t capacity, const char *source) {
    size_t length = string_length(source);
    if (length + 1 > capacity) return -1;
    for (size_t i = 0; i <= length; ++i) destination[i] = source[i];
    return 0;
}
static int path_prefix(const char *path, const char *prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (path[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}
static int tmp_path_valid(const char *path) {
    if (!path_prefix(path, "/tmp/") || !path[5]) return 0;
    for (size_t i = 5; path[i]; ++i) if (path[i] == '/') return 0;
    return 1;
}
static int tmp_find(const char *path) {
    for (int i = 0; i < VFS_MAX_TMP; ++i) if (tmp_files[i].used && string_equal(tmp_files[i].path, path)) return i;
    return -1;
}
static int tmp_create(const char *path) {
    if (!tmp_path_valid(path)) return -1;
    int existing = tmp_find(path);
    if (existing >= 0) return existing;
    for (int i = 0; i < VFS_MAX_TMP; ++i) {
        if (!tmp_files[i].used) {
            tmp_files[i].used = 1;
            tmp_files[i].size = 0;
            if (string_copy(tmp_files[i].path, sizeof(tmp_files[i].path), path) != 0) {
                tmp_files[i].used = 0;
                return -1;
            }
            return i;
        }
    }
    return -1;
}
static int allocate_descriptor(void) {
    for (int i = 3; i < VFS_MAX_OPEN; ++i) {
        if (!descriptors[i].used) {
            descriptors[i].used = 1;
            descriptors[i].offset = 0;
            descriptors[i].kind = 0;
            descriptors[i].flags = VFS_O_RDONLY;
            descriptors[i].tmp_index = -1;
            descriptors[i].data_size = 0;
            return i;
        }
    }
    return -1;
}
static int set_proc_data(struct vfs_descriptor *descriptor, const char *text) {
    size_t length = string_length(text);
    if (length >= sizeof(descriptor->data)) return -1;
    for (size_t i = 0; i <= length; ++i) descriptor->data[i] = text[i];
    descriptor->data_size = length;
    descriptor->kind = VFS_KIND_PROC;
    return 0;
}
static int virtual_directory_kind(const char *path) {
    if (string_equal(path, "/dev") || string_equal(path, "/dev/")) return VFS_KIND_DEV_DIRECTORY;
    if (string_equal(path, "/dev/input") || string_equal(path, "/dev/input/")) return VFS_KIND_INPUT_DIRECTORY;
    if (string_equal(path, "/proc") || string_equal(path, "/proc/")) return VFS_KIND_PROC_DIRECTORY;
    if (string_equal(path, "/proc/self") || string_equal(path, "/proc/self/")) return VFS_KIND_PROC_SELF_DIRECTORY;
    if (string_equal(path, "/tmp") || string_equal(path, "/tmp/")) return VFS_KIND_TMP_DIRECTORY;
    return 0;
}
static int is_virtual_path(const char *path) {
    return path_prefix(path, "/dev/") || path_prefix(path, "/proc/") || path_prefix(path, "/tmp/") || virtual_directory_kind(path);
}
#define VFS_PERM_EXEC 1
#define VFS_PERM_WRITE 2
#define VFS_PERM_READ 4

static uint32_t current_uid(void) {
    struct task *task = task_current();
    return task ? task->uid : 0;
}

static uint32_t current_gid(void) {
    struct task *task = task_current();
    return task ? task->gid : 0;
}

static uint64_t framebuffer_size_bytes(void) {
    struct orion_fb_info info;
    return framebuffer_get_info(&info) == 0 ? info.size : 0;
}

static int mode_allows(uint32_t mode, uint32_t owner_uid, uint32_t owner_gid, uint32_t want) {
    uint32_t uid = current_uid();
    if (uid == 0) return 1;
    uint32_t bits;
    if (uid == owner_uid) bits = (mode >> 6) & 7;
    else if (current_gid() == owner_gid) bits = (mode >> 3) & 7;
    else bits = mode & 7;
    return (bits & want) == want;
}

static int device_open_allows(uint32_t mode, uint32_t owner_gid, int flags) {
    uint32_t want = 0;
    int access = flags & 3;
    if (access != VFS_O_WRONLY) want |= VFS_PERM_READ;
    if (access != VFS_O_RDONLY) want |= VFS_PERM_WRITE;
    return mode_allows(mode, 0, owner_gid, want);
}

static int framebuffer_access_allowed(void) {
    return current_uid() == 0 || current_gid() == ORION_VIDEO_GID;
}

static int open_allows(const struct ext2_file *file, int flags) {
    uint32_t want = 0;
    int access = flags & 3;
    if (access != VFS_O_WRONLY) want |= VFS_PERM_READ;
    if (access != VFS_O_RDONLY) want |= VFS_PERM_WRITE;
    return mode_allows(file->mode, file->uid, file->gid, want);
}

static int parent_allows(const char *path, uint32_t want) {
    if (current_uid() == 0) return 1;
    size_t length = string_length(path);
    while (length > 1 && path[length - 1] == '/') --length;
    size_t split = length;
    while (split && path[split - 1] != '/') --split;
    if (!split) return 0;
    char parent[256];
    size_t parent_length = split > 1 ? split - 1 : 1;
    if (parent_length + 1 > sizeof(parent)) return 0;
    for (size_t i = 0; i < parent_length; ++i) parent[i] = path[i];
    parent[parent_length] = 0;
    struct ext2_file directory;
    if (ext2_lookup(parent, &directory) != 0) return 0;
    return mode_allows(directory.mode, directory.uid, directory.gid, want);
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!path || is_virtual_path(path)) return -1;
    return ext2_chown(path, uid, gid);
}

int vfs_can_enter(const char *path) {
    if (!path) return 0;
    if (is_virtual_path(path)) return 1;
    struct ext2_file directory;
    if (ext2_lookup(path, &directory) != 0) return 0;
    return mode_allows(directory.mode, directory.uid, directory.gid, VFS_PERM_EXEC);
}

int vfs_init(void) {
    for (size_t i = 0; i < VFS_MAX_OPEN; ++i) descriptors[i].used = 0;
    for (size_t i = 0; i < VFS_MAX_TMP; ++i) tmp_files[i].used = 0;
    return ext2_mount();
}
int vfs_open_flags(const char *path, int flags, uint32_t mode) {
    if (!path || !*path) return -1;
    int descriptor = allocate_descriptor();
    if (descriptor < 0) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    item->flags = flags;
    int directory_kind = virtual_directory_kind(path);
    if (directory_kind) {
        item->kind = (uint32_t)directory_kind;
        return descriptor;
    }
    if (string_equal(path, "/dev/null")) item->kind = VFS_KIND_NULL;
    else if (string_equal(path, "/dev/zero")) item->kind = VFS_KIND_ZERO;
    else if (string_equal(path, "/dev/tty")) item->kind = VFS_KIND_TTY;
    else if (string_equal(path, "/dev/fb0")) {
        if (!framebuffer_available() || !device_open_allows(0660, ORION_VIDEO_GID, flags)) {
            item->used = 0;
            return -1;
        }
        item->kind = VFS_KIND_FRAMEBUFFER;
    } else if (string_equal(path, "/dev/input/keyboard0")) {
        if (!device_open_allows(0444, 0, flags)) {
            item->used = 0;
            return -1;
        }
        item->kind = VFS_KIND_KEYBOARD;
    } else if (string_equal(path, "/dev/input/mouse0")) {
        if (!ps2mouse_available() || !device_open_allows(0444, 0, flags)) {
            item->used = 0;
            return -1;
        }
        item->kind = VFS_KIND_MOUSE;
    }
    else if (string_equal(path, "/proc/uptime")) {
        set_proc_data(item, "0.00 0.00\n");
    } else if (string_equal(path, "/proc/version")) {
        set_proc_data(item, "OrionOS version 0.04 x86_64\n");
    } else if (string_equal(path, "/proc/self/status")) {
        char status[128] = "Name:\tprocess\nState:\tR (running)\nPid:\t";
        uint64_t pid = task_current() ? task_current()->pid : 0;
        char number[24];
        size_t digits = 0;
        do {
            number[digits++] = (char)('0' + pid % 10);
            pid /= 10;
        } while (pid && digits < sizeof(number));
        size_t at = string_length(status);
        while (digits) status[at++] = number[--digits];
        status[at++] = '\n';
        status[at] = 0;
        set_proc_data(item, status);
    } else if (tmp_path_valid(path)) {
        int index = tmp_find(path);
        if (index < 0 && (flags & VFS_O_CREAT)) index = tmp_create(path);
        if (index < 0) {
            item->used = 0;
            return -1;
        }
        item->kind = VFS_KIND_TMP;
        item->tmp_index = index;
        if ((flags & VFS_O_TRUNC) && (flags & 3) != VFS_O_RDONLY) tmp_files[index].size = 0;
        if (flags & VFS_O_APPEND) item->offset = tmp_files[index].size;
        return descriptor;
    } else if (is_virtual_path(path)) {
        item->used = 0;
        return -1;
    } else {
        int created = 0;
        if (ext2_lookup(path, &item->file) != 0) {
            if (!(flags & VFS_O_CREAT) || !parent_allows(path, VFS_PERM_WRITE | VFS_PERM_EXEC) || ext2_create(path, mode, current_uid(), current_gid(), &item->file) != 0) {
                item->used = 0;
                return -1;
            }
            created = 1;
        } else if ((flags & VFS_O_CREAT) && (flags & VFS_O_EXCL)) {
            item->used = 0;
            return -1;
        }
        if (!created && !open_allows(&item->file, flags)) {
            item->used = 0;
            return -1;
        }
        if ((item->file.mode & EXT2_MODE_DIRECTORY) == EXT2_MODE_DIRECTORY) {
            if ((flags & 3) != VFS_O_RDONLY) {
                item->used = 0;
                return -1;
            }
            item->kind = VFS_KIND_DIRECTORY;
        } else {
            item->kind = VFS_KIND_EXT2;
            if ((flags & VFS_O_TRUNC) && (flags & 3) != VFS_O_RDONLY && ext2_truncate(&item->file) != 0) {
                item->used = 0;
                return -1;
            }
            if (flags & VFS_O_APPEND) item->offset = item->file.size;
        }
    }
    return descriptor;
}
int vfs_open(const char *path) {
    return vfs_open_flags(path, VFS_O_RDONLY, 0);
}
int64_t vfs_read(int descriptor, void *buffer, size_t size) {
    if (descriptor < 0 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used || (!buffer && size)) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    if ((item->flags & 3) == VFS_O_WRONLY) return -1;
    if (item->kind == VFS_KIND_EXT2) {
        int64_t result = ext2_read(&item->file, item->offset, buffer, size);
        if (result > 0) item->offset += (uint64_t)result;
        return result;
    }
    if (item->kind == VFS_KIND_TMP) {
        struct tmp_file *file = &tmp_files[item->tmp_index];
        size_t available = item->offset < file->size ? file->size - (size_t)item->offset : 0;
        if (size > available) size = available;
        for (size_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = file->data[item->offset + i];
        item->offset += size;
        return (int64_t)size;
    }
    if (item->kind == VFS_KIND_PROC) {
        size_t available = item->offset < item->data_size ? item->data_size - (size_t)item->offset : 0;
        if (size > available) size = available;
        for (size_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = (uint8_t)item->data[item->offset + i];
        item->offset += size;
        return (int64_t)size;
    }
    if (item->kind == VFS_KIND_ZERO) {
        for (size_t i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = 0;
        return (int64_t)size;
    }
    if (item->kind == VFS_KIND_NULL) return 0;
    if (item->kind == VFS_KIND_FRAMEBUFFER) {
        if (!framebuffer_access_allowed()) return -1;
        int64_t result = framebuffer_read(item->offset, buffer, size);
        if (result > 0) item->offset += (uint64_t)result;
        return result;
    }
    if (item->kind == VFS_KIND_KEYBOARD || item->kind == VFS_KIND_MOUSE) {
        if (size < sizeof(struct orion_input_event)) return -1;
        size_t count = 0;
        while (size - count >= sizeof(struct orion_input_event)) {
            struct orion_input_event event;
            int available = item->kind == VFS_KIND_KEYBOARD ? atkbd_read_event(&event) : ps2mouse_read_event(&event);
            if (!available) {
                if (count) break;
                __asm__ volatile("sti; hlt; cli" : : : "memory");
                continue;
            }
            ((struct orion_input_event *)((uint8_t *)buffer + count))[0] = event;
            count += sizeof(event);
        }
        return (int64_t)count;
    }
    return -1;
}
int64_t vfs_write(int descriptor, const void *buffer, size_t size) {
    if (descriptor < 0 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used || (!buffer && size)) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    if ((item->flags & 3) == VFS_O_RDONLY) return -1;
    if (item->kind == VFS_KIND_EXT2) {
        int64_t result = ext2_write(&item->file, item->offset, buffer, size);
        if (result > 0) item->offset += (uint64_t)result;
        return result;
    }
    if (item->kind == VFS_KIND_TMP) {
        struct tmp_file *file = &tmp_files[item->tmp_index];
        if (item->offset > VFS_TMP_SIZE || size > VFS_TMP_SIZE - item->offset) return -1;
        for (size_t i = 0; i < size; ++i) file->data[item->offset + i] = ((const uint8_t *)buffer)[i];
        item->offset += size;
        if (item->offset > file->size) file->size = (size_t)item->offset;
        return (int64_t)size;
    }
    if (item->kind == VFS_KIND_FRAMEBUFFER) {
        if (!framebuffer_access_allowed()) return -1;
        int64_t result = framebuffer_write(item->offset, buffer, size);
        if (result > 0) item->offset += (uint64_t)result;
        return result;
    }
    if (item->kind == VFS_KIND_NULL || item->kind == VFS_KIND_TTY || item->kind == VFS_KIND_ZERO) return (int64_t)size;
    return -1;
}
static int64_t add_dirent(uint8_t *buffer, size_t capacity, size_t used, uint64_t inode, int64_t offset, uint8_t type, const char *name) {
    size_t name_length = string_length(name);
    size_t header = offsetof(struct linux_dirent64, name);
    size_t record_length = (header + name_length + 1 + 7) & ~(size_t)7;
    if (record_length > capacity - used || record_length > UINT16_MAX) return -1;
    struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + used);
    entry->inode = inode;
    entry->offset = offset;
    entry->record_length = (uint16_t)record_length;
    entry->type = type;
    for (size_t i = 0; i <= name_length; ++i) entry->name[i] = name[i];
    return (int64_t)record_length;
}
static const char *const dev_entries[] = {"fb0", "input", "null", "zero", "tty"};
static const char *const input_entries[] = {"keyboard0", "mouse0"};
static const char *const proc_entries[] = {"self", "uptime", "version"};
int64_t vfs_getdents(int descriptor, void *buffer, size_t size) {
    if (descriptor < 0 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used || !buffer) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    if (item->kind != VFS_KIND_DIRECTORY && item->kind != VFS_KIND_TMP_DIRECTORY && item->kind != VFS_KIND_DEV_DIRECTORY && item->kind != VFS_KIND_PROC_DIRECTORY && item->kind != VFS_KIND_PROC_SELF_DIRECTORY && item->kind != VFS_KIND_INPUT_DIRECTORY) return -1;
    size_t used = 0;
    if (item->kind == VFS_KIND_DIRECTORY) {
        while (used < size) {
            uint64_t old_offset = item->offset;
            struct ext2_dirent source;
            int result = ext2_readdir(&item->file, &item->offset, &source);
            if (result <= 0) return result < 0 ? (used ? (int64_t)used : -1) : (int64_t)used;
            int64_t added = add_dirent((uint8_t *)buffer, size, used, source.inode, (int64_t)item->offset, source.type, source.name);
            if (added < 0) {
                item->offset = old_offset;
                return used ? (int64_t)used : -1;
            }
            used += (size_t)added;
        }
        return (int64_t)used;
    }
    while (used < size) {
        const char *name = NULL;
        uint64_t inode = 1;
        uint8_t type = 4;
        if (item->kind == VFS_KIND_DEV_DIRECTORY || item->kind == VFS_KIND_PROC_DIRECTORY || item->kind == VFS_KIND_PROC_SELF_DIRECTORY || item->kind == VFS_KIND_INPUT_DIRECTORY) {
            const char *const *entries = item->kind == VFS_KIND_DEV_DIRECTORY ? dev_entries : item->kind == VFS_KIND_INPUT_DIRECTORY ? input_entries : item->kind == VFS_KIND_PROC_SELF_DIRECTORY ? (const char *const[]){"status"} : proc_entries;
            size_t count = item->kind == VFS_KIND_DEV_DIRECTORY ? sizeof(dev_entries) / sizeof(dev_entries[0]) : item->kind == VFS_KIND_INPUT_DIRECTORY ? sizeof(input_entries) / sizeof(input_entries[0]) : item->kind == VFS_KIND_PROC_SELF_DIRECTORY ? 1 : sizeof(proc_entries) / sizeof(proc_entries[0]);
            if (item->offset >= count) break;
            name = entries[item->offset++];
            if (item->kind == VFS_KIND_DEV_DIRECTORY && string_equal(name, "fb0") && !framebuffer_available()) continue;
            if (item->kind == VFS_KIND_INPUT_DIRECTORY && string_equal(name, "mouse0") && !ps2mouse_available()) continue;
            type = item->kind == VFS_KIND_DEV_DIRECTORY && string_equal(name, "input") ? 4 :
                   item->kind == VFS_KIND_PROC_DIRECTORY && string_equal(name, "self") ? 4 :
                   item->kind == VFS_KIND_DEV_DIRECTORY || item->kind == VFS_KIND_INPUT_DIRECTORY ? 2 : 8;
        } else {
            size_t seen = 0;
            for (size_t i = 0; i < VFS_MAX_TMP; ++i) {
                if (!tmp_files[i].used) continue;
                if (seen++ == item->offset) {
                    name = tmp_files[i].path + 5;
                    inode = 0x10000 + i;
                    type = 8;
                    ++item->offset;
                    break;
                }
            }
            if (!name) break;
        }
        int64_t added = add_dirent((uint8_t *)buffer, size, used, inode, (int64_t)item->offset, type, name);
        if (added < 0) {
            --item->offset;
            return used ? (int64_t)used : -1;
        }
        used += (size_t)added;
    }
    return (int64_t)used;
}
int vfs_close(int descriptor) {
    if (descriptor < 3 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used) return -1;
    descriptors[descriptor].used = 0;
    return 0;
}
int vfs_dup(int descriptor) {
    if (descriptor < 3 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used) return -1;
    int copy = allocate_descriptor();
    if (copy < 0) return -1;
    struct vfs_descriptor *source = &descriptors[descriptor];
    struct vfs_descriptor *destination = &descriptors[copy];
    for (size_t i = 0; i < 15; ++i) destination->file.blocks[i] = source->file.blocks[i];
    destination->file.inode = source->file.inode;
    destination->file.mode = source->file.mode;
    destination->file.size = source->file.size;
    destination->offset = source->offset;
    destination->kind = source->kind;
    destination->flags = source->flags;
    destination->tmp_index = source->tmp_index;
    destination->data_size = source->data_size;
    for (size_t i = 0; i < sizeof(destination->data); ++i) destination->data[i] = source->data[i];
    return copy;
}
int vfs_size(int descriptor, uint64_t *size) {
    if (descriptor < 0 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used || !size) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    if (item->kind == VFS_KIND_FRAMEBUFFER && !framebuffer_access_allowed()) return -1;
    if (item->kind == VFS_KIND_EXT2 || item->kind == VFS_KIND_DIRECTORY) *size = item->file.size;
    else if (item->kind == VFS_KIND_FRAMEBUFFER) *size = framebuffer_size_bytes();
    else if (item->kind == VFS_KIND_TMP) *size = tmp_files[item->tmp_index].size;
    else *size = item->data_size;
    return 0;
}
int64_t vfs_seek(int descriptor, int64_t offset, int whence) {
    if (descriptor < 0 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    if (item->kind == VFS_KIND_FRAMEBUFFER && !framebuffer_access_allowed()) return -1;
    int64_t base = 0;
    if (whence == 1) base = (int64_t)item->offset;
    else if (whence == 2) {
        uint64_t size = 0;
        if (vfs_size(descriptor, &size) != 0 || size > INT64_MAX) return -1;
        base = (int64_t)size;
    } else if (whence != 0) return -1;
    if ((offset < 0 && base < -offset) || (offset > 0 && base > INT64_MAX - offset)) return -1;
    int64_t result = base + offset;
    if (result < 0) return -1;
    item->offset = (uint64_t)result;
    return result;
}
static int fill_stat(const struct ext2_file *file, struct vfs_stat *status) {
    if (!file || !status) return -1;
    status->inode = file->inode;
    status->size = file->size;
    status->mode = file->mode;
    status->uid = file->uid;
    status->gid = file->gid;
    return 0;
}
int vfs_stat_path(const char *path, struct vfs_stat *status) {
    if (!path || !status) return -1;
    status->uid = 0;
    status->gid = 0;
    int dir = virtual_directory_kind(path);
    if (dir) {
        status->inode = 1;
        status->size = 0;
        status->mode = VFS_S_IFDIR | 0755;
        return 0;
    }
    if (string_equal(path, "/dev/fb0")) {
        if (!framebuffer_available()) return -1;
        status->inode = 3;
        status->size = framebuffer_size_bytes();
        status->mode = VFS_S_IFCHR | 0660;
        status->gid = ORION_VIDEO_GID;
        return 0;
    }
    if (string_equal(path, "/dev/input/mouse0") && !ps2mouse_available()) return -1;
    if (string_equal(path, "/dev/input/keyboard0") || string_equal(path, "/dev/input/mouse0")) {
        status->inode = string_equal(path, "/dev/input/keyboard0") ? 4 : 5;
        status->size = 0;
        status->mode = VFS_S_IFCHR | 0444;
        return 0;
    }
    if (string_equal(path, "/dev/null") || string_equal(path, "/dev/zero") || string_equal(path, "/dev/tty") || string_equal(path, "/proc/uptime") || string_equal(path, "/proc/version") || string_equal(path, "/proc/self/status")) {
        status->inode = 2;
        status->size = 0;
        status->mode = VFS_S_IFREG | 0644;
        return 0;
    }
    int tmp = tmp_find(path);
    if (tmp >= 0) {
        status->inode = 0x10000 + (uint64_t)tmp;
        status->size = tmp_files[tmp].size;
        status->mode = VFS_S_IFREG | 0644;
        return 0;
    }
    struct ext2_file file;
    if (ext2_lookup(path, &file) != 0) return -1;
    return fill_stat(&file, status);
}
int vfs_stat_fd(int descriptor, struct vfs_stat *status) {
    if (descriptor < 0 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used || !status) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    status->uid = 0;
    status->gid = 0;
    if (item->kind == VFS_KIND_EXT2 || item->kind == VFS_KIND_DIRECTORY) return fill_stat(&item->file, status);
    if (item->kind == VFS_KIND_FRAMEBUFFER) {
        status->inode = 3;
        status->size = framebuffer_size_bytes();
        status->mode = VFS_S_IFCHR | 0660;
        status->gid = ORION_VIDEO_GID;
        return 0;
    }
    if (item->kind == VFS_KIND_KEYBOARD || item->kind == VFS_KIND_MOUSE) {
        status->inode = item->kind == VFS_KIND_KEYBOARD ? 4 : 5;
        status->size = 0;
        status->mode = VFS_S_IFCHR | 0444;
        return 0;
    }
    if (item->kind == VFS_KIND_DEV_DIRECTORY || item->kind == VFS_KIND_PROC_DIRECTORY || item->kind == VFS_KIND_PROC_SELF_DIRECTORY || item->kind == VFS_KIND_TMP_DIRECTORY || item->kind == VFS_KIND_INPUT_DIRECTORY) {
        status->inode = 1;
        status->size = 0;
        status->mode = VFS_S_IFDIR | 0755;
        return 0;
    }
    if (item->kind == VFS_KIND_TMP) {
        status->inode = 0x10000 + (uint64_t)item->tmp_index;
        status->size = tmp_files[item->tmp_index].size;
        status->mode = VFS_S_IFREG | 0644;
    } else {
        status->inode = 2;
        status->size = item->data_size;
        status->mode = VFS_S_IFREG | 0644;
    }
    return 0;
}
int vfs_mkdir(const char *path) {
    if (!path || is_virtual_path(path)) return -1;
    if (!parent_allows(path, VFS_PERM_WRITE | VFS_PERM_EXEC)) return -1;
    return ext2_mkdir(path, current_uid(), current_gid());
}
int vfs_unlink(const char *path) {
    if (!path) return -1;
    int tmp = tmp_find(path);
    if (tmp >= 0) {
        tmp_files[tmp].used = 0;
        return 0;
    }
    if (is_virtual_path(path)) return -1;
    if (!parent_allows(path, VFS_PERM_WRITE | VFS_PERM_EXEC)) return -1;
    return ext2_unlink(path);
}
int vfs_is_directory(const char *path) {
    struct vfs_stat status;
    return vfs_stat_path(path, &status) == 0 && (status.mode & VFS_S_IFMT) == VFS_S_IFDIR;
}
int vfs_is_tty(int descriptor) {
    return descriptor >= 0 && descriptor < VFS_MAX_OPEN && descriptors[descriptor].used && descriptors[descriptor].kind == VFS_KIND_TTY;
}

int64_t vfs_ioctl(int descriptor, unsigned long request, void *argument) {
    if (descriptor < 0 || descriptor >= VFS_MAX_OPEN || !descriptors[descriptor].used) return -1;
    struct vfs_descriptor *item = &descriptors[descriptor];
    if (item->kind != VFS_KIND_FRAMEBUFFER || !framebuffer_access_allowed()) return -1;
    return framebuffer_ioctl(request, argument);
}
