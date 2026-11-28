#ifndef ORION_SYS_INPUT_H
#define ORION_SYS_INPUT_H
#include <stdint.h>
#define ORION_INPUT_EVENT_KEY 1
#define ORION_INPUT_EVENT_MOUSE 2
#define ORION_INPUT_KEY_RELEASED 0
#define ORION_INPUT_KEY_PRESSED 1
#define ORION_INPUT_KEY_EXTENDED 0x100
#define ORION_INPUT_KEYBOARD_PATH "/dev/input/keyboard0"
#define ORION_INPUT_MOUSE_PATH "/dev/input/mouse0"
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
