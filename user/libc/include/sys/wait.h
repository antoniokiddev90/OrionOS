#ifndef ORION_SYS_WAIT_H
#define ORION_SYS_WAIT_H
#include <sys/types.h>
#define WNOHANG 1
pid_t wait4(pid_t pid, int *status, int options, void *usage);
pid_t waitpid(pid_t pid, int *status, int options);
#endif
