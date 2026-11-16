#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    char *arguments[] = {"/bin/sh", NULL};
    for (;;) {
        pid_t child = fork();
        if (child < 0) {
            puts("init: fork failed");
            sched_yield();
            continue;
        }
        if (child == 0) {
            execve("/bin/sh", arguments, NULL);
            puts("init: cannot execute /bin/sh");
            exit(127);
        }
        int status = 0;
        wait4(child, &status, 0, NULL);
    }
}
