#include "pkg.h"

static int valid_repo_url(const char *url) {
    if (!url || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) || strlen(url) >= PKG_URL_MAX) return 0;
    for (const char *p = url; *p; ++p) if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') return 0;
    return 1;
}

static int read_repositories(char repositories[PKG_REPO_MAX][PKG_URL_MAX], size_t *count) {
    char content[4096];
    size_t length = 0;
    *count = 0;
    if (pkg_read_file(PKG_REPO_LIST, content, sizeof(content) - 1, &length) != 0) return -1;
    content[length] = 0;
    char *line = content;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') ++end;
        char saved = *end;
        *end = 0;
        while (*line == ' ' || *line == '\t') ++line;
        size_t line_length = strlen(line);
        while (line_length && (line[line_length - 1] == ' ' || line[line_length - 1] == '\t' || line[line_length - 1] == '\r')) line[--line_length] = 0;
        if (*line && *line != '#') {
            if (!valid_repo_url(line) || *count >= PKG_REPO_MAX) return -1;
            strcpy(repositories[(*count)++], line);
        }
        if (!saved) break;
        line = end + 1;
    }
    return 0;
}

int pkg_repo_add(const char *url) {
    if (!valid_repo_url(url)) return -1;
    if (pkg_mkdirs("/etc/pkg") != 0) return -1;
    char repositories[PKG_REPO_MAX][PKG_URL_MAX];
    size_t count = 0;
    struct stat status;
    if (stat(PKG_REPO_LIST, &status) == 0) {
        if (read_repositories(repositories, &count) != 0) return -1;
    }
    for (size_t i = 0; i < count; ++i) if (!strcmp(repositories[i], url)) return 0;
    if (count >= PKG_REPO_MAX) return -1;
    int descriptor = open(PKG_REPO_LIST, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (descriptor < 0) return -1;
    int result = pkg_write_all(descriptor, url, strlen(url));
    if (!result) result = pkg_write_all(descriptor, "\n", 1);
    if (close(descriptor) != 0) result = -1;
    return result;
}

int pkg_repo_remove(const char *url) {
    char repositories[PKG_REPO_MAX][PKG_URL_MAX];
    size_t count = 0;
    if (read_repositories(repositories, &count) != 0) return -1;
    char output[4096];
    size_t used = 0;
    int found = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!strcmp(repositories[i], url)) {
            found = 1;
            continue;
        }
        size_t length = strlen(repositories[i]);
        if (used + length + 1 >= sizeof(output)) return -1;
        memcpy(output + used, repositories[i], length);
        used += length;
        output[used++] = '\n';
    }
    if (!found) return -1;
    return pkg_write_file(PKG_REPO_LIST, output, used, 0644);
}

int pkg_repo_list(void) {
    char repositories[PKG_REPO_MAX][PKG_URL_MAX];
    size_t count = 0;
    if (read_repositories(repositories, &count) != 0) {
        puts("pkg: cannot read repository list");
        return 1;
    }
    if (!count) puts("pkg: no repositories configured");
    for (size_t i = 0; i < count; ++i) puts(repositories[i]);
    return 0;
}

static int make_index_path(size_t index, char *path, size_t capacity) {
    char digits[8];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + index % 10);
        index /= 10;
    } while (index && count < sizeof(digits));
    char filename[16] = "index-";
    size_t used = strlen(filename);
    while (count) filename[used++] = digits[--count];
    filename[used] = 0;
    return pkg_join(path, capacity, PKG_CACHE_DIR, filename);
}

static int make_repo_index_url(const char *repository, const char *filename, char *url, size_t capacity) {
    size_t length = strlen(repository);
    if (length && repository[length - 1] == '/') {
        if (length + strlen(filename) + 1 > capacity) return -1;
        memcpy(url, repository, length);
        strcpy(url + length, filename);
        return 0;
    }
    return pkg_join(url, capacity, repository, filename);
}

int pkg_update(void) {
    char repositories[PKG_REPO_MAX][PKG_URL_MAX];
    size_t count = 0;
    if (read_repositories(repositories, &count) != 0) {
        puts("pkg: cannot read repository list");
        return 1;
    }
    if (!count) {
        puts("pkg: no repositories configured");
        return 1;
    }
    if (pkg_prepare_layout() != 0) {
        puts("pkg: cannot prepare package cache");
        return 1;
    }
    int failures = 0;
    for (size_t i = 0; i < count; ++i) {
        char destination[PKG_PATH_MAX];
        char url[PKG_URL_MAX];
        if (make_index_path(i, destination, sizeof(destination)) != 0 ||
            make_repo_index_url(repositories[i], "index", url, sizeof(url)) != 0) {
            ++failures;
            continue;
        }
        printf("Updating repository %s\n", repositories[i]);
        if (pkg_http_get(url, destination) != 0) {
            if (make_repo_index_url(repositories[i], "index.txt", url, sizeof(url)) != 0 || pkg_http_get(url, destination) != 0) {
                printf("pkg: failed to download repository index from %s\n", repositories[i]);
                ++failures;
                continue;
            }
        }
    }
    if (failures) return 1;
    puts("pkg: repository indices updated");
    return 0;
}

