#ifndef KERNEL_NET_NET_H
#define KERNEL_NET_NET_H

#include <stddef.h>
#include <stdint.h>

#define NET_FD_BASE 0x4000
#define NET_AF_INET 2
#define NET_SOCK_STREAM 1
#define NET_SOCK_DGRAM 2
#define NET_SOCK_NONBLOCK 0x800
#define NET_MSG_DONTWAIT 0x40

struct net_sockaddr_in {
    uint16_t family;
    uint16_t port;
    uint32_t address;
    uint8_t zero[8];
};

struct net_config {
    uint8_t address[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t dhcp_state;
};

int net_init(void);
void net_poll(void);
int net_get_config(struct net_config *config);
int net_dhcp_renew(void);
int net_socket_create(int domain, int type, int protocol, uint64_t owner);
int net_socket_bind(int descriptor, uint64_t owner, uint32_t address, uint16_t port);
int net_socket_listen(int descriptor, uint64_t owner, int backlog);
int net_socket_accept(int descriptor, uint64_t owner, int flags, uint32_t *address, uint16_t *port);
int net_socket_connect(int descriptor, uint64_t owner, uint32_t address, uint16_t port);
long net_socket_sendto(int descriptor, uint64_t owner, const void *buffer, size_t length, int flags, int has_address, uint32_t address, uint16_t port);
long net_socket_recvfrom(int descriptor, uint64_t owner, void *buffer, size_t length, int flags, int has_address, uint32_t *address, uint16_t *port);
int net_socket_shutdown(int descriptor, uint64_t owner, int how);
int net_socket_close(int descriptor, uint64_t owner);
void net_close_owner(uint64_t owner);

#endif
