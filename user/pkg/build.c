#include "pkg.h"

#define TAR_BLOCK_SIZE 512

struct archive_writer {
    int descriptor;
    uint64_t total;
    const char *output_path;
};

static int write_octal(uint8_t *field, size_t length, uint64_t value) {
    if (length < 2) return -1;
    for (size_t i = 0; i < length; ++i) field[i] = '0';
    field[length - 1] = 0;
    size_t position = length - 1;
    do {
        if (!position) return -1;
        field[--position] = (uint8_t)('0' + value % 8);
        value /= 8;
    } while (value);
    return 0;
}

static int split_tar_path(const char *path, char *header) {
    size_t length = strlen(path);
    if (!length) return -1;
    if (length <= 100) {
        memcpy(header, path, length);
        return 0;
    }
    const char *split = NULL;
    for (const char *p = path; *p; ++p) {
        if (*p == '/') {
            size_t prefix = (size_t)(p - path);
            size_t name = length - prefix - 1;
            if (prefix <= 155 && name <= 100) split = p;
        }
    }
    if (!split) return -1;
    size_t prefix_length = (size_t)(split - path);
    size_t name_length = length - prefix_length - 1;
    memcpy(header, split + 1, name_length);
    memcpy(header + 345, path, prefix_length);
    return 0;
}

static int write_header(struct archive_writer *writer, const char *path, uint32_t mode, uint64_t size, char type) {
    uint8_t header[TAR_BLOCK_SIZE];
    memset(header, 0, sizeof(header));
    if (split_tar_path(path, (char *)header) != 0 ||
        write_octal(header + 100, 8, mode & 0777) != 0 ||
        write_octal(header + 108, 8, 0) != 0 ||
        write_octal(header + 116, 8, 0) != 0 ||
        write_octal(header + 124, 12, size) != 0 ||
        write_octal(header + 136, 12, 0) != 0) return -1;
    memset(header + 148, ' ', 8);
    header[156] = (uint8_t)type;
    memcpy(header + 257, "ustar", 5);
    header[262] = 0;
    header[263] = '0';
    header[264] = '0';
    uint64_t checksum = 0;
    for (size_t i = 0; i < sizeof(header); ++i) checksum += header[i];
    if (write_octal(header + 148, 7, checksum) != 0) return -1;
    header[154] = 0;
    header[155] = ' ';
    if (pkg_write_all(writer->descriptor, header, sizeof(header)) != 0) return -1;
    writer->total += sizeof(header);
    return writer->total <= PKG_ARCHIVE_LIMIT ? 0 : -1;
}

