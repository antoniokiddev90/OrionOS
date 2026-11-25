#ifndef ORION_NETINET_IN_H
#define ORION_NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

typedef uint32_t in_addr_t;

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    sa_family_t sin_family;
    uint16_t sin_port;
    struct in_addr sin_addr;
    uint8_t sin_zero[8];
};

#define INADDR_ANY 0x00000000U
#define INADDR_BROADCAST 0xFFFFFFFFU
#define INADDR_NONE 0xFFFFFFFFU
#define IPPROTO_ICMP 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

static inline uint16_t htons(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static inline uint16_t ntohs(uint16_t value) {
    return htons(value);
}

static inline uint32_t htonl(uint32_t value) {
    return ((value & 0x000000FFU) << 24) | ((value & 0x0000FF00U) << 8) |
           ((value & 0x00FF0000U) >> 8) | ((value & 0xFF000000U) >> 24);
}

static inline uint32_t ntohl(uint32_t value) {
    return htonl(value);
}

#endif
