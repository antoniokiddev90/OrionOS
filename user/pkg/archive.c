#include "pkg.h"

#define TAR_BLOCK_SIZE 512
#define TAR_FILE_MAX 300000

static int read_exact(int descriptor, void *buffer, size_t length) {
    size_t used = 0;
    while (used < length) {
        ssize_t amount = read(descriptor, (char *)buffer + used, length - used);
        if (amount <= 0) return -1;
        used += (size_t)amount;
    }
    return 0;
}

static int parse_octal(const char *field, size_t length, uint64_t *value) {
    size_t index = 0;
    while (index < length && (field[index] == ' ' || field[index] == 0)) ++index;
    uint64_t result = 0;
    int digits = 0;
    while (index < length && field[index] >= '0' && field[index] <= '7') {
        if (result > (UINT64_MAX - (unsigned)(field[index] - '0')) / 8) return -1;
        result = result * 8 + (unsigned)(field[index++] - '0');
        digits = 1;
    }
    while (index < length) {
        if (field[index] != ' ' && field[index] != 0) return -1;
        ++index;
    }
    if (!digits) result = 0;
    *value = result;
    return 0;
}

static size_t field_length(const char *field, size_t capacity) {
    size_t length = 0;
    while (length < capacity && field[length]) ++length;
    return length;
}

static int tar_path(const uint8_t *header, char *path, size_t capacity) {
    size_t name_length = field_length((const char *)header, 100);
    size_t prefix_length = field_length((const char *)header + 345, 155);
    size_t needed = name_length + prefix_length + (prefix_length ? 1 : 0);
    if (!needed || needed >= capacity) return -1;
    size_t used = 0;
    if (prefix_length) {
        memcpy(path, header + 345, prefix_length);
        used = prefix_length;
        path[used++] = '/';
    }
    memcpy(path + used, header, name_length);
    used += name_length;
    path[used] = 0;
    return 0;
}

static int tar_header(int descriptor, uint8_t *header, uint64_t *size, uint32_t *mode, char *type) {
    if (read_exact(descriptor, header, TAR_BLOCK_SIZE) != 0) return -1;
    int zero = 1;
    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i) if (header[i]) { zero = 0; break; }
    if (zero) return 0;
    uint64_t stored_checksum;
    uint64_t parsed_size;
    uint64_t parsed_mode;
    if (parse_octal((const char *)header + 148, 8, &stored_checksum) != 0 ||
        parse_octal((const char *)header + 124, 12, &parsed_size) != 0 ||
        parse_octal((const char *)header + 100, 8, &parsed_mode) != 0) return -1;
    uint64_t checksum = 0;
    for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i) checksum += (i >= 148 && i < 156) ? (uint8_t)' ' : header[i];
    if (checksum != stored_checksum || parsed_size > TAR_FILE_MAX) return -1;
    *size = parsed_size;
    *mode = (uint32_t)parsed_mode & 0777;
    *type = header[156] ? (char)header[156] : '0';
    return 1;
}

static int skip_data(int descriptor, uint64_t size) {
    uint64_t padded = (size + TAR_BLOCK_SIZE - 1) & ~(uint64_t)(TAR_BLOCK_SIZE - 1);
    return lseek(descriptor, (off_t)padded, SEEK_CUR) < 0 ? -1 : 0;
}

