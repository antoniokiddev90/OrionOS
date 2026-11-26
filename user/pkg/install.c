#include "pkg.h"

static int is_root(void) {
    return geteuid() == 0;
}

static int valid_version(const char *version) {
    if (!version || !*version || strlen(version) >= PKG_VERSION_MAX) return 0;
    for (const char *p = version; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '+' || *p == '-')) return 0;
    }
    return 1;
}

static int record_path(const char *name, char *path, size_t capacity) {
    if (!pkg_valid_name(name)) return -1;
    if (pkg_join(path, capacity, PKG_DB_DIR, name) != 0) return -1;
    return 0;
}

int pkg_read_record(const char *name, char *version, size_t version_capacity) {
    char path[PKG_PATH_MAX];
    if (record_path(name, path, sizeof(path)) != 0) return -1;
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    char line[256];
    int state = pkg_read_line(descriptor, line, sizeof(line));
    close(descriptor);
    if (state != 1) return -1;
    char *cursor = line;
    char *first = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (!*cursor) return -1;
    *cursor++ = 0;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    char *start = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    *cursor = 0;
    if (!pkg_valid_name(first) || !valid_version(start) || strlen(start) >= version_capacity) return -1;
    if (strcmp(first, name) != 0) return -1;
    strcpy(version, start);
    return 0;
}

static int safe_remove_path(const char *path) {
    if (path[0] != '/' || !path[1]) return 0;
    const char *cursor = path;
    while (*cursor) {
        while (*cursor == '/') ++cursor;
        const char *start = cursor;
        while (*cursor && *cursor != '/') ++cursor;
        size_t length = (size_t)(cursor - start);
        if ((length == 2 && start[0] == '.' && start[1] == '.') || (length == 1 && start[0] == '.')) return 0;
    }
    return 1;
}

int pkg_remove_record(const char *name, int quiet) {
    char path[PKG_PATH_MAX];
    if (record_path(name, path, sizeof(path)) != 0) return -1;
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    char line[PKG_PATH_MAX];
    if (pkg_read_line(descriptor, line, sizeof(line)) != 1) {
        close(descriptor);
        return -1;
    }
    int removed = 0;
    for (;;) {
        int state = pkg_read_line(descriptor, line, sizeof(line));
        if (state == 0) break;
        if (state < 0) {
            close(descriptor);
            return -1;
        }
        if (safe_remove_path(line)) {
            if (unlink(line) == 0) ++removed;
        }
    }
    close(descriptor);
    unlink(path);
    if (!quiet) printf("pkg: removed %s (%u files)\n", name, (unsigned)removed);
    return 0;
}

static int install_package(const char *name, int force) {
    if (!pkg_valid_name(name)) {
        puts("pkg: invalid package name");
        return 1;
    }
    if (pkg_prepare_layout() != 0) {
        puts("pkg: cannot prepare package database");
        return 1;
    }
    struct pkg_entry entry;
    if (pkg_find_best(name, &entry) != 0) {
        printf("pkg: package not found: %s\n", name);
        return 1;
    }
    char current[PKG_VERSION_MAX];
    int installed = pkg_read_record(name, current, sizeof(current)) == 0;
    if (installed && !force) {
        int comparison = pkg_version_compare(entry.version, current);
        if (comparison == 0) {
            printf("pkg: %s %s is already installed\n", name, current);
            return 0;
        }
        if (comparison < 0) {
            printf("pkg: installed version %s is newer than repository version %s\n", current, entry.version);
            return 0;
        }
    }
    if (!valid_version(entry.version)) {
        puts("pkg: repository contains an invalid version");
        return 1;
    }
    char archive_name[PKG_NAME_MAX + PKG_VERSION_MAX + 8];
    strcpy(archive_name, name);
    strcat(archive_name, "-");
    strcat(archive_name, entry.version);
    strcat(archive_name, ".tar");
    char archive_path[PKG_PATH_MAX];
    if (pkg_join(archive_path, sizeof(archive_path), PKG_CACHE_DIR, archive_name) != 0) return 1;
    printf("Downloading %s %s from %s\n", name, entry.version, entry.repo);
    if (pkg_http_get(entry.archive, archive_path) != 0) {
        puts("pkg: package download failed");
        return 1;
    }
    struct pkg_metadata metadata;
    char pre[4096];
    char post[4096];
    if (pkg_archive_inspect(archive_path, &metadata, pre, sizeof(pre), post, sizeof(post)) != 0 ||
        strcmp(metadata.name, name) != 0 || strcmp(metadata.version, entry.version) != 0) {
        puts("pkg: package archive metadata is invalid or does not match the index");
        unlink(archive_path);
        return 1;
    }
    if (pkg_run_script(pre) != 0) {
        puts("pkg: pre-install script failed");
        return 1;
    }
    if (installed && pkg_remove_record(name, 1) != 0) {
        puts("pkg: cannot remove the previous package record");
        return 1;
    }
    char database_path[PKG_PATH_MAX];
    if (record_path(name, database_path, sizeof(database_path)) != 0 ||
        pkg_archive_install(archive_path, &metadata, database_path) != 0) {
        puts("pkg: package extraction failed");
        return 1;
    }
    if (pkg_run_script(post) != 0) {
        puts("pkg: post-install script failed");
        pkg_remove_record(name, 1);
        return 1;
    }
    printf("pkg: installed %s %s\n", name, metadata.version);
    return 0;
}

int pkg_install(const char *name) {
    if (!is_root()) {
        puts("pkg: install requires root privileges");
        return 1;
    }
    return install_package(name, 0);
}

int pkg_remove(const char *name) {
    if (!is_root()) {
        puts("pkg: remove requires root privileges");
        return 1;
    }
    if (!pkg_valid_name(name)) {
        puts("pkg: invalid package name");
        return 1;
    }
    if (pkg_remove_record(name, 0) != 0) {
        printf("pkg: package is not installed: %s\n", name);
        return 1;
    }
    return 0;
}

int pkg_upgrade(void) {
    if (!is_root()) {
        puts("pkg: upgrade requires root privileges");
        return 1;
    }
    if (pkg_update() != 0) return 1;
    int directory = open(PKG_DB_DIR, O_RDONLY);
    if (directory < 0) {
        puts("pkg: cannot open package database");
        return 1;
    }
    uint8_t buffer[2048];
    int failures = 0;
    for (;;) {
        int amount = getdents64(directory, buffer, sizeof(buffer));
        if (amount < 0) {
            failures = 1;
            break;
        }
        if (!amount) break;
        size_t offset = 0;
        while (offset < (size_t)amount) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + offset);
            if (entry->d_reclen < 19 || offset + entry->d_reclen > (size_t)amount) {
                failures = 1;
                break;
            }
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") && pkg_valid_name(entry->d_name)) {
                char current[PKG_VERSION_MAX];
                struct pkg_entry latest;
                if (pkg_read_record(entry->d_name, current, sizeof(current)) == 0 &&
                    pkg_find_best(entry->d_name, &latest) == 0 && pkg_version_compare(latest.version, current) > 0) {
                    printf("Upgrading %s from %s to %s\n", entry->d_name, current, latest.version);
                    if (install_package(entry->d_name, 1) != 0) failures = 1;
                }
            }
            offset += entry->d_reclen;
        }
        if (failures) break;
    }
    close(directory);
    if (failures) return 1;
    puts("pkg: upgrade complete");
    return 0;
}
