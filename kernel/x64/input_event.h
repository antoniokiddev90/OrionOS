#ifndef X64_INPUT_EVENT_H
#define X64_INPUT_EVENT_H
#include <stdint.h>
#define ORION_INPUT_EVENT_KEY 1
#define ORION_INPUT_EVENT_MOUSE 2
struct orion_input_event {
    uint64_t ticks;
    uint16_t type;
    uint16_t code;
    int32_t value;
    int32_t x;
    int32_t y;
    uint32_t buttons;
    uint32_t reserved;
};
#endif