static int normalize_package_path(const char *raw, const char *name, char *output, size_t capacity) {
    if (!raw || !*raw || raw[0] == '/') return -1;
    size_t raw_length = strlen(raw);
    if (raw_length >= capacity) return -1;
    char copy[PKG_PATH_MAX];
    memcpy(copy, raw, raw_length + 1);
    while (raw_length && copy[raw_length - 1] == '/') copy[--raw_length] = 0;
    if (!raw_length) return -1;
    char *slash = strchr(copy, '/');
    if (slash) {
        size_t first_length = (size_t)(slash - copy);
        if ((first_length == strlen(name) && !strncmp(copy, name, first_length)) ||
            (first_length == strlen(name) + 4 && !strncmp(copy, name, first_length - 4) && !strcmp(slash - 4, ".tar"))) {
            size_t suffix_length = strlen(slash + 1) + 1;
            for (size_t i = 0; i < suffix_length; ++i) copy[i] = slash[1 + i];
        }
    } else if (!strcmp(copy, name) || (strlen(copy) == strlen(name) + 4 && !strcmp(copy + strlen(name), ".tar"))) {
        output[0] = 0;
        return 0;
    }
    if (!copy[0]) {
        output[0] = 0;
        return 0;
    }
    size_t used = 0;
    size_t position = 0;
    while (copy[position]) {
        while (copy[position] == '/') ++position;
        if (!copy[position]) break;
        size_t start = position;
        while (copy[position] && copy[position] != '/') ++position;
        size_t part = position - start;
        if ((part == 1 && copy[start] == '.') || (part == 2 && copy[start] == '.' && copy[start + 1] == '.')) return -1;
        if (used && used + 1 >= capacity) return -1;
        if (used) output[used++] = '/';
        if (used + part >= capacity) return -1;
        memcpy(output + used, copy + start, part);
        used += part;
    }
    output[used] = 0;
    return used ? 0 : -1;
}

static int read_entry_text(int descriptor, uint64_t size, char *buffer, size_t capacity) {
    if (size >= capacity) return -1;
    size_t used = 0;
    while (used < size) {
        ssize_t amount = read(descriptor, buffer + used, (size_t)size - used);
        if (amount <= 0) return -1;
        used += (size_t)amount;
    }
    buffer[used] = 0;
    return 0;
}

