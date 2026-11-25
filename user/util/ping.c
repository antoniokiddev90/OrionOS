#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static uint16_t icmp_checksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;
    while (length >= 2) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length) sum += (uint32_t)data[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static int parse_count(const char *text, unsigned *count) {
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!text[0] || !end || *end || value < 1 || value > 100) return -1;
    *count = (unsigned)value;
    return 0;
}

int main(int argc, char **argv) {
    unsigned count = 4;
    const char *host = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0) {
            if (++i >= argc || parse_count(argv[i], &count) != 0) {
                puts("ping: invalid count");
                puts("Usage: ping [-c count] IPv4-address");
                return 2;
            }
        } else if (argv[i][0] == '-' || host) {
            puts("Usage: ping [-c count] IPv4-address");
            return 2;
        } else {
            host = argv[i];
        }
    }
    if (!host) {
        puts("Usage: ping [-c count] IPv4-address");
        return 2;
    }

    in_addr_t target = inet_addr(host);
    if (target == INADDR_NONE || target == INADDR_ANY) {
        puts("ping: invalid IPv4 address");
        return 2;
    }

    int descriptor = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_ICMP);
    if (descriptor < 0) {
        puts("ping: cannot create ICMP socket");
        return 1;
    }

    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = target;
    printf("PING %s: 32 data bytes\n", host);

    unsigned received = 0;
    uint16_t identifier = (uint16_t)getpid();
    for (unsigned sequence = 1; sequence <= count; ++sequence) {
        uint8_t request[40];
        memset(request, 0, sizeof(request));
        request[0] = 8;
        write_be16(request + 4, identifier);
        write_be16(request + 6, (uint16_t)sequence);
        for (size_t i = 8; i < sizeof(request); ++i) request[i] = (uint8_t)(i - 8);
        write_be16(request + 2, icmp_checksum(request, sizeof(request)));

        ssize_t sent = sendto(descriptor, request, sizeof(request), 0,
                              (const struct sockaddr *)&destination, sizeof(destination));
        if (sent != (ssize_t)sizeof(request)) {
            puts("ping: failed to send ICMP request");
            break;
        }

        uint64_t interval_start = getticks();
        int matched = 0;
        while (getticks() - interval_start < 100) {
            uint8_t response[128];
            struct sockaddr_in source;
            socklen_t source_length = sizeof(source);
            ssize_t length = recvfrom(descriptor, response, sizeof(response), 0,
                                      (struct sockaddr *)&source, &source_length);
            if (length >= 8 && source.sin_addr.s_addr == target && response[0] == 0 && response[1] == 0 &&
                read_be16(response + 4) == identifier && read_be16(response + 6) == (uint16_t)sequence) {
                uint64_t elapsed = (getticks() - interval_start) * 10;
                printf("%lu bytes from %s: icmp_seq=%u time=%lu ms\n",
                       (unsigned long)length, host, sequence, (unsigned long)elapsed);
                ++received;
                matched = 1;
                break;
            }
            sched_yield();
        }
        if (!matched) printf("Request timeout for icmp_seq %u\n", sequence);
    }

    close(descriptor);
    printf("--- %s ping statistics ---\n", host);
    printf("%u packets transmitted, %u received, %u%% packet loss\n",
           count, received, count ? ((count - received) * 100) / count : 0);
    return received ? 0 : 1;
}
