#ifndef X64_PS2MOUSE_H
#define X64_PS2MOUSE_H
#include "input_event.h"
void ps2mouse_init(void);
void ps2mouse_irq(void);
int ps2mouse_read_event(struct orion_input_event *event);
int ps2mouse_available(void);
#endif
