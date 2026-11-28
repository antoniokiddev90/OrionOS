#ifndef ORION_SYS_IPC_H
#define ORION_SYS_IPC_H
#include <stddef.h>
#include <stdint.h>
#define ORION_IPC_MAX_MESSAGE 512
long orion_ipc_send(uint64_t receiver_pid, const void *message, size_t size);
long orion_ipc_receive(uint64_t sender_pid, void *message, size_t capacity, uint64_t *sender_out);
#endif
