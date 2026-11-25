#ifndef ORION_SYS_NETWORK_H
#define ORION_SYS_NETWORK_H

#include <stdint.h>

enum {
    ORION_DHCP_UNCONFIGURED = 0,
    ORION_DHCP_DISCOVERING = 1,
    ORION_DHCP_REQUESTING = 2,
    ORION_DHCP_BOUND = 3
};

struct orion_network_config {
    uint8_t address[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t dhcp_state;
};

int orion_get_network_config(struct orion_network_config *config);
int orion_dhcp_renew(void);

#endif
