#include "net.h"
#include "../x64/e1000.h"
#include "../x64/pit.h"
#include <stddef.h>
#include <stdint.h>

#define ETH_HEADER_SIZE 14
#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP 0x0806
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP 6
#define IP_PROTOCOL_UDP 17
#define IP_HEADER_SIZE 20
#define UDP_HEADER_SIZE 8
#define TCP_HEADER_SIZE 20
#define NET_MAX_PACKET 1500
#define NET_MAX_SOCKETS 16
#define NET_RX_DATAGRAMS 4
#define NET_RX_BYTES 2048
#define NET_MAX_DATAGRAM 1400
#define ARP_ENTRIES 8
#define ARP_PENDING 4
#define DHCP_PACKET_SIZE 576
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_STATE_OPEN 1
#define TCP_STATE_LISTEN 2
#define TCP_STATE_SYN_SENT 3
#define TCP_STATE_SYN_RECEIVED 4
#define TCP_STATE_ESTABLISHED 5
#define TCP_STATE_CLOSED 6

struct arp_entry {
    uint32_t address;
    uint8_t mac[6];
    int used;
};

struct arp_pending_packet {
    uint32_t next_hop;
    uint16_t length;
    int used;
    uint8_t packet[NET_MAX_PACKET];
};

struct socket_datagram {
    uint16_t length;
    uint16_t port;
    uint32_t address;
    uint8_t data[NET_MAX_DATAGRAM];
};

struct net_socket {
    uint64_t owner;
    uint32_t local_address;
    uint32_t remote_address;
    uint32_t tx_next;
    uint32_t tx_unacked;
    uint32_t rx_next;
    uint32_t last_tx_tick;
    uint16_t local_port;
    uint16_t remote_port;
    uint16_t remote_window;
    uint16_t rx_length;
    uint16_t datagram_head;
    uint16_t datagram_count;
    uint8_t type;
    uint8_t protocol;
    uint8_t state;
    uint8_t nonblocking;
    uint8_t peer_closed;
    uint8_t read_shutdown;
    uint8_t write_shutdown;
    uint8_t retries;
    int16_t pending_slot;
    int16_t listener_slot;
    int used;
    uint8_t rx_data[NET_RX_BYTES];
    struct socket_datagram datagrams[NET_RX_DATAGRAMS];
};

static struct arp_entry arp_table[ARP_ENTRIES];
static struct arp_pending_packet arp_queue[ARP_PENDING];
static struct net_socket sockets[NET_MAX_SOCKETS];
static uint8_t local_mac[6];
static uint32_t local_address;
static uint32_t subnet_mask;
static uint32_t gateway_address;
static uint32_t dns_address;
static uint32_t dhcp_xid;
static uint32_t offered_address;
static uint32_t dhcp_server;
static uint16_t next_ephemeral = 49152;
static uint16_t next_ip_identification = 1;
static uint8_t dhcp_state;
static uint8_t dhcp_retries;
static uint8_t network_initialized;
static uint64_t dhcp_retry_tick;
static uint8_t receive_frame[E1000_MAX_FRAME_SIZE];

static void copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) to[i] = from[i];
}

static void zero_bytes(void *destination, size_t length) {
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
}

static uint16_t read_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static uint32_t checksum_add(uint32_t sum, const uint8_t *data, size_t length) {
    while (length >= 2) {
        sum += ((uint32_t)data[0] << 8) | data[1];
        data += 2;
        length -= 2;
    }
    if (length) sum += (uint32_t)data[0] << 8;
    return sum;
}