static int next_field(char **cursor, char *output, size_t capacity) {
    while (**cursor == ' ' || **cursor == '\t') ++*cursor;
    if (!**cursor || **cursor == '#') return 0;
    size_t used = 0;
    while (**cursor && **cursor != ' ' && **cursor != '\t' && **cursor != '\r') {
        if (used + 1 >= capacity) return -1;
        output[used++] = *(*cursor)++;
    }
    output[used] = 0;
    return 1;
}

static int make_archive_url(const char *repository, const char *archive, char *output, size_t capacity) {
    if (strncmp(archive, "http://", 7) == 0 || strncmp(archive, "https://", 8) == 0) {
        if (strlen(archive) >= capacity) return -1;
        strcpy(output, archive);
        return 0;
    }
    if (archive[0] != '/') return pkg_join(output, capacity, repository, archive);
    size_t scheme_length = strncmp(repository, "https://", 8) == 0 ? 8 : 7;
    const char *host = repository + scheme_length;
    const char *path = host;
    while (*path && *path != '/') ++path;
    size_t prefix = (size_t)(path - repository);
    if (prefix + strlen(archive) + 1 > capacity) return -1;
    memcpy(output, repository, prefix);
    strcpy(output + prefix, archive);
    return 0;
}

static int parse_index(const char *path, const char *repository, const char *wanted, struct pkg_entry *best, int *found) {
    char content[PKG_TEXT_MAX];
    size_t length = 0;
    if (pkg_read_file(path, content, sizeof(content) - 1, &length) != 0) return -1;
    content[length] = 0;
    char *line = content;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') ++end;
        char saved = *end;
        *end = 0;
        char *cursor = line;
        char name[PKG_NAME_MAX];
        char version[PKG_VERSION_MAX];
        char archive[PKG_URL_MAX];
        int a = next_field(&cursor, name, sizeof(name));
        int b = next_field(&cursor, version, sizeof(version));
        int c = next_field(&cursor, archive, sizeof(archive));
        if (a < 0 || b < 0 || c < 0) return -1;
        if (a && b && c && !strcmp(name, wanted) && pkg_valid_name(name)) {
            struct pkg_entry candidate;
            memset(&candidate, 0, sizeof(candidate));
            strcpy(candidate.name, name);
            strcpy(candidate.version, version);
            strcpy(candidate.repo, repository);
            if (make_archive_url(repository, archive, candidate.archive, sizeof(candidate.archive)) != 0) return -1;
            if (!*found || pkg_version_compare(candidate.version, best->version) > 0) {
                *best = candidate;
                *found = 1;
            }
        }
        if (!saved) break;
        line = end + 1;
    }
    return 0;
}

int pkg_find_best(const char *name, struct pkg_entry *entry) {
    if (!pkg_valid_name(name) || !entry) return -1;
    char repositories[PKG_REPO_MAX][PKG_URL_MAX];
    size_t count = 0;
    if (read_repositories(repositories, &count) != 0 || !count) return -1;
    int found = 0;
    int index_exists = 0;
    for (size_t i = 0; i < count; ++i) {
        char path[PKG_PATH_MAX];
        if (make_index_path(i, path, sizeof(path)) != 0) continue;
        struct stat status;
        if (stat(path, &status) != 0) continue;
        index_exists = 1;
        if (parse_index(path, repositories[i], name, entry, &found) != 0) return -1;
    }
    if (!index_exists) {
        puts("pkg: run 'pkg update' before installing packages");
        return -1;
    }
    return found ? 0 : -1;
}

int pkg_version_compare(const char *left, const char *right) {
    const char *a = left;
    const char *b = right;
    while (*a || *b) {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            const char *aa = a;
            const char *bb = b;
            while (*aa == '0') ++aa;
            while (*bb == '0') ++bb;
            const char *ae = aa;
            const char *be = bb;
            while (*ae >= '0' && *ae <= '9') ++ae;
            while (*be >= '0' && *be <= '9') ++be;
            size_t al = (size_t)(ae - aa);
            size_t bl = (size_t)(be - bb);
            if (al != bl) return al > bl ? 1 : -1;
            int cmp = strncmp(aa, bb, al);
            if (cmp) return cmp > 0 ? 1 : -1;
            a = ae;
            b = be;
            continue;
        }
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return ca > cb ? 1 : -1;
        if (*a) ++a;
        if (*b) ++b;
    }
    return 0;
}
