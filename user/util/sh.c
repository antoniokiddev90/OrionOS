#include "common.h"
static void print_motd(void) {
    if (util_print_file("/etc/motd") != 0) puts("OrionOS shell");
}
static int split_line(char *line, char **arguments, int capacity) {
    int count = 0;
    char *position = line;
    while (*position && count < capacity - 1) {
        while (*position == ' ' || *position == '\t') ++position;
        if (!*position) break;
        arguments[count++] = position;
        while (*position && *position != ' ' && *position != '\t') ++position;
        if (*position) *position++ = 0;
    }
    arguments[count] = NULL;
    return count;
}
static int run_command(int argc, char **arguments) {
    if (!argc) return 0;
    if (!strcmp(arguments[0], "cd")) {
        const char *path = argc > 1 ? arguments[1] : "/";
        if (chdir(path) != 0) { printf("cd: cannot change to %s\n", path); return 1; }
        return 0;
    }
    if (!strcmp(arguments[0], "exit")) return -1;
    pid_t child = fork();
    if (child < 0) { puts("sh: fork failed"); return 1; }
    if (child == 0) {
        char path[256];
        if (arguments[0][0] == '/') strcpy(path, arguments[0]);
        else if (strchr(arguments[0], '/')) strcpy(path, arguments[0]);
        else { strcpy(path, "/bin/"); strcat(path, arguments[0]); }
        execve(path, arguments, NULL);
        printf("sh: command not found: %s\n", arguments[0]);
        exit(127);
    }
    int status = 0;
    wait4(child, &status, 0, NULL);
    return 0;
}
int main(void) {
    print_motd();
    for (;;) {
        char current[256];
        if (!getcwd(current, sizeof(current))) strcpy(current, "/");
        printf("<%s> # ", current);
        char line[256];
        size_t length = 0;
        for (;;) {
            int character = getchar();
            if (character < 0) return 0;
            if (character == 4) {
                if (!length) return 0;
                continue;
            }
            if (character == '\n') { putchar('\n'); break; }
            if (character == '\b') {
                if (length) { --length; write(STDOUT_FILENO, "\b \b", 3); }
                continue;
            }
            if (length + 1 < sizeof(line)) {
                line[length++] = (char)character;
                putchar(character);
            }
        }
        line[length] = 0;
        char *arguments[16];
        int count = split_line(line, arguments, 16);
        if (run_command(count, arguments) < 0) return 0;
    }
}
