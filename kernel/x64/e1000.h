#ifndef X64_E1000_H
#define X64_E1000_H

#include <stdint.h>

#define E1000_RING_SIZE 128U
#define E1000_MAX_FRAME_SIZE 2048U
#define E1000_RECEIVE_ERROR (-1)
#define E1000_RECEIVE_EMPTY 0
#define E1000_RECEIVE_FRAME 1

int e1000_init(void);
int e1000_get_mac(uint8_t address[6]);
int e1000_send(const void *frame, uint16_t length);
int e1000_receive(void *buffer, uint16_t capacity, uint16_t *length);

#endif
