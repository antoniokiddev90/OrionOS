#ifndef X64_ATKBD_H
#define X64_ATKBD_H
#include <stdint.h>
#include "input_event.h"
void atkbd_init(void);
void atkbd_irq(void);
uint8_t atkbd_read_char(void);
int atkbd_read_event(struct orion_input_event *event);
#endif
