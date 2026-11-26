#include "pkg.h"

static void usage(void) {
    puts("Usage: pkg install <package>");
    puts("       pkg remove <package>");
    puts("       pkg update");
    puts("       pkg upgrade");
    puts("       pkg repo add <http-url>");
    puts("       pkg repo remove <http-url>");
    puts("       pkg repo list");
    puts("       pkg build <directory>");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (!strcmp(argv[1], "install")) {
        if (argc != 3) { usage(); return 2; }
        return pkg_install(argv[2]);
    }
    if (!strcmp(argv[1], "remove")) {
        if (argc != 3) { usage(); return 2; }
        return pkg_remove(argv[2]);
    }
    if (!strcmp(argv[1], "update")) {
        if (argc != 2) { usage(); return 2; }
        return pkg_update();
    }
    if (!strcmp(argv[1], "upgrade")) {
        if (argc != 2) { usage(); return 2; }
        return pkg_upgrade();
    }
    if (!strcmp(argv[1], "repo")) {
        if (argc == 3 && !strcmp(argv[2], "list")) return pkg_repo_list();
        if (argc != 4) { usage(); return 2; }
        if (!strcmp(argv[2], "add")) {
            if (pkg_repo_add(argv[3]) != 0) {
                puts("pkg: cannot add repository");
                return 1;
            }
            printf("pkg: added repository %s\n", argv[3]);
            return 0;
        }
        if (!strcmp(argv[2], "remove")) {
            if (pkg_repo_remove(argv[3]) != 0) {
                puts("pkg: cannot remove repository");
                return 1;
            }
            printf("pkg: removed repository %s\n", argv[3]);
            return 0;
        }
        usage();
        return 2;
    }
    if (!strcmp(argv[1], "build")) {
        if (argc != 3) { usage(); return 2; }
        return pkg_build(argv[2]);
    }
    usage();
    return 2;
}
