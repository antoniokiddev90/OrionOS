#ifndef ORION_UNISTD_H
#define ORION_UNISTD_H
#include <stddef.h>
#include <stdint.h>
typedef long ssize_t;
typedef long off_t;
typedef int pid_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
ssize_t read(int descriptor, void *buffer, size_t size);
ssize_t write(int descriptor, const void *buffer, size_t size);
int close(int descriptor);
off_t lseek(int descriptor, off_t offset, int whence);
pid_t fork(void);
int execve(const char *path, char *const arguments[], char *const environment[]);
pid_t wait4(pid_t pid, int *status, int options, void *usage);
pid_t waitpid(pid_t pid, int *status, int options);
int getdents64(int descriptor, void *buffer, size_t size);
int chdir(const char *path);
char *getcwd(char *buffer, size_t size);
pid_t getpid(void);
void sched_yield(void);
int dup(int descriptor);
int access(const char *path, int mode);
uid_t getuid(void);
gid_t getgid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
uid_t geteuid(void);
gid_t getegid(void);
int chown(const char *path, uid_t uid, gid_t gid);
uint64_t getticks(void);
#endif
