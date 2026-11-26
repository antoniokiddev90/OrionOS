#ifndef ORION_PKG_H
#define ORION_PKG_H
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/dns.h>

#define PKG_PATH_MAX 256
#define PKG_NAME_MAX 64
#define PKG_VERSION_MAX 64
#define PKG_LICENSE_MAX 64
#define PKG_URL_MAX 256
#define PKG_REPO_MAX 16
#define PKG_TEXT_MAX 16384
#define PKG_ARCHIVE_LIMIT 270000
#define PKG_REPO_LIST "/etc/pkg/repo.list"
#define PKG_DB_DIR "/etc/pkg/db"
#define PKG_CACHE_DIR "/var/cache/pkg"

struct pkg_metadata {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char license[PKG_LICENSE_MAX];
};

struct pkg_entry {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char archive[PKG_URL_MAX];
    char repo[PKG_URL_MAX];
};

int pkg_read_file(const char *path, char *buffer, size_t capacity, size_t *length);
int pkg_write_file(const char *path, const void *buffer, size_t length, unsigned mode);
int pkg_write_all(int descriptor, const void *buffer, size_t length);
int pkg_read_line(int descriptor, char *buffer, size_t capacity);
int pkg_mkdirs(const char *path);
int pkg_prepare_layout(void);
int pkg_join(char *output, size_t capacity, const char *left, const char *right);
int pkg_valid_name(const char *name);
int pkg_parse_config(const char *text, struct pkg_metadata *metadata);
int pkg_run_line(const char *line);
int pkg_run_script(const char *script);
int pkg_run_recipe(const char *path);
int pkg_http_get(const char *url, const char *output);
int pkg_repo_add(const char *url);
int pkg_repo_remove(const char *url);
int pkg_repo_list(void);
int pkg_update(void);
int pkg_find_best(const char *name, struct pkg_entry *entry);
int pkg_install(const char *name);
int pkg_remove(const char *name);
int pkg_upgrade(void);
int pkg_archive_inspect(const char *path, struct pkg_metadata *metadata, char *pre, size_t pre_capacity, char *post, size_t post_capacity);
int pkg_archive_install(const char *archive, const struct pkg_metadata *metadata, const char *database_path);
int pkg_archive_build(const char *source, const char *output);
int pkg_build(const char *directory);
int pkg_remove_record(const char *name, int quiet);
int pkg_read_record(const char *name, char *version, size_t version_capacity);
int pkg_version_compare(const char *left, const char *right);

#endif
