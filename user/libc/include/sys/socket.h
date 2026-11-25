#ifndef ORION_SYS_SOCKET_H
#define ORION_SYS_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef uint16_t sa_family_t;
typedef uint32_t socklen_t;

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

#define AF_UNSPEC 0
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_NONBLOCK 0x800
#define MSG_DONTWAIT 0x40
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

int socket(int domain, int type, int protocol);
int bind(int descriptor, const struct sockaddr *address, socklen_t length);
int listen(int descriptor, int backlog);
int accept(int descriptor, struct sockaddr *address, socklen_t *length);
int connect(int descriptor, const struct sockaddr *address, socklen_t length);
ssize_t send(int descriptor, const void *buffer, size_t length, int flags);
ssize_t recv(int descriptor, void *buffer, size_t length, int flags);
ssize_t sendto(int descriptor, const void *buffer, size_t length, int flags, const struct sockaddr *destination, socklen_t destination_length);
ssize_t recvfrom(int descriptor, void *buffer, size_t length, int flags, struct sockaddr *source, socklen_t *source_length);
int shutdown(int descriptor, int how);

#endif