static int append_file_data(struct archive_writer *writer, const char *path, uint64_t size) {
    int input = open(path, O_RDONLY);
    if (input < 0) return -1;
    uint8_t buffer[512];
    uint64_t remaining = size;
    int result = 0;
    while (remaining) {
        size_t part = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        ssize_t amount = read(input, buffer, part);
        if (amount <= 0 || (size_t)amount > part || pkg_write_all(writer->descriptor, buffer, (size_t)amount) != 0) {
            result = -1;
            break;
        }
        remaining -= (size_t)amount;
        writer->total += (size_t)amount;
    }
    close(input);
    if (result != 0) return -1;
    size_t padding = (size_t)((TAR_BLOCK_SIZE - (size % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE);
    if (padding) {
        uint8_t zero[TAR_BLOCK_SIZE];
        memset(zero, 0, sizeof(zero));
        if (pkg_write_all(writer->descriptor, zero, padding) != 0) return -1;
        writer->total += padding;
    }
    return writer->total <= PKG_ARCHIVE_LIMIT ? 0 : -1;
}

static int build_directory(struct archive_writer *writer, const char *absolute, const char *relative, unsigned depth) {
    if (depth > 24) return -1;
    int directory = open(absolute, O_RDONLY);
    if (directory < 0) return -1;
    uint8_t buffer[4096];
    int result = 0;
    for (;;) {
        int amount = getdents64(directory, buffer, sizeof(buffer));
        if (amount < 0) {
            result = -1;
            break;
        }
        if (!amount) break;
        size_t offset = 0;
        while (offset < (size_t)amount) {
            struct linux_dirent64 *entry = (struct linux_dirent64 *)(buffer + offset);
            if (entry->d_reclen < 19 || offset + entry->d_reclen > (size_t)amount) {
                result = -1;
                break;
            }
            if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) {
                char child_absolute[PKG_PATH_MAX];
                char child_relative[PKG_PATH_MAX];
                if (pkg_join(child_absolute, sizeof(child_absolute), absolute, entry->d_name) != 0 ||
                    (relative[0] ? pkg_join(child_relative, sizeof(child_relative), relative, entry->d_name) :
                                   (strlen(entry->d_name) >= sizeof(child_relative) ? -1 : (strcpy(child_relative, entry->d_name), 0))) != 0) {
                    result = -1;
                    break;
                }
                if (!strcmp(child_absolute, writer->output_path) || !strcmp(child_relative, "make-pkg.txt")) {
                    offset += entry->d_reclen;
                    continue;
                }
                struct stat status;
                if (stat(child_absolute, &status) != 0) {
                    result = -1;
                    break;
                }
                if (S_ISDIR(status.st_mode)) {
                    if (write_header(writer, child_relative, status.st_mode, 0, '5') != 0 ||
                        build_directory(writer, child_absolute, child_relative, depth + 1) != 0) {
                        result = -1;
                        break;
                    }
                } else if (S_ISREG(status.st_mode)) {
                    if (write_header(writer, child_relative, status.st_mode, status.st_size, '0') != 0 ||
                        append_file_data(writer, child_absolute, status.st_size) != 0) {
                        result = -1;
                        break;
                    }
                } else {
                    result = -1;
                    break;
                }
            }
            offset += entry->d_reclen;
        }
        if (result) break;
    }
    close(directory);
    return result;
}

int pkg_archive_build(const char *source, const char *output) {
    int descriptor = open(output, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (descriptor < 0) return -1;
    struct archive_writer writer;
    writer.descriptor = descriptor;
    writer.total = 0;
    writer.output_path = output;
    int result = build_directory(&writer, source, "", 0);
    if (!result) {
        uint8_t end[TAR_BLOCK_SIZE * 2];
        memset(end, 0, sizeof(end));
        if (writer.total + sizeof(end) > PKG_ARCHIVE_LIMIT || pkg_write_all(descriptor, end, sizeof(end)) != 0) result = -1;
        else writer.total += sizeof(end);
    }
    if (close(descriptor) != 0) result = -1;
    if (result) {
        unlink(output);
        return -1;
    }
    return 0;
}

int pkg_build(const char *directory) {
    if (!directory || !*directory) {
        puts("pkg: build requires a package directory");
        return 2;
    }
    char original[PKG_PATH_MAX];
    char source[PKG_PATH_MAX];
    char output[PKG_PATH_MAX];
    if (!getcwd(original, sizeof(original)) || chdir(directory) != 0 || !getcwd(source, sizeof(source))) {
        puts("pkg: cannot open package build directory");
        return 1;
    }
    char config[2048];
    size_t config_length = 0;
    struct pkg_metadata metadata;
    if (pkg_read_file("pkg.cfg", config, sizeof(config) - 1, &config_length) != 0) {
        chdir(original);
        puts("pkg: pkg.cfg is required");
        return 1;
    }
    config[config_length] = 0;
    if (pkg_parse_config(config, &metadata) != 0) {
        chdir(original);
        puts("pkg: invalid pkg.cfg");
        return 1;
    }
    struct stat recipe_status;
    if (stat("make-pkg.txt", &recipe_status) != 0 || pkg_run_recipe("make-pkg.txt") != 0) {
        chdir(original);
        puts("pkg: package recipe failed");
        return 1;
    }
    char filename[PKG_NAME_MAX + 8];
    strcpy(filename, metadata.name);
    strcat(filename, ".tar");
    if (pkg_join(output, sizeof(output), original, filename) != 0) {
        chdir(original);
        puts("pkg: output path is too long");
        return 1;
    }
    chdir(original);
    if (pkg_archive_build(source, output) != 0) {
        puts("pkg: cannot create package archive; check path limits and available space");
        return 1;
    }
    printf("pkg: created %s\n", output);
    return 0;
}
