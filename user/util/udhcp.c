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

static const char *state_name(uint8_t state) {
    if (state == ORION_DHCP_DISCOVERING) return "discovering";
    if (state == ORION_DHCP_REQUESTING) return "requesting";
    if (state == ORION_DHCP_BOUND) return "bound";
    return "unconfigured";
}

static int show_status(void) {
    struct orion_network_config config;
    if (orion_get_network_config(&config) != 0) {
        puts("udhcp: network configuration is unavailable");
        return 1;
    }
    printf("DHCP state: %s\n", state_name(config.dhcp_state));
    printf("IPv4 address: ");
    if (address_is_zero(config.address)) puts("unassigned");
    else {
        print_address(config.address);
        putchar('\n');
    }
    printf("Subnet mask: ");
    if (address_is_zero(config.netmask)) puts("unavailable");
    else {
        print_address(config.netmask);
        putchar('\n');
    }
    printf("Gateway: ");
    if (address_is_zero(config.gateway)) puts("unavailable");
    else {
        print_address(config.gateway);
        putchar('\n');
    }
    printf("DNS server: ");
    if (address_is_zero(config.dns)) puts("unavailable");
    else {
        print_address(config.dns);
        putchar('\n');
    }
    return 0;
}

static void print_usage(void) {
    puts("Usage: udhcp [status|renew]");
}

int main(int argc, char **argv) {
    const char *operation = argc > 1 ? argv[1] : "status";
    if (argc > 2 || (argc == 2 && strcmp(operation, "-r") != 0 && strcmp(operation, "status") != 0 && strcmp(operation, "renew") != 0)) {
        print_usage();
        return 2;
    }
    if (strcmp(operation, "status") == 0) return show_status();
    if (strcmp(operation, "renew") == 0 || strcmp(operation, "-r") == 0) {
        if (orion_dhcp_renew() != 0) {
            puts("udhcp: cannot request DHCP renewal");
            return 1;
        }
        puts("DHCP renewal requested");
        return 0;
    }
    print_usage();
    return 2;
}
