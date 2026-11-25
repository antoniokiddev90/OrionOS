#include <arpa/inet.h>
#include <sys/dns.h>
#include <sys/network.h>
#include <stdio.h>
#include <string.h>

static int address_is_zero(const uint8_t address[4]) {
    return !(address[0] | address[1] | address[2] | address[3]);
}

static void print_address(const uint8_t address[4]) {
    printf("%u.%u.%u.%u", (unsigned)address[0], (unsigned)address[1],
           (unsigned)address[2], (unsigned)address[3]);
}

static void print_usage(void) {
    puts("Usage: nslookup hostname [dns-server-ipv4]");
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        print_usage();
        return 2;
    }
    struct orion_network_config config;
    if (orion_get_network_config(&config) != 0) {
        puts("nslookup: network configuration is unavailable");
        return 1;
    }
    uint8_t selected_server[4];
    const uint8_t *server = config.dns;
    if (argc == 3) {
        in_addr_t selected = inet_addr(argv[2]);
        if (selected == INADDR_NONE || selected == INADDR_ANY) {
            puts("nslookup: invalid DNS server IPv4 address");
            return 2;
        }
        memcpy(selected_server, &selected, sizeof(selected_server));
        server = selected_server;
    }
    if (address_is_zero(server)) {
        puts("nslookup: no DNS server is configured");
        return 1;
    }
    printf("Server: ");
    print_address(server);
    puts(":53");
    printf("Name: %s\n", argv[1]);
    uint8_t result[4];
    if (orion_dns_lookup_ipv4(argv[1], server, result) != 0) {
        puts("nslookup: DNS query failed or returned no IPv4 address");
        return 1;
    }
    printf("Address: ");
    print_address(result);
    putchar('\n');
    return 0;
}
