#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    char *arguments[] = {"/bin/login", NULL};
    for (;;) {
        pid_t child = fork();
        if (child < 0) {
            puts("init: fork failed");
            sched_yield();
            continue;
        }
        if (child == 0) {
            execve("/bin/login", arguments, NULL);
            puts("init: cannot execute /bin/login");
            exit(127);
        }
        int status = 0;
        wait4(child, &status, 0, NULL);
    }
}
