#ifndef ORION_TLS_H
#define ORION_TLS_H

#include <stddef.h>

int orion_tls_connect(int descriptor, const char *hostname);
long orion_tls_send(int descriptor, const void *buffer, size_t length);
long orion_tls_recv(int descriptor, void *buffer, size_t capacity);
void orion_tls_close(int descriptor);

#endif
