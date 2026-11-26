#include "pkg.h"

int pkg_write_all(int descriptor, const void *buffer, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t written = write(descriptor, (const char *)buffer + sent, length - sent);
        if (written <= 0) return -1;
        sent += (size_t)written;
    }
    return 0;
}

int pkg_read_file(const char *path, char *buffer, size_t capacity, size_t *length) {
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    size_t used = 0;
    for (;;) {
        if (used == capacity) {
            char extra;
            ssize_t result = read(descriptor, &extra, 1);
            close(descriptor);
            if (result != 0) return -1;
            break;
        }
        ssize_t result = read(descriptor, buffer + used, capacity - used);
        if (result < 0) {
            close(descriptor);
            return -1;
        }
        if (!result) break;
        used += (size_t)result;
    }
    close(descriptor);
    if (length) *length = used;
    if (used < capacity) buffer[used] = 0;
    return 0;
}

int pkg_write_file(const char *path, const void *buffer, size_t length, unsigned mode) {
    int descriptor = open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (descriptor < 0) return -1;
    int result = pkg_write_all(descriptor, buffer, length);
    if (close(descriptor) != 0) result = -1;
    return result;
}

int pkg_read_line(int descriptor, char *buffer, size_t capacity) {
    if (!capacity) return -1;
    size_t used = 0;
    int overflow = 0;
    for (;;) {
        char value;
        ssize_t result = read(descriptor, &value, 1);
        if (result < 0) return -1;
        if (!result) {
            if (!used && !overflow) return 0;
            break;
        }
        if (value == '\n') break;
        if (value != '\r') {
            if (used + 1 < capacity) buffer[used++] = value;
            else overflow = 1;
        }
    }
    buffer[used] = 0;
    return overflow ? -1 : 1;
}

int pkg_join(char *output, size_t capacity, const char *left, const char *right) {
    size_t a = strlen(left);
    size_t b = strlen(right);
    int slash = a && left[a - 1] != '/' && b && right[0] != '/';
    int duplicate = a && left[a - 1] == '/' && b && right[0] == '/';
    size_t start = duplicate ? 1 : 0;
    if (a + (size_t)slash + b - start + 1 > capacity) return -1;
    memcpy(output, left, a);
    size_t used = a;
    if (slash) output[used++] = '/';
    memcpy(output + used, right + start, b - start);
    output[used + b - start] = 0;
    return 0;
}

int pkg_mkdirs(const char *path) {
    char current[PKG_PATH_MAX];
    size_t length = strlen(path);
    if (!length || length >= sizeof(current)) return -1;
    memcpy(current, path, length + 1);
    for (size_t i = 1; i <= length; ++i) {
        if (current[i] != '/' && current[i] != 0) continue;
        char saved = current[i];
        current[i] = 0;
        if (current[0]) {
            struct stat status;
            if (stat(current, &status) != 0) {
                if (mkdir(current, 0755) != 0 && stat(current, &status) != 0) return -1;
            } else if (!S_ISDIR(status.st_mode)) {
                return -1;
            }
        }
        current[i] = saved;
    }
    return 0;
}

int pkg_prepare_layout(void) {
    if (pkg_mkdirs("/etc/pkg/db") != 0) return -1;
    if (pkg_mkdirs(PKG_CACHE_DIR) != 0) return -1;
    return 0;
}

int pkg_valid_name(const char *name) {
    if (!name || !*name || strlen(name) >= PKG_NAME_MAX || !strcmp(name, ".") || !strcmp(name, "..")) return 0;
    for (const char *p = name; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '.' || *p == '_' || *p == '+' || *p == '-')) return 0;
    }
    return 1;
}

static char *trim_text(char *text) {
    while (*text == ' ' || *text == '\t') ++text;
    size_t length = strlen(text);
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r')) text[--length] = 0;
    return text;
}

static int copy_value(char *destination, size_t capacity, char *value) {
    value = trim_text(value);
    size_t length = strlen(value);
    if (length >= 2 && value[0] == '"' && value[length - 1] == '"') {
        value[length - 1] = 0;
        ++value;
        --length;
    }
    if (length >= capacity) return -1;
    memcpy(destination, value, length + 1);
    return 0;
}

