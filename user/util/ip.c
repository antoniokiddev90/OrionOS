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

static int mask_prefix(const uint8_t mask[4]) {
    int prefix = 0;
    int zero_seen = 0;
    for (size_t i = 0; i < 4; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            int set = (mask[i] >> bit) & 1;
            if (set && zero_seen) return -1;
            if (set) ++prefix;
            else zero_seen = 1;
        }
    }
    return prefix;
}

static int show_address(void) {
    struct orion_network_config config;
    if (orion_get_network_config(&config) != 0) {
        puts("ip: network configuration is unavailable");
        return 1;
    }
    if (address_is_zero(config.address)) {
        puts("1: eth0: <NO-IPv4> mtu 1500 state DOWN");
        return 0;
    }
    int prefix = mask_prefix(config.netmask);
    if (prefix < 0) {
        puts("ip: non-contiguous IPv4 netmask is unsupported");
        return 1;
    }
    puts("1: eth0: <UP> mtu 1500 state UNKNOWN");
    printf("    inet ");
    print_address(config.address);
    printf("/%u\n", (unsigned)prefix);
    return 0;
}

static int show_routes(void) {
    struct orion_network_config config;
    if (orion_get_network_config(&config) != 0) {
        puts("ip: network configuration is unavailable");
        return 1;
    }
    if (address_is_zero(config.address)) {
        puts("ip: no IPv4 routes");
        return 0;
    }
    int prefix = mask_prefix(config.netmask);
    if (prefix < 0) {
        puts("ip: non-contiguous IPv4 netmask is unsupported");
        return 1;
    }
    uint8_t network[4];
    for (size_t i = 0; i < 4; ++i) network[i] = config.address[i] & config.netmask[i];
    print_address(network);
    printf("/%u dev eth0 scope link src ", (unsigned)prefix);
    print_address(config.address);
    putchar('\n');
    if (!address_is_zero(config.gateway)) {
        printf("default via ");
        print_address(config.gateway);
        puts(" dev eth0");
    }
    return 0;
}

static void print_usage(void) {
    puts("Usage: ip [-4] [addr|address|route] [show]");
}

int main(int argc, char **argv) {
    int index = 1;
    if (index < argc && strcmp(argv[index], "-4") == 0) ++index;
    const char *operation = index < argc ? argv[index++] : "addr";
    if (index < argc && strcmp(argv[index], "show") == 0) ++index;
    if (index != argc) {
        print_usage();
        return 2;
    }
    if (strcmp(operation, "addr") == 0 || strcmp(operation, "address") == 0) return show_address();
    if (strcmp(operation, "route") == 0) return show_routes();
    print_usage();
    return 2;
}
