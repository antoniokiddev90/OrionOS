#include <sys/dns.h>
#include <sys/network.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#define DNS_PACKET_CAPACITY 512
#define DNS_TIMEOUT_TICKS 400

static uint16_t dns_read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void dns_write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static int dns_same_address(const uint8_t left[4], const uint8_t right[4]) {
    for (size_t i = 0; i < 4; ++i) if (left[i] != right[i]) return 0;
    return 1;
}

static int dns_encode_name(const char *hostname, uint8_t *packet, size_t capacity, size_t *offset) {
    size_t length = strlen(hostname);
    if (!length || length > 253) return -1;
    size_t position = *offset;
    size_t index = 0;
    while (index < length) {
        size_t start = index;
        while (index < length && hostname[index] != '.') {
            unsigned char value = (unsigned char)hostname[index];
            if (value < 33 || value > 126 || value == '/') return -1;
            ++index;
        }
        size_t label_length = index - start;
        if (!label_length || label_length > 63 || position + label_length + 2 > capacity) return -1;
        packet[position++] = (uint8_t)label_length;
        for (size_t i = 0; i < label_length; ++i) packet[position++] = (uint8_t)hostname[start + i];
        if (index < length) {
            ++index;
            if (index == length) break;
        }
    }
    if (position + 1 > capacity || position - *offset + 1 > 255) return -1;
    packet[position++] = 0;
    *offset = position;
    return 0;
}

static int dns_skip_name(const uint8_t *packet, size_t length, size_t *offset) {
    size_t cursor = *offset;
    size_t consumed = 0;
    int jumped = 0;
    for (size_t steps = 0; steps < 128 && cursor < length; ++steps) {
        uint8_t label = packet[cursor];
        if (label == 0) {
            if (!jumped) ++consumed;
            *offset += consumed;
            return 0;
        }
        if ((label & 0xC0U) == 0xC0U) {
            if (cursor + 1 >= length) return -1;
            size_t pointer = ((size_t)(label & 0x3FU) << 8) | packet[cursor + 1];
            if (pointer >= length) return -1;
            if (!jumped) consumed += 2;
            cursor = pointer;
            jumped = 1;
            continue;
        }
        if (label & 0xC0U) return -1;
        if ((size_t)label > length - cursor - 1) return -1;
        if (!jumped) consumed += (size_t)label + 1;
        cursor += (size_t)label + 1;
    }
    return -1;
}

static int dns_parse_response(const uint8_t *packet, size_t length, uint16_t identifier, uint8_t address[4]) {
    if (length < 12 || dns_read_be16(packet) != identifier) return -1;
    uint16_t flags = dns_read_be16(packet + 2);
    if (!(flags & 0x8000U) || (flags & 0x7800U) || (flags & 0x0200U) || (flags & 0x000FU)) return -1;
    uint16_t questions = dns_read_be16(packet + 4);
    uint16_t answers = dns_read_be16(packet + 6);
    if (!questions) return -1;
    size_t offset = 12;
    for (uint16_t i = 0; i < questions; ++i) {
        if (dns_skip_name(packet, length, &offset) != 0 || offset + 4 > length) return -1;
        offset += 4;
    }
    for (uint16_t i = 0; i < answers; ++i) {
        if (dns_skip_name(packet, length, &offset) != 0 || offset + 10 > length) return -1;
        uint16_t type = dns_read_be16(packet + offset);
        uint16_t class_code = dns_read_be16(packet + offset + 2);
        uint16_t data_length = dns_read_be16(packet + offset + 8);
        offset += 10;
        if ((size_t)data_length > length - offset) return -1;
        if (type == 1 && class_code == 1 && data_length == 4) {
            memcpy(address, packet + offset, 4);
            return 0;
        }
        offset += data_length;
    }
    return -1;
}

int orion_dns_lookup_ipv4(const char *hostname, const uint8_t server[4], uint8_t address[4]) {
    if (!hostname || !address) return -1;
    uint8_t selected_server[4];
    if (server) {
        memcpy(selected_server, server, sizeof(selected_server));
    } else {
        struct orion_network_config config;
        if (orion_get_network_config(&config) != 0) return -1;
        memcpy(selected_server, config.dns, sizeof(selected_server));
    }
    if (!(selected_server[0] | selected_server[1] | selected_server[2] | selected_server[3])) return -1;

    uint8_t request[DNS_PACKET_CAPACITY];
    memset(request, 0, sizeof(request));
    uint16_t identifier = (uint16_t)(getticks() ^ ((uint64_t)(uint32_t)getpid() << 7) ^ 0xA761U);
    dns_write_be16(request, identifier);
    dns_write_be16(request + 2, 0x0100);
    dns_write_be16(request + 4, 1);
    size_t request_length = 12;
    if (dns_encode_name(hostname, request, sizeof(request), &request_length) != 0 || request_length + 4 > sizeof(request)) return -1;
    dns_write_be16(request + request_length, 1);
    dns_write_be16(request + request_length + 2, 1);
    request_length += 4;

    int descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (descriptor < 0) return -1;
    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(53);
    memcpy(&destination.sin_addr.s_addr, selected_server, 4);
    ssize_t sent = sendto(descriptor, request, request_length, 0,
                          (const struct sockaddr *)&destination, sizeof(destination));
    if (sent != (ssize_t)request_length) {
        close(descriptor);
        return -1;
    }

    uint64_t start = getticks();
    uint8_t response[DNS_PACKET_CAPACITY];
    int result = -1;
    while (getticks() - start < DNS_TIMEOUT_TICKS) {
        struct sockaddr_in source;
        socklen_t source_length = sizeof(source);
        ssize_t received = recvfrom(descriptor, response, sizeof(response), MSG_DONTWAIT,
                                    (struct sockaddr *)&source, &source_length);
        if (received > 0) {
            if (source.sin_port == htons(53) &&
                dns_same_address((const uint8_t *)&source.sin_addr.s_addr, selected_server)) {
                result = dns_parse_response(response, (size_t)received, identifier, address);
                break;
            }
        }
        sched_yield();
    }
    close(descriptor);
    return result;
}