int pkg_parse_config(const char *text, struct pkg_metadata *metadata) {
    if (!text || !metadata) return -1;
    memset(metadata, 0, sizeof(*metadata));
    strcpy(metadata->license, "unknown");
    int section = 0;
    int found_name = 0;
    int found_version = 0;
    char line[256];
    size_t used = 0;
    for (size_t i = 0;; ++i) {
        char value = text[i];
        if (value != '\n' && value != 0) {
            if (used + 1 >= sizeof(line)) return -1;
            line[used++] = value;
            continue;
        }
        line[used] = 0;
        char *content = trim_text(line);
        if (!strcmp(content, "[pkg]")) section = 1;
        else if (*content == '[') section = 0;
        else if (section && *content && *content != '#') {
            char *equal = strchr(content, '=');
            if (!equal) return -1;
            *equal++ = 0;
            char *key = trim_text(content);
            if (!strcmp(key, "name")) {
                if (copy_value(metadata->name, sizeof(metadata->name), equal) != 0) return -1;
                found_name = 1;
            } else if (!strcmp(key, "version")) {
                if (copy_value(metadata->version, sizeof(metadata->version), equal) != 0) return -1;
                found_version = 1;
            } else if (!strcmp(key, "license")) {
                if (copy_value(metadata->license, sizeof(metadata->license), equal) != 0) return -1;
            }
        }
        used = 0;
        if (!value) break;
    }
    if (!found_name || !found_version || !pkg_valid_name(metadata->name) || !metadata->version[0]) return -1;
    return 0;
}

static int command_status(pid_t child) {
    int status = 0;
    if (wait4(child, &status, 0, NULL) < 0) return -1;
    return status == 0 ? 0 : -1;
}

int pkg_run_line(const char *source) {
    if (!source) return -1;
    char line[256];
    size_t length = strlen(source);
    if (length >= sizeof(line)) return -1;
    memcpy(line, source, length + 1);
    char *cursor = line;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (!*cursor || *cursor == '#') return 0;
    char *arguments[16];
    int count = 0;
    char *read_position = cursor;
    char *write_position = cursor;
    while (*read_position) {
        while (*read_position == ' ' || *read_position == '\t') ++read_position;
        if (!*read_position) break;
        if (count >= 15) return -1;
        arguments[count++] = write_position;
        char quote = 0;
        while (*read_position) {
            char value = *read_position++;
            if (!quote && (value == ' ' || value == '\t')) break;
            if (!quote && (value == '\'' || value == '"')) {
                quote = value;
                continue;
            }
            if (quote && value == quote) {
                quote = 0;
                continue;
            }
            if (value == '\\' && *read_position) value = *read_position++;
            *write_position++ = value;
        }
        if (quote) return -1;
        *write_position++ = 0;
    }
    arguments[count] = NULL;
    if (!count) return 0;
    if (!strcmp(arguments[0], "cd")) {
        const char *directory = count > 1 ? arguments[1] : "/";
        return chdir(directory);
    }
    if (!strcmp(arguments[0], "exit")) return 0;
    char executable[PKG_PATH_MAX];
    if (strchr(arguments[0], '/')) {
        if (strlen(arguments[0]) >= sizeof(executable)) return -1;
        strcpy(executable, arguments[0]);
    } else if (pkg_join(executable, sizeof(executable), "/bin", arguments[0]) != 0) {
        return -1;
    }
    pid_t child = fork();
    if (child < 0) return -1;
    if (!child) {
        execve(executable, arguments, NULL);
        printf("pkg: cannot execute %s\n", executable);
        exit(127);
    }
    return command_status(child);
}

int pkg_run_script(const char *script) {
    if (!script) return 0;
    char line[256];
    size_t used = 0;
    for (size_t i = 0;; ++i) {
        char value = script[i];
        if (value != '\n' && value != 0) {
            if (used + 1 >= sizeof(line)) return -1;
            line[used++] = value;
            continue;
        }
        if (value == 0 && !used) break;
        line[used] = 0;
        if (pkg_run_line(line) != 0) return -1;
        used = 0;
        if (!value) break;
    }
    return 0;
}

static int recipe_directive(const char *line) {
    const char *token_end = line;
    while (*token_end && *token_end != ' ' && *token_end != '\t') ++token_end;
    for (const char *p = line; p < token_end; ++p) if (*p == ':') return 1;
    const char *variable_end = line;
    while ((*variable_end >= 'A' && *variable_end <= 'Z') || (*variable_end >= 'a' && *variable_end <= 'z') || (*variable_end >= '0' && *variable_end <= '9') || *variable_end == '_') ++variable_end;
    if (variable_end == line) return 0;
    while (*variable_end == ' ' || *variable_end == '\t') ++variable_end;
    if (*variable_end == '=') return 1;
    return 0;
}

int pkg_run_recipe(const char *path) {
    char text[8192];
    size_t length = 0;
    if (pkg_read_file(path, text, sizeof(text) - 1, &length) != 0) return -1;
    text[length] = 0;
    char line[256];
    size_t used = 0;
    for (size_t i = 0;; ++i) {
        char value = text[i];
        if (value != '\n' && value != 0) {
            if (used + 1 >= sizeof(line)) return -1;
            line[used++] = value;
            continue;
        }
        line[used] = 0;
        char *command = line;
        while (*command == ' ' || *command == '\t') ++command;
        if (*command && *command != '#' && !recipe_directive(command) && pkg_run_line(command) != 0) return -1;
        used = 0;
        if (!value) break;
    }
    return 0;
}