int pkg_archive_inspect(const char *path, struct pkg_metadata *metadata, char *pre, size_t pre_capacity, char *post, size_t post_capacity) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    char config[2048];
    int have_config = 0;
    if (pre && pre_capacity) pre[0] = 0;
    if (post && post_capacity) post[0] = 0;
    for (;;) {
        uint8_t header[TAR_BLOCK_SIZE];
        uint64_t size;
        uint32_t mode;
        char type;
        int state = tar_header(descriptor, header, &size, &mode, &type);
        if (state <= 0) {
            close(descriptor);
            if (state < 0 || !have_config || pkg_parse_config(config, metadata) != 0) return -1;
            return 0;
        }
        char raw[PKG_PATH_MAX];
        if (tar_path(header, raw, sizeof(raw)) != 0) {
            if (skip_data(descriptor, size) != 0) { close(descriptor); return -1; }
            continue;
        }
        char *base = raw;
        for (char *p = raw; *p; ++p) if (*p == '/') base = p + 1;
        int result = 0;
        if (type == '0' && !strcmp(base, "pkg.cfg")) {
            result = read_entry_text(descriptor, size, config, sizeof(config));
            if (!result) have_config = 1;
        } else if (type == '0' && pre && pre_capacity && !strcmp(base, "pre.sh")) {
            result = read_entry_text(descriptor, size, pre, pre_capacity);
        } else if (type == '0' && post && post_capacity && !strcmp(base, "post.sh")) {
            result = read_entry_text(descriptor, size, post, post_capacity);
        } else {
            result = skip_data(descriptor, size);
            if (result == 0) continue;
        }
        if (result != 0) {
            close(descriptor);
            return -1;
        }
        uint64_t consumed = size;
        if (lseek(descriptor, (off_t)((TAR_BLOCK_SIZE - (consumed % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE), SEEK_CUR) < 0) {
            close(descriptor);
            return -1;
        }
    }
}

static int manifest_line(int descriptor, const char *line) {
    size_t length = strlen(line);
    if (pkg_write_all(descriptor, line, length) != 0 || pkg_write_all(descriptor, "\n", 1) != 0) return -1;
    return 0;
}

static int make_parent(const char *path) {
    char parent[PKG_PATH_MAX];
    size_t length = strlen(path);
    if (!length || length >= sizeof(parent)) return -1;
    memcpy(parent, path, length + 1);
    char *slash = NULL;
    for (char *p = parent; *p; ++p) if (*p == '/') slash = p;
    if (!slash || slash == parent) return 0;
    *slash = 0;
    return pkg_mkdirs(parent);
}

static int extract_file(int archive, const char *destination, uint64_t size, uint32_t mode) {
    int output = open(destination, O_CREAT | O_WRONLY | O_TRUNC, mode ? mode : 0644);
    if (output < 0) return -1;
    uint8_t buffer[512];
    uint64_t remaining = size;
    int result = 0;
    while (remaining) {
        size_t part = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        if (read_exact(archive, buffer, part) != 0 || pkg_write_all(output, buffer, part) != 0) {
            result = -1;
            break;
        }
        remaining -= part;
    }
    if (close(output) != 0) result = -1;
    uint64_t padding = (TAR_BLOCK_SIZE - (size % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE;
    if (lseek(archive, (off_t)padding, SEEK_CUR) < 0) result = -1;
    return result;
}

int pkg_archive_install(const char *archive, const struct pkg_metadata *metadata, const char *database_path) {
    int input = open(archive, O_RDONLY);
    if (input < 0) return -1;
    int database = open(database_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (database < 0) {
        close(input);
        return -1;
    }
    char record[256];
    size_t used = 0;
    const char *fields[3] = {metadata->name, metadata->version, metadata->license};
    for (size_t field = 0; field < 3; ++field) {
        size_t length = strlen(fields[field]);
        if (used + length + 2 >= sizeof(record)) {
            close(database);
            close(input);
            unlink(database_path);
            return -1;
        }
        memcpy(record + used, fields[field], length);
        used += length;
        record[used++] = field == 2 ? '\n' : ' ';
    }
    if (pkg_write_all(database, record, used) != 0) {
        close(database);
        close(input);
        unlink(database_path);
        return -1;
    }
    int result = 0;
    for (;;) {
        uint8_t header[TAR_BLOCK_SIZE];
        uint64_t size;
        uint32_t mode;
        char type;
        int state = tar_header(input, header, &size, &mode, &type);
        if (state <= 0) {
            if (state < 0) result = -1;
            break;
        }
        char raw[PKG_PATH_MAX];
        char relative[PKG_PATH_MAX];
        if (tar_path(header, raw, sizeof(raw)) != 0 || normalize_package_path(raw, metadata->name, relative, sizeof(relative)) != 0) {
            result = -1;
            break;
        }
        if (!relative[0]) {
            if (skip_data(input, size) != 0) result = -1;
            if (result) break;
            continue;
        }
        if (type == '5') {
            char directory[PKG_PATH_MAX];
            if (pkg_join(directory, sizeof(directory), "/", relative) != 0 || pkg_mkdirs(directory) != 0) result = -1;
            if (skip_data(input, size) != 0) result = -1;
            if (result) break;
            continue;
        }
        if (type != '0') {
            result = -1;
            break;
        }
        if (!strcmp(relative, "pkg.cfg") || !strcmp(relative, "pre.sh") || !strcmp(relative, "post.sh") || !strcmp(relative, "make-pkg.txt")) {
            if (skip_data(input, size) != 0) result = -1;
            if (result) break;
            continue;
        }
        char destination[PKG_PATH_MAX];
        if (pkg_join(destination, sizeof(destination), "/", relative) != 0 || make_parent(destination) != 0 ||
            manifest_line(database, destination) != 0 || extract_file(input, destination, size, mode) != 0) {
            result = -1;
            break;
        }
    }
    if (close(database) != 0) result = -1;
    close(input);
    if (result != 0) {
        pkg_remove_record(metadata->name, 1);
        return -1;
    }
    return 0;
}