static uint16_t checksum_finish(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t internet_checksum(const uint8_t *data, size_t length) {
    return checksum_finish(checksum_add(0, data, length));
}

static uint16_t transport_checksum(uint32_t source, uint32_t destination, uint8_t protocol, const uint8_t *data, uint16_t length) {
    uint8_t pseudo[12];
    write_be32(pseudo, source);
    write_be32(pseudo + 4, destination);
    pseudo[8] = 0;
    pseudo[9] = protocol;
    write_be16(pseudo + 10, length);
    uint32_t sum = checksum_add(0, pseudo, sizeof(pseudo));
    sum = checksum_add(sum, data, length);
    return checksum_finish(sum);
}

static int mac_is_zero(const uint8_t *mac) {
    uint8_t value = 0;
    for (size_t i = 0; i < 6; ++i) value |= mac[i];
    return value == 0;
}

static int ethernet_send(const uint8_t destination[6], uint16_t type, const void *payload, uint16_t length) {
    uint8_t frame[NET_MAX_PACKET + ETH_HEADER_SIZE];
    if (length > NET_MAX_PACKET) return -1;
    copy_bytes(frame, destination, 6);
    copy_bytes(frame + 6, local_mac, 6);
    write_be16(frame + 12, type);
    copy_bytes(frame + ETH_HEADER_SIZE, payload, length);
    return e1000_send(frame, (uint16_t)(ETH_HEADER_SIZE + length));
}

static int arp_lookup(uint32_t address, uint8_t mac[6]) {
    for (size_t i = 0; i < ARP_ENTRIES; ++i) {
        if (arp_table[i].used && arp_table[i].address == address) {
            copy_bytes(mac, arp_table[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

static void ipv4_transmit_resolved(uint32_t destination, const uint8_t mac[6], const uint8_t *packet, uint16_t length) {
    (void)destination;
    (void)ethernet_send(mac, ETH_TYPE_IPV4, packet, length);
}

static void arp_remember(uint32_t address, const uint8_t mac[6]) {
    if (!address || mac_is_zero(mac) || (mac[0] & 1U)) return;
    size_t slot = ARP_ENTRIES;
    for (size_t i = 0; i < ARP_ENTRIES; ++i) {
        if (arp_table[i].used && arp_table[i].address == address) {
            slot = i;
            break;
        }
        if (!arp_table[i].used && slot == ARP_ENTRIES) slot = i;
    }
    if (slot == ARP_ENTRIES) slot = (size_t)(address % ARP_ENTRIES);
    arp_table[slot].used = 1;
    arp_table[slot].address = address;
    copy_bytes(arp_table[slot].mac, mac, 6);
    for (size_t i = 0; i < ARP_PENDING; ++i) {
        if (!arp_queue[i].used || arp_queue[i].next_hop != address) continue;
        ipv4_transmit_resolved(address, mac, arp_queue[i].packet, arp_queue[i].length);
        arp_queue[i].used = 0;
    }
}

static void arp_send_request(uint32_t target) {
    uint8_t packet[28];
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    zero_bytes(packet, sizeof(packet));
    write_be16(packet, 1);
    write_be16(packet + 2, ETH_TYPE_IPV4);
    packet[4] = 6;
    packet[5] = 4;
    write_be16(packet + 6, 1);
    copy_bytes(packet + 8, local_mac, 6);
    write_be32(packet + 14, local_address);
    write_be32(packet + 24, target);
    (void)ethernet_send(broadcast, ETH_TYPE_ARP, packet, sizeof(packet));
}

static void arp_send_reply(const uint8_t destination_mac[6], uint32_t destination_address) {
    uint8_t packet[28];
    zero_bytes(packet, sizeof(packet));
    write_be16(packet, 1);
    write_be16(packet + 2, ETH_TYPE_IPV4);
    packet[4] = 6;
    packet[5] = 4;
    write_be16(packet + 6, 2);
    copy_bytes(packet + 8, local_mac, 6);
    write_be32(packet + 14, local_address);
    copy_bytes(packet + 18, destination_mac, 6);
    write_be32(packet + 24, destination_address);
    (void)ethernet_send(destination_mac, ETH_TYPE_ARP, packet, sizeof(packet));
}

static void arp_process(const uint8_t *packet, uint16_t length, const uint8_t ethernet_source[6]) {
    if (length < 28 || read_be16(packet) != 1 || read_be16(packet + 2) != ETH_TYPE_IPV4 ||
        packet[4] != 6 || packet[5] != 4) return;
    for (size_t i = 0; i < 6; ++i) if (packet[8 + i] != ethernet_source[i]) return;
    uint16_t operation = read_be16(packet + 6);
    uint32_t sender_address = read_be32(packet + 14);
    uint32_t target_address = read_be32(packet + 24);
    const uint8_t *sender_mac = packet + 8;
    if (sender_address) arp_remember(sender_address, sender_mac);
    if (operation == 1 && local_address && target_address == local_address) {
        arp_send_reply(sender_mac, sender_address);
    }
}

static int ipv4_emit_packet(uint32_t destination, const uint8_t *packet, uint16_t length) {
    uint8_t mac[6];
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (destination == 0xFFFFFFFFU) return ethernet_send(broadcast, ETH_TYPE_IPV4, packet, length);
    if (!local_address) return -1;
    uint32_t next_hop = destination;
    if (subnet_mask && ((destination & subnet_mask) != (local_address & subnet_mask))) {
        if (!gateway_address) return -1;
        next_hop = gateway_address;
    }
    if (arp_lookup(next_hop, mac) == 0) {
        ipv4_transmit_resolved(next_hop, mac, packet, length);
        return 0;
    }
    size_t pending_slot = ARP_PENDING;
    int request_needed = 1;
    for (size_t i = 0; i < ARP_PENDING; ++i) {
        if (arp_queue[i].used && arp_queue[i].next_hop == next_hop) {
            request_needed = 0;
        }
        if (!arp_queue[i].used && pending_slot == ARP_PENDING) pending_slot = i;
    }
    if (pending_slot == ARP_PENDING) return -1;
    if (length > NET_MAX_PACKET) return -1;
    arp_queue[pending_slot].used = 1;
    arp_queue[pending_slot].next_hop = next_hop;
    arp_queue[pending_slot].length = length;
    copy_bytes(arp_queue[pending_slot].packet, packet, length);
    if (request_needed) arp_send_request(next_hop);
    return 0;
}

static int ipv4_send_from(uint32_t source, uint32_t destination, uint8_t protocol, const void *payload, uint16_t length) {
    uint8_t packet[NET_MAX_PACKET];
    uint16_t total = (uint16_t)(IP_HEADER_SIZE + length);
    if (total > NET_MAX_PACKET) return -1;
    packet[0] = 0x45;
    packet[1] = 0;
    write_be16(packet + 2, total);
    write_be16(packet + 4, next_ip_identification++);
    write_be16(packet + 6, 0x4000);
    packet[8] = 64;
    packet[9] = protocol;
    packet[10] = 0;
    packet[11] = 0;
    write_be32(packet + 12, source);
    write_be32(packet + 16, destination);
    if (length) copy_bytes(packet + IP_HEADER_SIZE, payload, length);
    write_be16(packet + 10, internet_checksum(packet, IP_HEADER_SIZE));
    return ipv4_emit_packet(destination, packet, total);
}

static int udp_send_from(uint32_t source_address, uint32_t destination_address, uint16_t source_port, uint16_t destination_port, const void *data, uint16_t length) {
    uint8_t packet[UDP_HEADER_SIZE + NET_MAX_DATAGRAM];
    uint16_t total = (uint16_t)(UDP_HEADER_SIZE + length);
    if (length > NET_MAX_DATAGRAM) return -1;
    write_be16(packet, source_port);
    write_be16(packet + 2, destination_port);
    write_be16(packet + 4, total);
    write_be16(packet + 6, 0);
    if (length) copy_bytes(packet + UDP_HEADER_SIZE, data, length);
    uint16_t checksum = transport_checksum(source_address, destination_address, IP_PROTOCOL_UDP, packet, total);
    if (!checksum) checksum = 0xFFFF;
    write_be16(packet + 6, checksum);
    return ipv4_send_from(source_address, destination_address, IP_PROTOCOL_UDP, packet, total);
}

static int tcp_send_raw(struct net_socket *socket, uint8_t flags, uint32_t sequence, uint32_t acknowledgment, const void *data, uint16_t length) {
    uint8_t packet[TCP_HEADER_SIZE + NET_MAX_DATAGRAM];
    uint16_t total = (uint16_t)(TCP_HEADER_SIZE + length);
    if (length > NET_MAX_DATAGRAM) return -1;
    zero_bytes(packet, TCP_HEADER_SIZE);
    write_be16(packet, socket->local_port);
    write_be16(packet + 2, socket->remote_port);
    write_be32(packet + 4, sequence);
    write_be32(packet + 8, acknowledgment);
    packet[12] = 0x50;
    packet[13] = flags;
    write_be16(packet + 14, (uint16_t)(NET_RX_BYTES - socket->rx_length));
    if (length) copy_bytes(packet + TCP_HEADER_SIZE, data, length);
    uint16_t checksum = transport_checksum(socket->local_address, socket->remote_address, IP_PROTOCOL_TCP, packet, total);
    write_be16(packet + 16, checksum);
    return ipv4_send_from(socket->local_address, socket->remote_address, IP_PROTOCOL_TCP, packet, total);
}

static void dhcp_send_message(uint8_t message_type) {
    uint8_t packet[DHCP_PACKET_SIZE];
    size_t offset = 240;
    zero_bytes(packet, sizeof(packet));
    packet[0] = 1;
    packet[1] = 1;
    packet[2] = 6;
    write_be32(packet + 4, dhcp_xid);
    packet[10] = 0x80;
    copy_bytes(packet + 28, local_mac, 6);
    packet[236] = 99;
    packet[237] = 130;
    packet[238] = 83;
    packet[239] = 99;
    packet[offset++] = 53;
    packet[offset++] = 1;
    packet[offset++] = message_type;
    if (message_type == 3) {
        packet[offset++] = 50;
        packet[offset++] = 4;
        write_be32(packet + offset, offered_address);
        offset += 4;
        if (dhcp_server) {
            packet[offset++] = 54;
            packet[offset++] = 4;
            write_be32(packet + offset, dhcp_server);
            offset += 4;
        }
    }
    packet[offset++] = 55;
    packet[offset++] = 4;
    packet[offset++] = 1;
    packet[offset++] = 3;
    packet[offset++] = 6;
    packet[offset++] = 15;
    packet[offset++] = 255;
    if (udp_send_from(0, 0xFFFFFFFFU, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, (uint16_t)offset) == 0) {
        dhcp_state = message_type == 1 ? 1 : 2;
        dhcp_retry_tick = pit_ticks();
    }
}

static void dhcp_process(uint32_t source_address, const uint8_t *packet, uint16_t length) {
    if (length < 240 || packet[0] != 2 || packet[1] != 1 || packet[2] != 6 ||
        read_be32(packet + 4) != dhcp_xid || packet[236] != 99 || packet[237] != 130 ||
        packet[238] != 83 || packet[239] != 99) return;
    for (size_t i = 0; i < 6; ++i) if (packet[28 + i] != local_mac[i]) return;
    uint32_t offered = read_be32(packet + 16);
    uint8_t message_type = 0;
    uint32_t server = source_address;
    uint32_t mask = 0;
    uint32_t router = 0;
    uint32_t dns = dns_address;
    size_t offset = 240;
    while (offset < length) {
        uint8_t option = packet[offset++];
        if (option == 255) break;
        if (option == 0) continue;
        if (offset >= length) return;
        uint8_t option_length = packet[offset++];
        if ((size_t)option_length > length - offset) return;
        if (option == 53 && option_length == 1) message_type = packet[offset];
        else if (option == 54 && option_length == 4) server = read_be32(packet + offset);
        else if (option == 1 && option_length == 4) mask = read_be32(packet + offset);
        else if (option == 3 && option_length >= 4) router = read_be32(packet + offset);
        else if (option == 6 && option_length >= 4) dns = read_be32(packet + offset);
        offset += option_length;
    }
    if (message_type == 2 && offered && (dhcp_state == 1 || dhcp_state == 2)) {
        offered_address = offered;
        dhcp_server = server;
        dns_address = dns;
        dhcp_send_message(3);
    } else if (message_type == 5 && offered && dhcp_state == 2 && (!dhcp_server || server == dhcp_server)) {
        offered_address = offered;
        dhcp_server = server;
        local_address = offered;
        subnet_mask = mask ? mask : 0xFFFFFF00U;
        gateway_address = router;
        dns_address = dns;
        dhcp_state = 3;
        dhcp_retries = 0;
    } else if (message_type == 6) {
        local_address = 0;
        subnet_mask = 0;
        gateway_address = 0;
        dns_address = 0;
        dhcp_state = 0;
        dhcp_send_message(1);
    }
}

static void udp_process(uint32_t source_address, uint32_t destination_address, const uint8_t *packet, uint16_t length) {
    if (length < UDP_HEADER_SIZE) return;
    uint16_t source_port = read_be16(packet);
    uint16_t destination_port = read_be16(packet + 2);
    uint16_t udp_length = read_be16(packet + 4);
    if (udp_length < UDP_HEADER_SIZE || udp_length > length) return;
    uint16_t supplied_checksum = read_be16(packet + 6);
    if (supplied_checksum && transport_checksum(source_address, destination_address, IP_PROTOCOL_UDP, packet, udp_length) != 0) return;
    const uint8_t *payload = packet + UDP_HEADER_SIZE;
    uint16_t payload_length = (uint16_t)(udp_length - UDP_HEADER_SIZE);
    if (destination_port == DHCP_CLIENT_PORT && source_port == DHCP_SERVER_PORT) {
        dhcp_process(source_address, payload, payload_length);
        return;
    }
    for (size_t i = 0; i < NET_MAX_SOCKETS; ++i) {
        struct net_socket *socket = &sockets[i];
        if (!socket->used || socket->type != NET_SOCK_DGRAM || socket->protocol != IP_PROTOCOL_UDP ||
            socket->local_port != destination_port) continue;
        if (socket->local_address && socket->local_address != destination_address) continue;
        if (socket->remote_address && (socket->remote_address != source_address || socket->remote_port != source_port)) continue;
        if (socket->datagram_count >= NET_RX_DATAGRAMS || payload_length > NET_MAX_DATAGRAM) return;
        uint16_t index = (uint16_t)((socket->datagram_head + socket->datagram_count) % NET_RX_DATAGRAMS);
        struct socket_datagram *datagram = &socket->datagrams[index];
        datagram->address = source_address;
        datagram->port = source_port;
        datagram->length = payload_length;
        if (payload_length) copy_bytes(datagram->data, payload, payload_length);
        ++socket->datagram_count;
        return;
    }
}

static int socket_slot_from_descriptor(int descriptor) {
    if (descriptor < NET_FD_BASE) return -1;
    int slot = descriptor - NET_FD_BASE;
    return slot >= 0 && slot < NET_MAX_SOCKETS ? slot : -1;
}

static struct net_socket *socket_get(int descriptor, uint64_t owner, int *slot_out) {
    int slot = socket_slot_from_descriptor(descriptor);
    if (slot < 0 || !sockets[slot].used || sockets[slot].owner != owner) return NULL;
    if (slot_out) *slot_out = slot;
    return &sockets[slot];
}

static int socket_allocate(uint64_t owner, uint8_t type) {
    for (int i = 0; i < NET_MAX_SOCKETS; ++i) {
        if (sockets[i].used) continue;
        zero_bytes(&sockets[i], sizeof(sockets[i]));
        sockets[i].used = 1;
        sockets[i].owner = owner;
        sockets[i].type = type;
        sockets[i].protocol = type == NET_SOCK_STREAM ? IP_PROTOCOL_TCP : IP_PROTOCOL_UDP;
        sockets[i].state = TCP_STATE_OPEN;
        sockets[i].pending_slot = -1;
        sockets[i].listener_slot = -1;
        return i;
    }
    return -1;
}

static int port_in_use(uint8_t type, uint16_t port, int except_slot) {
    for (int i = 0; i < NET_MAX_SOCKETS; ++i) {
        if (i != except_slot && sockets[i].used && sockets[i].type == type && sockets[i].local_port == port) return 1;
    }
    return 0;
}

static uint16_t allocate_ephemeral(uint8_t type, int slot) {
    for (uint32_t attempt = 0; attempt < 16384; ++attempt) {
        uint16_t candidate = next_ephemeral++;
        if (next_ephemeral < 49152) next_ephemeral = 49152;
        if (!port_in_use(type, candidate, slot)) return candidate;
    }
    return 0;
}

static int endpoint_matches(const struct net_socket *socket, uint32_t local_address, uint16_t local_port, uint32_t remote_address, uint16_t remote_port) {
    return socket->used && socket->type == NET_SOCK_STREAM &&
           socket->local_port == local_port &&
           (!socket->local_address || socket->local_address == local_address) &&
           socket->remote_address == remote_address && socket->remote_port == remote_port &&
           socket->state != TCP_STATE_LISTEN && socket->state != TCP_STATE_OPEN;
}

static int listener_find(uint32_t local_address, uint16_t local_port) {
    for (int i = 0; i < NET_MAX_SOCKETS; ++i) {
        struct net_socket *socket = &sockets[i];
        if (socket->used && socket->type == NET_SOCK_STREAM && socket->state == TCP_STATE_LISTEN &&
            socket->local_port == local_port && (!socket->local_address || socket->local_address == local_address) &&
            socket->pending_slot < 0) return i;
    }
    return -1;
}

static uint32_t initial_sequence(uint16_t local_port, uint16_t remote_port, uint32_t remote_address) {
    uint32_t value = (uint32_t)pit_ticks() ^ ((uint32_t)local_port << 16) ^ remote_address ^ remote_port;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

static void tcp_input(uint32_t source_address, uint32_t destination_address, const uint8_t *packet, uint16_t length) {
    if (length < TCP_HEADER_SIZE) return;
    uint16_t source_port = read_be16(packet);
    uint16_t destination_port = read_be16(packet + 2);
    uint32_t sequence = read_be32(packet + 4);
    uint32_t acknowledgment = read_be32(packet + 8);
    uint8_t header_length = (uint8_t)((packet[12] >> 4) * 4);
    uint8_t flags = packet[13];
    uint16_t window = read_be16(packet + 14);
    if (header_length < TCP_HEADER_SIZE || header_length > length ||
        transport_checksum(source_address, destination_address, IP_PROTOCOL_TCP, packet, length) != 0) return;
    uint16_t payload_length = (uint16_t)(length - header_length);
    const uint8_t *payload = packet + header_length;
    int slot = -1;
    struct net_socket *socket = NULL;
    for (int i = 0; i < NET_MAX_SOCKETS; ++i) {
        if (endpoint_matches(&sockets[i], destination_address, destination_port, source_address, source_port)) {
            slot = i;
            socket = &sockets[i];
            break;
        }
    }
    if (!socket && (flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK)) {
        int listener_slot = listener_find(destination_address, destination_port);
        if (listener_slot < 0) return;
        slot = socket_allocate(sockets[listener_slot].owner, NET_SOCK_STREAM);
        if (slot < 0) return;
        socket = &sockets[slot];
        socket->local_address = destination_address;
        socket->remote_address = source_address;
        socket->local_port = destination_port;
        socket->remote_port = source_port;
        socket->remote_window = window;
        socket->state = TCP_STATE_SYN_RECEIVED;
        socket->listener_slot = (int16_t)listener_slot;
        socket->rx_next = sequence + 1;
        socket->tx_unacked = initial_sequence(destination_port, source_port, source_address);
        socket->tx_next = socket->tx_unacked + 1;
        socket->last_tx_tick = (uint32_t)pit_ticks();
        (void)tcp_send_raw(socket, TCP_FLAG_SYN | TCP_FLAG_ACK, socket->tx_unacked, socket->rx_next, NULL, 0);
        return;
    }
    if (!socket) return;
    if (flags & TCP_FLAG_RST) {
        socket->state = TCP_STATE_CLOSED;
        socket->peer_closed = 1;
        return;
    }
    socket->remote_window = window;
    if (socket->state == TCP_STATE_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) && acknowledgment == socket->tx_next) {
            socket->tx_unacked = acknowledgment;
            socket->rx_next = sequence + 1;
            socket->state = TCP_STATE_ESTABLISHED;
            socket->retries = 0;
            (void)tcp_send_raw(socket, TCP_FLAG_ACK, socket->tx_next, socket->rx_next, NULL, 0);
        }
        return;
    }
    if (socket->state == TCP_STATE_SYN_RECEIVED) {
        if ((flags & TCP_FLAG_ACK) && acknowledgment == socket->tx_next) {
            socket->tx_unacked = acknowledgment;
            socket->state = TCP_STATE_ESTABLISHED;
            socket->retries = 0;
            if (socket->listener_slot >= 0 && socket->listener_slot < NET_MAX_SOCKETS && sockets[socket->listener_slot].used) {
                sockets[socket->listener_slot].pending_slot = (int16_t)slot;
            }
        }
        return;
    }
    if (socket->state != TCP_STATE_ESTABLISHED) return;
    if ((flags & TCP_FLAG_ACK) && (int32_t)(acknowledgment - socket->tx_unacked) >= 0 &&
        (int32_t)(socket->tx_next - acknowledgment) >= 0) socket->tx_unacked = acknowledgment;
    if (socket->peer_closed && (payload_length || (flags & TCP_FLAG_FIN))) {
        (void)tcp_send_raw(socket, TCP_FLAG_ACK, socket->tx_next, socket->rx_next, NULL, 0);
        return;
    }
    if (payload_length || (flags & TCP_FLAG_FIN)) {
        if (sequence == socket->rx_next) {
            int accepted = 1;
            if (payload_length) {
                if (socket->read_shutdown) {
                    socket->rx_next += payload_length;
                } else if (payload_length <= NET_RX_BYTES - socket->rx_length) {
                    copy_bytes(socket->rx_data + socket->rx_length, payload, payload_length);
                    socket->rx_length = (uint16_t)(socket->rx_length + payload_length);
                    socket->rx_next += payload_length;
                } else {
                    accepted = 0;
                }
            }
            if (accepted && (flags & TCP_FLAG_FIN)) {
                socket->rx_next++;
                socket->peer_closed = 1;
            }
        }
        (void)tcp_send_raw(socket, TCP_FLAG_ACK, socket->tx_next, socket->rx_next, NULL, 0);
    }
    if (socket->state == TCP_STATE_CLOSED) socket->peer_closed = 1;
}

static void icmp_process(uint32_t source_address, uint32_t destination_address, const uint8_t *packet, uint16_t length) {
    if (length < 8 || destination_address != local_address || internet_checksum(packet, length) != 0) return;
    if (!source_address) return;
    if (packet[0] == 0 && packet[1] == 0) {
        for (size_t i = 0; i < NET_MAX_SOCKETS; ++i) {
            struct net_socket *socket = &sockets[i];
            if (!socket->used || socket->type != NET_SOCK_DGRAM || socket->protocol != IP_PROTOCOL_ICMP ||
                (socket->local_address && socket->local_address != destination_address) ||
                (socket->remote_address && socket->remote_address != source_address) ||
                socket->datagram_count >= NET_RX_DATAGRAMS || length > NET_MAX_DATAGRAM) continue;
            uint16_t index = (uint16_t)((socket->datagram_head + socket->datagram_count) % NET_RX_DATAGRAMS);
            struct socket_datagram *datagram = &socket->datagrams[index];
            datagram->address = source_address;
            datagram->port = 0;
            datagram->length = length;
            copy_bytes(datagram->data, packet, length);
            ++socket->datagram_count;
        }
        return;
    }
    if (packet[0] != 8 || packet[1] != 0) return;
    uint8_t reply[NET_MAX_PACKET - IP_HEADER_SIZE];
    if (length > sizeof(reply)) return;
    copy_bytes(reply, packet, length);
    reply[0] = 0;
    reply[2] = 0;
    reply[3] = 0;
    write_be16(reply + 2, internet_checksum(reply, length));
    (void)ipv4_send_from(local_address, source_address, IP_PROTOCOL_ICMP, reply, length);
}

static void ipv4_process(const uint8_t *packet, uint16_t length, const uint8_t source_mac[6]) {
    if (length < IP_HEADER_SIZE || (packet[0] >> 4) != 4) return;
    uint8_t header_length = (uint8_t)((packet[0] & 0x0FU) * 4);
    uint16_t total_length = read_be16(packet + 2);
    if (header_length < IP_HEADER_SIZE || header_length > length || total_length < header_length || total_length > length) return;
    if (internet_checksum(packet, header_length) != 0 || (read_be16(packet + 6) & 0x3FFFU)) return;
    uint32_t source_address = read_be32(packet + 12);
    uint32_t destination_address = read_be32(packet + 16);
    if (source_address) arp_remember(source_address, source_mac);
    if (destination_address != local_address && destination_address != 0xFFFFFFFFU) return;
    const uint8_t *payload = packet + header_length;
    uint16_t payload_length = (uint16_t)(total_length - header_length);
    if (packet[9] == IP_PROTOCOL_ICMP) icmp_process(source_address, destination_address, payload, payload_length);
    else if (packet[9] == IP_PROTOCOL_UDP) udp_process(source_address, destination_address, payload, payload_length);
    else if (packet[9] == IP_PROTOCOL_TCP) tcp_input(source_address, destination_address, payload, payload_length);
}

static void ethernet_process(const uint8_t *frame, uint16_t length) {
    if (length < ETH_HEADER_SIZE) return;
    uint16_t type = read_be16(frame + 12);
    if (type == ETH_TYPE_ARP) arp_process(frame + ETH_HEADER_SIZE, (uint16_t)(length - ETH_HEADER_SIZE), frame + 6);
    else if (type == ETH_TYPE_IPV4) ipv4_process(frame + ETH_HEADER_SIZE, (uint16_t)(length - ETH_HEADER_SIZE), frame + 6);
}

int net_init(void) {
    if (network_initialized) return 0;
    if (e1000_get_mac(local_mac) != 0) return -1;
    local_address = 0;
    subnet_mask = 0;
    gateway_address = 0;
    dns_address = 0;
    dhcp_xid = (uint32_t)pit_ticks() ^ 0x4F52494FU;
    if (!dhcp_xid) dhcp_xid = 0x4F52494FU;
    next_ephemeral = 49152;
    next_ip_identification = 1;
    dhcp_state = 0;
    dhcp_retries = 0;
    network_initialized = 1;
    dhcp_send_message(1);
    return 0;
}

void net_poll(void) {
    if (!network_initialized) return;
    for (unsigned count = 0; count < 64; ++count) {
        uint16_t length = 0;
        int result = e1000_receive(receive_frame, sizeof(receive_frame), &length);
        if (result == E1000_RECEIVE_EMPTY) break;
        if (result == E1000_RECEIVE_FRAME) ethernet_process(receive_frame, length);
    }
    uint64_t now = pit_ticks();
    if (dhcp_state != 3 && now - dhcp_retry_tick >= (dhcp_retries < 4 ? 500 : 3000)) {
        if (dhcp_retries < 8) ++dhcp_retries;
        dhcp_send_message(dhcp_state == 2 ? 3 : 1);
    }
    for (size_t i = 0; i < NET_MAX_SOCKETS; ++i) {
        struct net_socket *socket = &sockets[i];
        if (!socket->used || (socket->state != TCP_STATE_SYN_SENT && socket->state != TCP_STATE_SYN_RECEIVED)) continue;
        if (now - socket->last_tx_tick < 100) continue;
        if (socket->retries >= 5) {
            socket->state = TCP_STATE_CLOSED;
            socket->peer_closed = 1;
            continue;
        }
        ++socket->retries;
        socket->last_tx_tick = (uint32_t)now;
        if (socket->state == TCP_STATE_SYN_SENT) {
            (void)tcp_send_raw(socket, TCP_FLAG_SYN, socket->tx_unacked, 0, NULL, 0);
        } else {
            (void)tcp_send_raw(socket, TCP_FLAG_SYN | TCP_FLAG_ACK, socket->tx_unacked, socket->rx_next, NULL, 0);
        }
    }
}

int net_get_config(struct net_config *config) {
    if (!config || !network_initialized) return -1;
    write_be32(config->address, local_address);
    write_be32(config->netmask, subnet_mask);
    write_be32(config->gateway, gateway_address);
    write_be32(config->dns, dns_address);
    config->dhcp_state = dhcp_state;
    return 0;
}

int net_dhcp_renew(void) {
    if (!network_initialized) return -1;
    dhcp_retries = 0;
    dhcp_state = 0;
    dhcp_send_message(1);
    return dhcp_state == 1 ? 0 : -1;
}

int net_socket_create(int domain, int type, int protocol, uint64_t owner) {
    if (!network_initialized || domain != NET_AF_INET) return -1;
    int nonblocking = (type & NET_SOCK_NONBLOCK) != 0;
    int base_type = type & ~NET_SOCK_NONBLOCK;
    if (base_type != NET_SOCK_STREAM && base_type != NET_SOCK_DGRAM) return -1;
    int selected_protocol = protocol ? protocol : (base_type == NET_SOCK_STREAM ? IP_PROTOCOL_TCP : IP_PROTOCOL_UDP);
    if ((base_type == NET_SOCK_STREAM && selected_protocol != IP_PROTOCOL_TCP) ||
        (base_type == NET_SOCK_DGRAM && selected_protocol != IP_PROTOCOL_UDP && selected_protocol != IP_PROTOCOL_ICMP)) return -1;
    int slot = socket_allocate(owner, (uint8_t)base_type);
    if (slot < 0) return -1;
    sockets[slot].protocol = (uint8_t)selected_protocol;
    sockets[slot].nonblocking = (uint8_t)nonblocking;
    return NET_FD_BASE + slot;
}

int net_socket_bind(int descriptor, uint64_t owner, uint32_t address, uint16_t port) {
    int slot;
    struct net_socket *socket = socket_get(descriptor, owner, &slot);
    if (!socket || socket->state != TCP_STATE_OPEN || (address && address != local_address) ||
        (port && port_in_use(socket->type, port, slot))) return -1;
    socket->local_address = address;
    socket->local_port = port;
    return 0;
}

int net_socket_listen(int descriptor, uint64_t owner, int backlog) {
    (void)backlog;
    int slot;
    struct net_socket *socket = socket_get(descriptor, owner, &slot);
    if (!socket || socket->type != NET_SOCK_STREAM || socket->state != TCP_STATE_OPEN) return -1;
    if (!socket->local_port) {
        socket->local_port = allocate_ephemeral(socket->type, slot);
        if (!socket->local_port) return -1;
    }
    socket->state = TCP_STATE_LISTEN;
    return 0;
}

static void wait_for_network_tick(void) {
    __asm__ volatile("sti; hlt; cli" : : : "memory");
}

static int wait_for_ipv4_address(void) {
    uint64_t start = pit_ticks();
    while (!local_address && pit_ticks() - start < 600) {
        net_poll();
        if (!local_address) wait_for_network_tick();
    }
    return local_address ? 0 : -1;
}

int net_socket_accept(int descriptor, uint64_t owner, int flags, uint32_t *address, uint16_t *port) {
    int listener_slot;
    struct net_socket *listener = socket_get(descriptor, owner, &listener_slot);
    if (!listener || listener->type != NET_SOCK_STREAM || listener->state != TCP_STATE_LISTEN) return -1;
    int nonblocking = (flags & NET_MSG_DONTWAIT) || listener->nonblocking;
    for (;;) {
        net_poll();
        if (listener->pending_slot >= 0 && listener->pending_slot < NET_MAX_SOCKETS) {
            int accepted_slot = listener->pending_slot;
            struct net_socket *accepted = &sockets[accepted_slot];
            listener->pending_slot = -1;
            accepted->listener_slot = -1;
            if (address) *address = accepted->remote_address;
            if (port) *port = accepted->remote_port;
            return NET_FD_BASE + accepted_slot;
        }
        if (nonblocking) return -1;
        wait_for_network_tick();
    }
}

int net_socket_connect(int descriptor, uint64_t owner, uint32_t address, uint16_t port) {
    int slot;
    struct net_socket *socket = socket_get(descriptor, owner, &slot);
    if (!socket || socket->state != TCP_STATE_OPEN || !address || (!port && socket->protocol != IP_PROTOCOL_ICMP)) return -1;
    if (wait_for_ipv4_address() != 0) return -1;
    if (socket->local_address && socket->local_address != local_address) return -1;
    socket->local_address = local_address;
    socket->remote_address = address;
    socket->remote_port = port;
    if (!socket->local_port) {
        socket->local_port = allocate_ephemeral(socket->type, slot);
        if (!socket->local_port) return -1;
    }
    if (socket->type == NET_SOCK_DGRAM) {
        socket->state = TCP_STATE_ESTABLISHED;
        return 0;
    }
    socket->state = TCP_STATE_SYN_SENT;
    socket->tx_unacked = initial_sequence(socket->local_port, port, address);
    socket->tx_next = socket->tx_unacked + 1;
    socket->last_tx_tick = (uint32_t)pit_ticks();
    socket->retries = 0;
    (void)tcp_send_raw(socket, TCP_FLAG_SYN, socket->tx_unacked, 0, NULL, 0);
    uint64_t start = pit_ticks();
    while (socket->state == TCP_STATE_SYN_SENT && pit_ticks() - start < 600) {
        net_poll();
        if (socket->state == TCP_STATE_SYN_SENT) wait_for_network_tick();
    }
    return socket->state == TCP_STATE_ESTABLISHED ? 0 : -1;
}

long net_socket_sendto(int descriptor, uint64_t owner, const void *buffer, size_t length, int flags, int has_address, uint32_t address, uint16_t port) {
    (void)flags;
    struct net_socket *socket = socket_get(descriptor, owner, NULL);
    if (!socket || socket->write_shutdown || length > 65536 || (length && !buffer)) return -1;
    if (!local_address && wait_for_ipv4_address() != 0) return -1;
    net_poll();
    if (socket->type == NET_SOCK_DGRAM) {
        if (has_address) {
            if (!address || (socket->protocol == IP_PROTOCOL_UDP && !port)) return -1;
        } else {
            address = socket->remote_address;
            port = socket->remote_port;
        }
        if (!address || (socket->protocol == IP_PROTOCOL_UDP && !port) || !local_address) return -1;
        if (socket->protocol == IP_PROTOCOL_ICMP) {
            if (length < 8 || length > NET_MAX_PACKET - IP_HEADER_SIZE || !buffer) return -1;
            return ipv4_send_from(local_address, address, IP_PROTOCOL_ICMP, buffer, (uint16_t)length) == 0 ? (long)length : -1;
        }
        if (!socket->local_port) {
            socket->local_port = allocate_ephemeral(socket->type, socket_slot_from_descriptor(descriptor));
            if (!socket->local_port) return -1;
        }
        if (length > NET_MAX_DATAGRAM) return -1;
        return udp_send_from(local_address, address, socket->local_port, port, buffer, (uint16_t)length) == 0 ? (long)length : -1;
    }
    if (socket->state != TCP_STATE_ESTABLISHED || !socket->remote_address || !socket->remote_port) return -1;
    if (!length) return 0;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t sent = 0;
    while (sent < length) {
        uint16_t amount = (uint16_t)(length - sent > NET_MAX_DATAGRAM ? NET_MAX_DATAGRAM : length - sent);
        if (!socket->remote_window) return sent ? (long)sent : -1;
        if (amount > socket->remote_window) amount = socket->remote_window;
        if (!amount) return sent ? (long)sent : -1;
        if (tcp_send_raw(socket, TCP_FLAG_ACK | TCP_FLAG_PSH, socket->tx_next, socket->rx_next, bytes + sent, amount) != 0) return sent ? (long)sent : -1;
        socket->tx_next += amount;
        socket->remote_window = (uint16_t)(socket->remote_window - amount);
        sent += amount;
    }
    return (long)sent;
}

long net_socket_recvfrom(int descriptor, uint64_t owner, void *buffer, size_t length, int flags, int has_address, uint32_t *address, uint16_t *port) {
    struct net_socket *socket = socket_get(descriptor, owner, NULL);
    if (!socket || socket->read_shutdown || length > 65536 || (length && !buffer)) return -1;
    int nonblocking = (flags & NET_MSG_DONTWAIT) || socket->nonblocking;
    for (;;) {
        net_poll();
        if (socket->type == NET_SOCK_DGRAM && socket->datagram_count) {
            struct socket_datagram *datagram = &socket->datagrams[socket->datagram_head];
            size_t copied = length < datagram->length ? length : datagram->length;
            if (copied) copy_bytes(buffer, datagram->data, copied);
            if (has_address) {
                if (address) *address = datagram->address;
                if (port) *port = datagram->port;
            }
            socket->datagram_head = (uint16_t)((socket->datagram_head + 1) % NET_RX_DATAGRAMS);
            --socket->datagram_count;
            return (long)copied;
        }
        if (socket->type == NET_SOCK_STREAM && socket->rx_length) {
            size_t copied = length < socket->rx_length ? length : socket->rx_length;
            if (copied) copy_bytes(buffer, socket->rx_data, copied);
            for (size_t i = copied; i < socket->rx_length; ++i) socket->rx_data[i - copied] = socket->rx_data[i];
            socket->rx_length = (uint16_t)(socket->rx_length - copied);
            if (copied && socket->state == TCP_STATE_ESTABLISHED) {
                (void)tcp_send_raw(socket, TCP_FLAG_ACK, socket->tx_next, socket->rx_next, NULL, 0);
            }
            return (long)copied;
        }
        if (socket->type == NET_SOCK_STREAM && (socket->peer_closed || socket->state == TCP_STATE_CLOSED)) return 0;
        if (nonblocking) return -1;
        wait_for_network_tick();
    }
}

int net_socket_shutdown(int descriptor, uint64_t owner, int how) {
    struct net_socket *socket = socket_get(descriptor, owner, NULL);
    if (!socket || how < 0 || how > 2) return -1;
    if (how == 0 || how == 2) socket->read_shutdown = 1;
    if (how == 1 || how == 2) {
        if (socket->type == NET_SOCK_STREAM && socket->state == TCP_STATE_ESTABLISHED && !socket->write_shutdown) {
            (void)tcp_send_raw(socket, TCP_FLAG_FIN | TCP_FLAG_ACK, socket->tx_next, socket->rx_next, NULL, 0);
            ++socket->tx_next;
        }
        socket->write_shutdown = 1;
    }
    return 0;
}

int net_socket_close(int descriptor, uint64_t owner) {
    int slot;
    struct net_socket *socket = socket_get(descriptor, owner, &slot);
    if (!socket) return -1;
    if (socket->type == NET_SOCK_STREAM && socket->state == TCP_STATE_ESTABLISHED && !socket->write_shutdown) {
        (void)tcp_send_raw(socket, TCP_FLAG_FIN | TCP_FLAG_ACK, socket->tx_next, socket->rx_next, NULL, 0);
    }
    if (socket->pending_slot >= 0 && socket->pending_slot < NET_MAX_SOCKETS && sockets[socket->pending_slot].used) {
        zero_bytes(&sockets[socket->pending_slot], sizeof(sockets[socket->pending_slot]));
    }
    for (size_t i = 0; i < NET_MAX_SOCKETS; ++i) {
        if (sockets[i].used && sockets[i].listener_slot == slot) zero_bytes(&sockets[i], sizeof(sockets[i]));
    }
    zero_bytes(socket, sizeof(*socket));
    return 0;
}

void net_close_owner(uint64_t owner) {
    for (int i = 0; i < NET_MAX_SOCKETS; ++i) {
        if (sockets[i].used && sockets[i].owner == owner) {
            int descriptor = NET_FD_BASE + i;
            (void)net_socket_close(descriptor, owner);
        }
    }
}
