#ifndef ORION_SYS_DNS_H
#define ORION_SYS_DNS_H

#include <stdint.h>

int orion_dns_lookup_ipv4(const char *hostname, const uint8_t server[4], uint8_t address[4]);

#endif
