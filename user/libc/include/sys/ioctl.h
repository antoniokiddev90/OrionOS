#ifndef ORION_SYS_IOCTL_H
#define ORION_SYS_IOCTL_H
#include <stdint.h>
int ioctl(int descriptor, unsigned long request, void *argument);
#endif
